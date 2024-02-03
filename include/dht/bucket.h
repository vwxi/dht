#ifndef _BUCKET_H
#define _BUCKET_H

#include "util.hpp"
#include "proto.h"

namespace lotus {
namespace dht {

class routing_table;
class network;

class bucket : public std::list<routing_table_entry> {
public:
    bucket(std::shared_ptr<routing_table>);
    
    void update(net_peer, bool);
    void update_near_entry(net_peer);
    void update_far_entry(net_peer);
    void add_new(net_peer);

    void responded(net_peer);
    void stale(net_peer);

    void update_cache(net_peer);

    u64 last_seen;
    std::size_t max_size;
    std::shared_ptr<routing_table> table;

    std::list<net_peer> cache;
    std::mutex cache_mutex;
};

}
}

#endif