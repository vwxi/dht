#ifndef _ROUTING_H
#define _ROUTING_H

#include "util.hpp"
#include "bucket.hpp"

namespace lotus {
namespace dht {

template <typename Network, typename Bucket>
struct tree;

template <typename Network, typename Bucket>
class node;

template <typename Network, typename Bucket>
class routing_table : public std::enable_shared_from_this<routing_table<Network, Bucket>> {
public:
    routing_table(hash_t, Network&);
    ~routing_table();

    void init();

    void traverse(bool, hash_t, tree<Network, Bucket>**, int&);
    void dfs(std::function<void(tree<Network, Bucket>*)>);
    void split(tree<Network, Bucket>*, int);
    void update(net_peer);
    void stale(net_peer);
    Bucket& find_bucket(hash_t);
    std::deque<routing_table_entry> find_alpha(hash_t);
    boost::optional<routing_table_entry> find(hash_t);

    hash_t id;
    
    Network& net;

    boost::shared_mutex mutex;

    tree<Network, Bucket>* root;

private:
    void _dfs(std::function<void(tree<Network, Bucket>*)>, tree<Network, Bucket>*);

    std::shared_ptr<routing_table> strong_ref;
};

template <typename Network, typename Bucket>
struct tree {
    tree<Network, Bucket>* parent;
    tree<Network, Bucket>* left;
    tree<Network, Bucket>* right;
    struct { 
        hash_t prefix;
        int cutoff;
    } prefix;
    
    Bucket data;
    bool leaf;

    explicit tree(std::shared_ptr<routing_table<Network, Bucket>>);
    ~tree();
};

}
}

#endif