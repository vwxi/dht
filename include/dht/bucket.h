#ifndef _BUCKET_H
#define _BUCKET_H

#include "util.hpp"

namespace tulip {
namespace dht {

class routing_table;
class peer;
class network;

class bucket : public std::list<peer> {
public:
    bucket(std::shared_ptr<routing_table>);
    
    void update(peer, bool);

    bool closer(const bucket&, hash_t);

    u64 last_seen;
    std::size_t max_size;
    std::shared_ptr<routing_table> table;
};

}
}

#endif