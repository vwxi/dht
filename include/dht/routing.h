#ifndef _ROUTING_H
#define _ROUTING_H

#include "util.hpp"
#include "bucket.h"

namespace lotus {
namespace dht {

struct tree;
class node;

class routing_table : public std::enable_shared_from_this<routing_table> {
public:
    routing_table(hash_t, network&);
    ~routing_table();

    void init();

    void traverse(bool, hash_t, tree**, int&);
    void dfs(std::function<void(tree*)>);
    void split(tree*, int);
    void update(net_peer);
    void stale(net_peer);
    bucket& find_bucket(hash_t);
    std::deque<routing_table_entry> find_alpha(hash_t);
    boost::optional<routing_table_entry> find(hash_t);

    hash_t id;
    
    network& net;

    boost::shared_mutex mutex;

    tree* root;

private:
    void _dfs(std::function<void(tree*)>, tree*);

    std::shared_ptr<routing_table> strong_ref;
};

struct tree {
    tree* parent;
    tree* left;
    tree* right;
    struct { 
        hash_t prefix;
        int cutoff;
    } prefix;
    bucket data;
    bool leaf;

    tree(std::shared_ptr<routing_table>);
    ~tree();
};

}
}

#endif