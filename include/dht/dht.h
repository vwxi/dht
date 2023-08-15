#ifndef _DHT_H
#define _DHT_H

#include "util.hpp"
#include "proto.h"
#include "bucket.h"
#include "routing.h"
#include "network.h"
#include <boost/variant/static_visitor.hpp>

namespace tulip {
namespace dht {

using fv_value = boost::variant<boost::blank, std::string, bucket>;

// callback types
using basic_callback = std::function<void(peer)>;
using bucket_callback = std::function<void(peer, bucket)>;
using find_value_callback = std::function<void(peer, fv_value)>;

class node {
public:
    node(u16);
    ~node() = default;

    // async interfaces
    void ping(peer, basic_callback, basic_callback);
    void store(peer, std::string, std::string, basic_callback, basic_callback);
    void find_node(peer, hash_t, bucket_callback, basic_callback);
    void find_value(peer, hash_t, find_value_callback, basic_callback);
    void find_value(peer, std::string, find_value_callback, basic_callback);

    fv_value lookup(bool, hash_t);

    void iter_store(std::string, std::string);
    bucket iter_find_node(hash_t);
    boost::optional<std::string> iter_find_value(std::string);
    
private:
    basic_callback basic_nothing = [](peer) { };
    
    void handle_ping(peer, proto::message);
    void handle_store(peer, proto::message);
    void handle_find_node(peer, proto::message);
    void handle_find_value(peer, proto::message);

    hash_t id;

    network net;
    
    std::shared_ptr<routing_table> table;
    std::weak_ptr<routing_table> table_ref;

    std::mutex ht_mutex;
    std::unordered_map<hash_t, std::string> ht;

    std::random_device rd;
    std::default_random_engine reng;
};

}
}

#endif