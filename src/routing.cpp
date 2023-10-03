#include "routing.h"
#include "bucket.h"
#include "network.h"
#include "util.hpp"

/// @private
/// @brief internal macro that traverses a tree based on the peer's id and also
/// tries to find peer within current bucket after traversal
#define TRAVERSE \
    tree* ptr = NULL; \
    bucket::iterator it; \
    int cutoff = 0; \
    { \
        R_LOCK(mutex); \
        ptr = root; \
        traverse(false, req.id, &ptr, cutoff); \
        assert(ptr != nullptr); \
        it = std::find_if(ptr->data.begin(), ptr->data.end(), \
            [&](const peer& p) { return p.id == req.id; }); \
    }

namespace tulip {
namespace dht {

/// @brief initialize a tree
tree::tree(std::shared_ptr<routing_table> rt) :
    data(rt), leaf(true), left(nullptr), right(nullptr) { }

tree::~tree() {
    delete left; left = nullptr;
    delete right; right = nullptr;
}

// routing table is a XOR-trie
routing_table::routing_table(hash_t id_, network& net_) : id(id_), net(net_) { };
routing_table::~routing_table() { delete root; root = nullptr; }

void routing_table::init() {
    strong_ref = shared_from_this();

    W_LOCK(mutex);

    root = new tree(shared_from_this());
    root->parent = nullptr;
    root->prefix.prefix = hash_t(0);
    root->prefix.cutoff = 0;
}

/// @brief take a ptr to the ptr of some root and traverse based on bits of id
void routing_table::traverse(bool p, hash_t t, tree** ptr, int& cutoff) {
    if(!ptr) return;
    if(!*ptr) return;

    while((*(*ptr)).leaf == false) {
        if(t & (1 << (proto::bit_hash_width - cutoff++))) {
            if(!(*(*ptr)).right) { *ptr = NULL; return; }
            else if(p && (*ptr)->right->leaf) return;
            *ptr = (*ptr)->right;
        } else {
            if(!(*ptr)->left) { *ptr = NULL; return; }
            else if(p && (*ptr)->left->leaf) return;
            *ptr = (*ptr)->left;
        }
    }
}

/// @brief split a tree ptr into two subtrees, categorize contained nodes into new subtrees
void routing_table::split(tree* t, int cutoff) {
    if(!t) return;

    t->left = new tree(shared_from_this());
    if(!t->left) return;
    t->left->parent = t;
    t->left->prefix.prefix = t->prefix.prefix;
    t->left->prefix.cutoff = cutoff + 1;

    t->right = new tree(shared_from_this());
    if(!t->right) return;
    t->right->parent = t;
    t->right->prefix.prefix = t->prefix.prefix | (hash_t(1) << (proto::bit_hash_width - cutoff));
    t->right->prefix.cutoff = cutoff + 1;

    t->leaf = false;

    for(auto& it : t->data) {
        if(it.id & (1 << (proto::bit_hash_width - cutoff))) {
            t->right->data.push_back(it);
        } else { 
            t->left->data.push_back(it);
        }
    }

    t->data.clear();
}

/// @brief update peer in routing table whether or not it exists within table
void routing_table::update(peer req) {
    TRAVERSE;

    // mismatched node - diff ip same id
    if(it != ptr->data.end() && !(req == *it))
        return;

    W_LOCK(mutex);

    hash_t mask(~hash_t(0) << (proto::bit_hash_width - cutoff));
    
    if(ptr->data.size() < ptr->data.max_size) {
        spdlog::debug("bucket isn't full, update node {}", util::htos(req.id));
        // bucket is not full, update node
        ptr->data.update(req, true);
    } else {
        if((req.id & mask) == (id & mask)) {
            if(it == ptr->data.end()) {
                spdlog::debug("bucket is within prefix, split");
                // bucket is full and within our own prefix, split
                /// @todo relaxed splitting, see https://stackoverflow.com/questions/32129978/highly-unbalanced-kademlia-routing-table/32187456#32187456
                split(ptr, cutoff);
            } else {
                spdlog::debug("bucket is nearby and full");
                // bucket is full but nearby, update node
                ptr->data.update(req, true);
            }
        } else {
            spdlog::debug("bucket is far and full");
            if(it != ptr->data.end()) {
                // node is known to us already, ping normally
                ptr->data.update(req, false);
                spdlog::debug("node {} exists in table, updating normally", util::htos(req.id));
            } else {
                LOCK(ptr->cache_mutex);
                
                // node is unknown
                auto cit = std::find_if(ptr->cache.begin(), ptr->cache.end(),
                    [&](peer p) { return p.id == req.id; });
                
                if(cit == ptr->cache.end()) {
                    // is the cache full? kick out oldest node and add this one
                    if(ptr->cache.size() > proto::repl_cache_size) {
                        spdlog::debug("replacement cache is full, removing oldest candidate");
                        ptr->cache.pop_front();
                    }
                    
                    // node is unknown and doesn't exist in cache, add
                    ptr->cache.push_back(req);
                    spdlog::debug("node {} is unknown, adding to replacement cache", util::htos(req.id));
                } else {
                    // node is unknown and exists in cache, move to back
                    ptr->cache.splice(ptr->cache.end(), ptr->cache, cit);
                    spdlog::debug("node {} is unknown, moving to end of replacement cache", util::htos(req.id));
                }
            }
        }
    }
}

/// @brief evict peer from routing table, repeated calls will do nothing
void routing_table::evict(peer req) {
    TRAVERSE;

    // does peer exist in table?
    if(it != ptr->data.end()) {
        // erase from bucket
        ptr->data.erase(it);

        // if cache has peer waiting, add most recently noticed node to bucket
        if(ptr->cache.size() > 0) {
            auto cit = ptr->cache.end();
            cit--;
            spdlog::debug("there is peer ({}) in the cache waiting, add it to the bucket", util::htos(cit->id));
            ptr->data.push_back(*cit);
            ptr->cache.erase(cit);
        }
    }
}

/// @brief update peer that was in node's pending list
/// @note this is used for pings to update bucket peers
void routing_table::update_pending(peer req) {
    TRAVERSE;

    W_LOCK(mutex);
    
    // does peer exist in table?
    if(it != ptr->data.end()) {
        // if peer isn't too stale, decrement staleness and move to end of bucket
        if(req.staleness++ < proto::missed_pings_allowed) {
            req.staleness--;
            ptr->data.splice(ptr->data.end(), ptr->data, it);
            spdlog::debug("pending node {} updated", util::htos(req.id));
        } else {
            // otherwise, erase peer from bucket
            ptr->data.erase(it);
            spdlog::debug("erasing pending node {}", util::htos(req.id));
        }
    }
}

/// @brief find bucket based on peer id
bucket routing_table::find_bucket(peer req) {
    TRAVERSE;

    return ptr->data;
}

/// @brief increment staleness by one
int routing_table::stale(peer req) {
    TRAVERSE;

    W_LOCK(mutex);

    if(it != ptr->data.end())
        return it->staleness++;
    return -1;
}

void routing_table::_dfs(std::function<void(tree*)> fn, tree* ptr) {
    if(ptr == nullptr) 
        return;

    R_LOCK(mutex);

    if(ptr->leaf && ptr->data.empty()) 
        return;

    fn(ptr);

    if(ptr->left) _dfs(fn, ptr->left);
    if(ptr->right) _dfs(fn, ptr->right);
}

void routing_table::dfs(std::function<void(tree*)> fn) {
    tree* ptr = root;
    _dfs(fn, ptr);
}

std::deque<peer> routing_table::find_alpha(peer req) {
    TRAVERSE;

    R_LOCK(mutex);

    std::deque<peer> res;

    int n = 0;
    for(auto e = ptr->data.begin(); e != ptr->data.end() && n++ < proto::alpha; ++e)
        res.push_back(*e);

    // try and get more contacts from sibling tree nodes if there aren't enough 
    if(res.size() < proto::alpha && ptr->parent != nullptr) {
        ptr = ptr->parent->left == ptr ? ptr->parent->right : ptr->parent->left;
        std::copy_n(ptr->data.begin(), proto::alpha - res.size(), std::back_inserter(res));
    }

    // nothing we can do afterwards
    return res;
}

}
}

#undef TRAVERSE