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
    int type;
    std::string value;
    peer origin;
    u64 timestamp;
    std::string signature;

    kv() { }

    kv(hash_t k, int ty, std::string v, peer o, u64 ts, std::string s) : 
        key(k), type(ty), value(v), origin(o), timestamp(ts), signature(s) { } 

    kv(hash_t k, const proto::stored_data& s) : 
        key(k), type(s.t), value(s.v), origin(s.o.to_peer()), timestamp(s.t), signature(s.s) { } 

    std::string sig_blob() const {
        proto::sig_blob sb;
        sb.k = util::b58encode_h(key); // k: key
        sb.d = type; // d: store type
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
    using basic_callback = std::function<void(peer)>;
    using value_callback = std::function<void(std::vector<kv>)>;
    using prov_callback = std::function<void(std::vector<peer>)>;
    using get_addresses_callback = std::function<void(peer, const std::vector<peer_record>&)>;

    basic_callback basic_nothing = [](peer) { };

    node(bool, u16);

    hash_t get_id() const;
    
    void run();
    void run(std::string, std::string);
    void export_keypair(std::string, std::string);

    ~node();

    void put(std::string, std::string);
    void get(std::string, value_callback);
    void provide(std::string, peer);
    void get_providers(std::string, prov_callback);
    void get_addresses(peer, hash_t, get_addresses_callback, basic_callback);
    
    void join(peer, basic_callback, basic_callback);
    
private:
    using fv_value = boost::variant<boost::blank, kv, bucket>;
    using bucket_callback = std::function<void(peer, bucket)>;
    using find_value_callback = std::function<void(peer, fv_value)>;
    using identify_callback = std::function<void(peer, std::string)>;
    using fut_t = std::tuple<peer, fv_value>;

    struct djc {
        std::mutex mutex;
        std::list<peer> shortlist;
    };

    using lkp_t = std::function<fv_value(std::deque<peer>, std::shared_ptr<djc>, int)>;

    void _run();

    void ping(peer, basic_callback, basic_callback);
    void iter_store(int, std::string, std::string);
    bucket iter_find_node(hash_t);
    void identify(peer, identify_callback, basic_callback);

    std::future<fut_t> _lookup(bool, peer, hash_t);
    std::future<std::string> _identify(peer);

    bool identify_node(peer);
    
    bucket lookup_nodes(std::deque<peer>, hash_t);
    fv_value lookup_value(std::deque<peer>, boost::optional<std::shared_ptr<djc>>, hash_t, int);

    std::list<node::fv_value> disjoint_lookup_value(hash_t target_id, int Q) {
        std::deque<peer> initial = table->find_alpha(peer(target_id));
        std::shared_ptr<djc> claimed = std::make_shared<djc>();
        std::list<fv_value> paths;
        std::list<std::future<fv_value>> tasks;

        int i = 0;

        auto task = [this, target_id] (
            std::deque<peer> shortlist, std::shared_ptr<djc> claimed, int Q) -> fv_value {
                return lookup_value(shortlist, claimed, target_id, Q);
            };

        // we cant do anything
        if(initial.size() < proto::disjoint_paths)
            return paths;

        int num_to_slice = initial.size() / proto::disjoint_paths;

        while(i++ < proto::disjoint_paths) {
            int n = 0;
            std::deque<peer> shortlist;

            while(n++ < num_to_slice) {
                shortlist.push_back(initial.front());
                initial.pop_front();
            }

            tasks.push_back(std::async(std::launch::async, task, shortlist, claimed, Q));
        }

        for(auto&& t : tasks)
            paths.push_back(t.get());

        return paths;
    }

    void refresh(tree*);
    void republish(kv);

    // async interfaces
    void store(bool, peer, kv, basic_callback, basic_callback);
    void find_node(peer, hash_t, bucket_callback, basic_callback);
    void find_value(peer, hash_t, find_value_callback, basic_callback);

    // message handlers
    void handle_ping(peer, proto::message);
    void handle_store(peer, proto::message);
    void handle_find_node(peer, proto::message);
    void handle_find_value(peer, proto::message);
    void handle_identify(peer, proto::message);
    void handle_get_addresses(peer, proto::message);

    hash_t id;
    
    std::atomic_bool running;

    network net;
    
    std::shared_ptr<routing_table> table;
    std::weak_ptr<routing_table> table_ref;

    std::mutex ht_mutex;
    std::unordered_map<hash_t, kv> ht;

    std::random_device rd;
    hash_reng_t reng;
    token_reng_t treng;

    std::thread refresh_thread;
    std::thread republish_thread;

public:
    pki::crypto crypto;
};

}
}

#endif