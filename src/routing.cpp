#include "routing.h"
#include "bucket.h"
#include "network.h"
#include "util.hpp"

/// @private
/// @brief internal macro that traverses a tree based on the peer's id and also
/// tries to find peer within current bucket after traversal
#define TRAVERSE(I) \
    tree* ptr = NULL; \
    bucket::iterator it; \
    int cutoff = 0; \
    { \
        R_LOCK(mutex); \
        ptr = root; \
        traverse(false, I, &ptr, cutoff); \
        assert(ptr != nullptr); \
        it = std::find_if(ptr->data.begin(), ptr->data.end(), \
            [&](const routing_table_entry& e) { return e.id == I; }); \
    }

namespace lotus {
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
void routing_table::update(net_peer req) {
    TRAVERSE(req.id);

    W_LOCK(mutex);

    hash_t mask(~hash_t(0) << (proto::bit_hash_width - cutoff));

    if(it == ptr->data.end() && ptr->data.size() < ptr->data.max_size) {
        // bucket is not full and peer doesnt exist yet, add to bucket
        ptr->data.add_new(req);
    } else {
        if(it != ptr->data.end()) {
            if((req.id & mask) == (id & mask)) {
                if(it == ptr->data.end()) {
                    spdlog::debug("routing: bucket is within prefix, split");
                    // bucket is full and within our own prefix, split
                    /// @todo relaxed splitting, see https://stackoverflow.com/questions/32129978/highly-unbalanced-kademlia-routing-table/32187456#32187456
                    split(ptr, cutoff);
                } else {
                    // bucket is full but nearby, update node
                    ptr->data.update_near_entry(req);
                }
            } else {
                if(it != ptr->data.end()) {
                    // node is known to us already but far so ping to check liveness
                    ptr->data.update_far_entry(req);
                } else {
                    // add/update entry in replacement cache
                    ptr->data.update_cache(req);
                }
            }
        }
    }
}

void routing_table::stale(net_peer req) {
    TRAVERSE(req.id);
    ptr->data.stale(req);
}

bucket& routing_table::find_bucket(hash_t req) {
    TRAVERSE(req);
    return ptr->data;
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

std::deque<routing_table_entry> routing_table::find_alpha(hash_t req) {
    R_LOCK(mutex);

    TRAVERSE(req);

    std::deque<routing_table_entry> res;

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

boost::optional<routing_table_entry> routing_table::find(hash_t id) {
    R_LOCK(mutex);

    TRAVERSE(id);
    
    return (it != ptr->data.end()) ? *it : boost::optional<routing_table_entry>(boost::none);
}

}
}

#undef TRAVERSE