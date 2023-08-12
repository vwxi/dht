#ifndef _DHT_H
#define _DHT_H

#include "util.hpp"
#include "proto.h"
#include "bucket.h"
#include "routing.h"
#include "network.h"

namespace tulip {
namespace dht {

// callback types
using basic_callback = std::function<void(peer)>;
using bucket_callback = std::function<void(peer, bucket)>;
using find_value_callback = std::function<void(peer, boost::variant<std::string, bucket>)>;

class node {
public:
    node(u16);

    // async interfaces
    void ping(peer, basic_callback, basic_callback);
    void store(peer, std::string, std::string, basic_callback, basic_callback);
    void find_node(peer, hash_t, bucket_callback, basic_callback);
    void find_value(peer, hash_t, find_value_callback, basic_callback);

    void lookup(hash_t, bucket_callback);
    
private:
    basic_callback basic_nothing = [](peer) { };
    
    void handle_ping(peer, proto::message);
    void handle_store(peer, proto::message);
    void handle_find_node(peer, proto::message);
    void handle_find_value(peer, proto::message);

    hash_t id;

    network net;
    routing_table table;

    std::mutex ht_mutex;
    std::unordered_map<hash_t, std::string> ht;

    std::random_device rd;
    std::default_random_engine reng;
};

}
}

#endif