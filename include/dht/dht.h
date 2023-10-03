#ifndef _DHT_H
#define _DHT_H

#include "util.hpp"
#include "proto.h"
#include "bucket.h"
#include "routing.h"
#include "network.h"
#include "crypto.h"

namespace tulip {
namespace dht {

struct kv {
    hash_t key;
    std::string value;
    peer origin;
    u64 timestamp;
    std::string signature;
    kv() { }
    kv(hash_t k, const proto::stored_data& s) : 
        key(k), value(s.v), origin(s.o.to_peer()), timestamp(s.t), signature(s.s) { } 
    kv(hash_t k, std::string v, peer o, u64 t, std::string s) : 
        key(k), value(v), origin(o), timestamp(t), signature(s) { } 
    std::string sig_blob() const {
        msgpack::zone z;

        proto::sig_blob sb;
        sb.k = util::b58encode_h(key); // k: key
        sb.v = value; // v: value
        sb.i = util::b58encode_h(origin.id); // i: origin ID
        sb.t = timestamp; // t: timestamp

        std::stringstream ss;
        msgpack::pack(ss, sb);

        return ss.str();
    }
};

class node {
public:
    using fv_value = boost::variant<boost::blank, kv, bucket>;
    using basic_callback = std::function<void(peer)>;
    using bucket_callback = std::function<void(peer, bucket)>;
    using find_value_callback = std::function<void(peer, fv_value)>;
    using pub_key_callback = std::function<void(peer, std::string)>;

    basic_callback basic_nothing = [](peer) { };

    node(u16);

    void run();
    void run(std::string, std::string);
    void export_keypair(std::string, std::string);

    ~node();

    proto::status put(std::string, std::string);
    fv_value get(std::string);
    
    void join(peer, basic_callback, basic_callback);

    void ping(peer, basic_callback, basic_callback);
    void iter_store(std::string, std::string);
    bucket iter_find_node(hash_t);
    fv_value iter_find_value(std::string);
    void pub_key(peer, pub_key_callback, basic_callback);
    
    std::list<fv_value> disjoint_lookup(bool, hash_t);

private:
    using fut_t = std::tuple<peer, fv_value>;

    struct djc {
        std::mutex mutex;
        std::list<peer> shortlist;
    };

    void _run();

    std::future<fut_t> _lookup(bool, peer, hash_t);
    std::future<std::string> _pub_key(peer);
    
    fv_value lookup(bool, std::deque<peer>, boost::optional<std::shared_ptr<djc>>, hash_t);
    fv_value lp_lookup(hash_t, int);

    void refresh(tree*);
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
    void handle_pub_key(peer, proto::message);

    hash_t id;

    std::atomic_bool running;

    network net;
    
    std::shared_ptr<routing_table> table;
    std::weak_ptr<routing_table> table_ref;

    std::mutex ht_mutex;
    std::unordered_map<hash_t, kv> ht;

    std::random_device rd;
    hash_reng_t reng;

    std::thread refresh_thread;
    std::thread republish_thread;

public:
    pki::crypto crypto;
};

}
}

#endif