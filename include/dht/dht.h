#ifndef _DHT_H
#define _DHT_H

#include "util.hpp"
#include "proto.h"
#include "bucket.h"

#include "routing.h"
#include "network.h"
#include "spdlog/spdlog.h"

namespace tulip {

// client callback
using c_callback = std::function<void(dht::peer)>;
// client callback (bucket)
using bkt_callback = std::function<void(dht::peer, dht::bucket)>;

/// @brief accessible interface
class node : dht::node {
public:
    node();
    node(u16, u16);

    void do_nothing(dht::peer);

    void ping(dht::peer, c_callback, c_callback);
    void find_node(dht::peer, dht::hash_t, bkt_callback, c_callback);

    dht::hash_t own_id();
};

}

#endif