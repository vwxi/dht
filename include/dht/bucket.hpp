#ifndef _BUCKET_H
#define _BUCKET_H

#include "util.hpp"
#include "proto.hpp"

namespace lotus {
namespace dht {

template <typename Network, typename Bucket>
class routing_table;

template <typename Network>
class bucket : public std::list<routing_table_entry> {
public:
    explicit bucket(std::shared_ptr<routing_table<Network, bucket>>);
    
    void update(net_peer, bool);
    void add_or_update_near_entry(net_peer);
    void update_far_entry(net_peer);
    void add_new(net_peer);

    void responded(net_peer);
    void stale(net_peer);

    void update_cache(net_peer);

    u64 last_seen;
    std::shared_ptr<routing_table<Network, bucket>> table;

    std::list<net_peer> cache;
    std::mutex cache_mutex;
};

}
}

#endif