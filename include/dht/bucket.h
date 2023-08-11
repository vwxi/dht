#ifndef _BUCKET_H
#define _BUCKET_H

#include "util.hpp"

namespace tulip {
namespace dht {

class routing_table;
class peer;
class network;

class bucket : 
    public std::list<peer>,
    public std::enable_shared_from_this<std::list<peer>> {
public:
    bucket(routing_table&);
    ~bucket();
    
    void update(peer, bool);

    bool closer(const bucket&, hash_t);

    u64 last_seen;
    std::size_t max_size;
    routing_table& table;
};

}
}

#endif