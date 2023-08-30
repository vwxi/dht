#ifndef _ROUTING_H
#define _ROUTING_H

#include "util.hpp"
#include "bucket.h"

namespace tulip {
namespace dht {

struct tree;
class peer;
class node;

class routing_table : public std::enable_shared_from_this<routing_table> {
public:
    routing_table(hash_t, network&);
    ~routing_table();

    void init();

    void traverse(bool, hash_t, tree**, int&);
    void dfs(std::function<void(tree*)>);
    void split(tree*, int);
    void update(peer);
    void evict(peer);
    void update_pending(peer);
    bucket find_bucket(peer);
    int stale(peer);
    
    hash_t id;
    
    network& net;

    boost::shared_mutex mutex;

    tree* root;

private:
    void _dfs(std::function<void(tree*)>, tree*);

    std::shared_ptr<routing_table> strong_ref;
};

struct tree {
    tree* left;
    tree* right;
    struct { 
        hash_t prefix;
        int cutoff;
    } prefix;
    bucket data;
    bool leaf;

    std::list<peer> cache;
    std::mutex cache_mutex;

    tree(std::shared_ptr<routing_table>);
    ~tree();
};

}
}

#endif