#ifndef _ROUTING_H
#define _ROUTING_H

#include "util.hpp"
#include "bucket.h"

namespace dht {

struct tree;
class peer;
class node;

class routing_table {
public:
    routing_table(hash_t, node&);
    ~routing_table();

    void traverse(hash_t, tree**, int&);
    void split(tree*, int);
    bool exists(peer);
    void update(peer);
    void evict(peer);
    void update_pending(peer);
    bucket find_bucket(peer);
    
    hash_t id;
    
    node& node_;

    std::mutex mutex;

private:
    tree* root;
};

struct tree {
    struct tree* left;
    struct tree* right;
    std::shared_ptr<bucket> data;
    bool leaf;

    std::shared_ptr<std::list<peer>> cache;
    std::mutex cache_mutex;

    tree(routing_table&);
    ~tree();
};

}

#endif