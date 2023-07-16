#ifndef _DHT_H
#define _DHT_H

#include "util.hpp"
#include "proto.h"
#include "bucket.h"

#include "routing.h"
#include "network.h"
#include "spdlog/spdlog.h"

namespace tulip {

using c_callback = std::function<void(dht::peer)>; // empty client callback
using bkt_callback = std::function<void(dht::peer, dht::bucket)>; // client callback returns bucket & peer

/// @brief accessible interface
class node : dht::node {
public:
    node();
    node(u16, u16);

    void do_nothing(dht::peer);

    void ping(dht::peer, c_callback, c_callback);
    void find_node(dht::peer, dht::hash_t, bkt_callback, c_callback);

    dht::bucket lookup(std::mutex&, std::mutex&, std::mutex&, std::list<dht::peer>&, dht::bucket&, dht::hash_t);
    dht::bucket lookup(dht::hash_t);

    dht::hash_t own_id();

    std::unordered_map<dht::hash_t, std::string> ht;
};

}

#endif