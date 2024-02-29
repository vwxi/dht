#include "routing.hpp"
#include "bucket.hpp"
#include "network.hpp"
#include "util.hpp"

/// @private
/// @brief internal macro that traverses a tree based on the peer's id and also
/// tries to find peer within current bucket after traversal
/// @todo make function for this instead
#define TRAVERSE(I) \
    tree<Network, Bucket>* ptr = NULL; \
    typename bucket<Network>::iterator it; \
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
template <typename Network, typename Bucket>
tree<Network, Bucket>::tree(std::shared_ptr<routing_table<Network, Bucket>> rt) :
    data(rt), leaf(true), left(nullptr), right(nullptr), parent(nullptr) { }

template <typename Network, typename Bucket>
tree<Network, Bucket>::~tree() {
    delete left; left = nullptr;
    delete right; right = nullptr;
}

// routing table is a XOR-trie
template <typename Network, typename Bucket>
routing_table<Network, Bucket>::routing_table(hash_t id_, Network& net_) : 
    id(id_), net(net_), root(nullptr) { }

template <typename Network, typename Bucket>
routing_table<Network, Bucket>::~routing_table() {
    if(root != nullptr) { 
        delete root; 
        root = nullptr; 
    }
}

template <typename Network, typename Bucket>
void routing_table<Network, Bucket>::init() {
    strong_ref = this->shared_from_this();

    W_LOCK(mutex);

    root = new tree<Network, Bucket>(this->shared_from_this());
    root->parent = nullptr;
    root->prefix.prefix = hash_t(0);
    root->prefix.cutoff = 0;
}

/// @brief take a ptr to the ptr of some root and traverse based on bits of id
template <typename Network, typename Bucket>
void routing_table<Network, Bucket>::traverse(bool p, hash_t t, tree<Network, Bucket>** ptr, int& cutoff) {
    if(!ptr) return;
    if(!*ptr) return;

    while((*(*ptr)).leaf == false) {
        if(t & (1 << (proto::bit_hash_width - ++cutoff))) {
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
template <typename Network, typename Bucket>
void routing_table<Network, Bucket>::split(tree<Network, Bucket>* t, int cutoff) {
    if(!t) return;

    t->left = new tree<Network, Bucket>(this->shared_from_this());
    if(!t->left) return;
    t->left->parent = t;
    t->left->prefix.prefix = t->prefix.prefix;
    t->left->prefix.cutoff = cutoff + 1;

    t->right = new tree<Network, Bucket>(this->shared_from_this());
    if(!t->right) return;
    t->right->parent = t;
    t->right->prefix.prefix = t->prefix.prefix | (hash_t(1) << (proto::bit_hash_width - cutoff));
    t->right->prefix.cutoff = cutoff + 1;

    t->leaf = false;

    for(auto& it : t->data) {
        if((it.id & (hash_t(1) << (proto::bit_hash_width - (cutoff + 1))))) {
            t->right->data.push_back(it);
        } else { 
            t->left->data.push_back(it);
        }
    }

    if(t->left->data.size() > proto::bucket_size)
        t->left->data.resize(proto::bucket_size);
    
    if(t->right->data.size() > proto::bucket_size)
        t->right->data.resize(proto::bucket_size);

    t->data.clear();
}

/// @brief update peer in routing table whether or not it exists within table
template <typename Network, typename Bucket>
void routing_table<Network, Bucket>::update(net_peer req) {
    TRAVERSE(req.id);

    W_LOCK(mutex);

    hash_t mask(~hash_t(0) << (proto::bit_hash_width - cutoff));

    if(it == ptr->data.end() && ptr->data.size() < proto::bucket_size) {
        // bucket is not full and peer doesnt exist yet, add to bucket
        ptr->data.add_or_update_near_entry(req);
    } else {
        if(it != ptr->data.end()) {
            if((req.id & mask) == (id & mask)) {
                // bucket is full but nearby, update node
                ptr->data.add_or_update_near_entry(req);
            } else {
                // node is known to us already but far so ping to check liveness
                ptr->data.update_far_entry(req);
            }
        } else {
            if((req.id & mask) == (id & mask)) {
                spdlog::debug("routing: bucket is within prefix, split");
                // bucket is full and within our own prefix, split
                /// @todo relaxed splitting, see https://stackoverflow.com/questions/32129978/highly-unbalanced-kademlia-routing-table/32187456#32187456
                ptr->data.emplace_back(req.id, req.addr);
                split(ptr, cutoff);
            } else {
                // add/update entry in replacement cache
                ptr->data.update_cache(req);
            }
        }
    }
}

template <typename Network, typename Bucket>
void routing_table<Network, Bucket>::stale(net_peer req) {
    TRAVERSE(req.id);
    ptr->data.stale(req);
}

template <typename Network, typename Bucket>
Bucket& routing_table<Network, Bucket>::find_bucket(hash_t req) {
    TRAVERSE(req);
    
    return ptr->data;
}

template <typename Network, typename Bucket>
void routing_table<Network, Bucket>::_dfs(std::function<void(tree<Network, Bucket>*)> fn, tree<Network, Bucket>* ptr) {
    if(ptr == nullptr) 
        return;

    R_LOCK(mutex);

    if(ptr->leaf && ptr->data.empty()) 
        return;

    fn(ptr);

    if(ptr->left) _dfs(fn, ptr->left);
    if(ptr->right) _dfs(fn, ptr->right);
}

template <typename Network, typename Bucket>
void routing_table<Network, Bucket>::dfs(std::function<void(tree<Network, Bucket>*)> fn) {
    tree<Network, Bucket>* ptr = root;
    _dfs(fn, ptr);
}

template <typename Network, typename Bucket>
std::deque<routing_table_entry> routing_table<Network, Bucket>::find_alpha(hash_t req) {
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

template <typename Network, typename Bucket>
boost::optional<routing_table_entry> routing_table<Network, Bucket>::find(hash_t id) {
    R_LOCK(mutex);

    TRAVERSE(id);
    
    return (it != ptr->data.end()) ? *it : boost::optional<routing_table_entry>(boost::none);
}

// handle test cases
EINST(tree, test::mock_network, bucket<test::mock_network>);
EINST(routing_table, test::mock_network, bucket<test::mock_network>);
EINST(routing_table, test::mock_rt_net_resp, bucket<test::mock_rt_net_resp>);
EINST(routing_table, test::mock_rt_net_unresp, bucket<test::mock_rt_net_unresp>);
EINST(routing_table, test::mock_rt_net_maybe, bucket<test::mock_rt_net_maybe>);

}
}

#undef TRAVERSE