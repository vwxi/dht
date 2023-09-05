#ifndef _DHT_H
#define _DHT_H

#include "util.hpp"
#include "proto.h"
#include "bucket.h"
#include "routing.h"
#include "network.h"

namespace tulip {
namespace dht {

struct kv {
    hash_t key;
    std::string value;
    peer origin;
    u64 timestamp;
    kv() { }
    kv(hash_t k, const proto::stored_data& s) : key(k), value(s.v), origin(s.o.to_peer()), timestamp(s.t) { } 
    kv(hash_t k, std::string v, peer o, u64 t) : key(k), value(v), origin(o), timestamp(t) { } 
};

using fv_value = boost::variant<boost::blank, kv, bucket>;

// callback types
using basic_callback = std::function<void(peer)>;
using bucket_callback = std::function<void(peer, bucket)>;
using find_value_callback = std::function<void(peer, fv_value)>;

class node {
public:
    node(u16);
    ~node();

    proto::status put(std::string, std::string);
    fv_value get(std::string);
    
    void join(peer, basic_callback, basic_callback);

    void ping(peer, basic_callback, basic_callback);
    void iter_store(std::string, std::string);
    bucket iter_find_node(hash_t);
    fv_value iter_find_value(std::string);
    
private:
    using fut_t = std::tuple<peer, fv_value>;

    basic_callback basic_nothing = [](peer) { };
    
    std::future<fut_t> _find_wrapper(bool, peer, hash_t);
    fv_value lookup(bool, std::string, hash_t);

    void refresh_prefix(hash_t);
    void refresh(tree*);
    void refresh_tree();

    void republish(kv);
    
    // async interfaces
    void store(bool, peer, kv, basic_callback, basic_callback);
    void find_node(peer, hash_t, bucket_callback, basic_callback);
    void find_value(peer, hash_t, find_value_callback, basic_callback);
    void find_value(peer, std::string, find_value_callback, basic_callback);

    void handle_ping(peer, proto::message);
    void handle_store(peer, proto::message);
    void handle_find_node(peer, proto::message);
    void handle_find_value(peer, proto::message);

    hash_t id;

    network net;
    
    std::shared_ptr<routing_table> table;
    std::weak_ptr<routing_table> table_ref;

    std::mutex ht_mutex;
    std::unordered_map<hash_t, kv> ht;

    std::random_device rd;
    std::default_random_engine reng;

    std::thread refresh_thread;
};

}
}

#endif