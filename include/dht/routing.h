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

    void traverse(hash_t, tree**, int&);
    void split(tree*, int);
    void update(peer);
    void evict(peer);
    void update_pending(peer);
    bucket find_bucket(peer);
    int stale(peer);
    
    hash_t id;
    
    network& net;

    std::mutex mutex;

private:
    std::shared_ptr<routing_table> strong_ref;
    tree* root;
};

struct tree {
    struct tree* left;
    struct tree* right;
    bucket data;
    bool leaf;

    std::list<peer> cache;
    std::mutex cache_mutex;

    tree(routing_table&);
    ~tree();
};

}
}

#endif