#include "routing.h"
#include "bucket.h"
#include "network.h"

/// @private
/// @brief internal macro that traverses a tree based on the peer's id and also
/// tries to find peer within current bucket after traversal
#define TRAVERSE \
    tree* ptr = root; \
    int i = 0; \
    traverse(req.id, &ptr, i); \
    assert(ptr); \
    auto it = std::find_if(ptr->data->begin(), ptr->data->end(), \
        [&](peer p) { return p.id == req.id; }); 

namespace dht {

/// @brief initialize a tree
/// @param rt routing_table reference
tree::tree(routing_table& rt) {
    left = right = nullptr;
    data = std::make_shared<bucket>(rt);
    cache = std::make_shared<std::list<peer>>();
    leaf = true;
}

tree::~tree() {
    delete left;
    delete right;
    data.reset();
    cache.reset();
    cache_mutex.unlock();
}

/// @brief initialize a routing table
/// @param id_ root id
/// @param node__ node reference
routing_table::routing_table(hash_t id_, node& node__) : id(id_), node_(node__) {
    root = new tree(*this);
};

routing_table::~routing_table() {
    delete root;
}

/// @brief take a ptr to the ptr of some root and traverse based on bits of id
/// @param id id to guide traversal
/// @param ptr ptr to move around
/// @param i number of branches traversed
void routing_table::traverse(hash_t id, tree** ptr, int& i) {
    if(!ptr) return;
    if(!*ptr) return;

    while((*ptr)->leaf == false) {
        if(id[proto::I - i++]) {
            if(!(*ptr)->right) { *ptr = NULL; return; }
            *ptr = (*ptr)->right;
        } else {
            if(!(*ptr)->left) { *ptr = NULL; return; }
            *ptr = (*ptr)->left;
        }
    }
}

/// @brief split a tree ptr into two subtrees, categorize contained nodes into new subtrees
/// @param t tree ptr to split
/// @param i bit index to determine categorization
void routing_table::split(tree* t, int i) {
    if(!t) return;

    t->left = new tree(*this);
    if(!t->left) return;

    t->right = new tree(*this);
    if(!t->right) return;

    t->leaf = false;

    for(auto& it : *(t->data)) {
        if(it.id[proto::I - i]) {
            t->right->data->push_back(it);
        } else { 
            t->left->data->push_back(it);
        }
    }

    t->data->clear();
}

/// @brief check if peer exists in routing table
/// @param req peer struct
/// @return true if so, false if not
bool routing_table::exists(peer req) {
    TRAVERSE;
    return it != ptr->data->end();
}

/// @brief update peer in routing table whether or not it exists within table
/// @param req peer struct
void routing_table::update(peer req) {
    TRAVERSE;

    hash_t prefix(((~hash_t(0) << (proto::I - i)) & ~hash_t(0)));
        
    if(ptr->data->size() < ptr->data->max_size) {
        spdlog::warn("bucket isn't full, update node {}", util::htos(req.id));
        // bucket is not full, update node
        ptr->data->update(req, true);
    } else {
        if((req.id & prefix) == (id & prefix)) {
            if(it == ptr->data->end()) {
                spdlog::warn("bucket is within prefix, split");
                // bucket is full and within our own prefix, split
                split(ptr, i);
            } else {
                spdlog::warn("bucket is nearby and full");
                // bucket is full but nearby, update node
                ptr->data->update(req, true);
            }
        } else {
            spdlog::warn("bucket is far and full");
            if(exists(req)) {
                // node is known to us already, ping normally
                ptr->data->update(req, false);
                spdlog::info("node {} exists in table, updating normally", util::htos(req.id));
            } else {
                // node is unknown
                std::lock_guard<std::mutex> g(ptr->cache_mutex);
                auto cit = std::find_if(ptr->cache->begin(), ptr->cache->end(),
                    [&](peer p) { return p.id == req.id; });
                
                if(cit == ptr->cache->end()) {
                    // is the cache full? kick out oldest node and add this one
                    if(ptr->cache->size() > proto::C) {
                        spdlog::info("replacement cache is full, removing oldest candidate");
                        ptr->cache->pop_front();
                    }
                    
                    // node is unknown and doesn't exist in cache, add
                    ptr->cache->push_back(req);
                    spdlog::info("node {} is unknown, adding to replacement cache", util::htos(req.id));
                } else {
                    // node is unknown and exists in cache, move to back
                    ptr->cache->splice(ptr->cache->end(), *(ptr->cache), cit);
                    spdlog::info("node {} is unknown, moving to end of replacement cache", util::htos(req.id));
                }
            }
        }
    }
}

/// @brief evict peer from routing table, repeated calls will do nothing
/// @param req peer struct
void routing_table::evict(peer req) {
    TRAVERSE;

    if(it != ptr->data->end()) {
        ptr->data->erase(it);

        if(ptr->cache->size() > 0) {
            auto cit = ptr->cache->end();
            cit--;
            spdlog::info("there is peer ({}) in the cache waiting, add it to the bucket", util::htos(cit->id));
            ptr->data->push_back(*cit);
            ptr->cache->erase(cit);
        }
    }
}

/// @brief update peer that was in node_'s pending list
/// @param req peer struct
void routing_table::update_pending(peer req) {
    TRAVERSE;

    if(it != ptr->data->end()) {
        if(req.staleness++ < proto::M) {
            req.staleness--;
            ptr->data->splice(ptr->data->end(), *(ptr->data), it);
            spdlog::info("pending node {} updated", util::htos(req.id));
        } else {
            ptr->data->erase(it);
            spdlog::info("erasing pending node {}", util::htos(req.id));
        }
    }
}

/// @brief find bucket based on peer id
/// @param req peer struct
/// @return bucket object
bucket routing_table::find_bucket(peer req) {
    TRAVERSE;

    return *(ptr->data);
}

/// @brief increment staleness by one
/// @param req peer struct
int routing_table::stale(peer req) {
    TRAVERSE;

    if(it != ptr->data->end())
        return it->staleness++;
    return -1;
}

}

#undef TRAVERSE