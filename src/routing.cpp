#include "routing.h"
#include "bucket.h"
#include "network.h"

#define TRAVERSE \
    tree* ptr = root; \
    int i = 0; \
    traverse(req.id, &ptr, i); \
    assert(ptr); \
    auto it = std::find_if(ptr->data->begin(), ptr->data->end(), \
        [&](peer p) { return p.id == req.id; }); 

namespace dht {

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

routing_table::routing_table(hash_t id_, node& node__) : id(id_), node_(node__) {
    root = new tree(*this);
};

routing_table::~routing_table() {
    delete root;
}

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

bool routing_table::exists(peer req) {
    TRAVERSE;   

    return it != ptr->data->end();
}

void routing_table::update(peer req) {
    TRAVERSE;

    hash_t prefix(((~hash_t(0) << (proto::I - i)) & ~hash_t(0)));
        
    if(ptr->data->size() < ptr->data->max_size) {
        spdlog::warn("bucket isn't full, update node {}", util::htos(req.id));
        ptr->data->update(req, true);
    } else {
        if((req.id & prefix) == (id & prefix)) {
            if(it == ptr->data->end()) {
                spdlog::warn("bucket is within prefix, split");
                split(ptr, i);
            } else {
                spdlog::warn("bucket is nearby and full");
                ptr->data->update(req, true);
            }
        } else {
            spdlog::warn("bucket is far and full");
            if(exists(req)) {
                ptr->data->update(req, false);
                spdlog::info("node {} exists in table, updating normally", util::htos(req.id));
            } else {
                std::lock_guard<std::mutex> g(ptr->cache_mutex);
                auto cit = std::find_if(ptr->cache->begin(), ptr->cache->end(),
                    [&](peer p) { return p.id == req.id; });
                
                if(cit == ptr->cache->end()) {
                    ptr->cache->push_back(req);
                    spdlog::info("node {} is unknown, adding to replacement cache", util::htos(req.id));
                } else {
                    ptr->cache->splice(ptr->cache->end(), *(ptr->cache), cit);
                    spdlog::info("node {} is unknown, moving to end of replacement cache", util::htos(req.id));
                }
            }
        }
    }
}

// will do nothing if peer isn't in table
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

// will do nothing if peer isn't in table
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

bucket routing_table::find_bucket(peer req) {
    TRAVERSE;

    return *(ptr->data);
}

}