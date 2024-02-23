#ifndef _DHT_H
#define _DHT_H

#include "util.hpp"
#include "proto.h"
#include "bucket.h"
#include "routing.h"
#include "network.h"
#include "crypto.h"

namespace lotus {
namespace dht {

struct kv {
    hash_t key;
    int type;
    std::string value;
    net_peer origin;
    u64 timestamp;
    std::string signature;

    kv() : origin(empty_net_peer), timestamp(0), type(0) { }

    kv(hash_t k, int ty, const std::string& v, const net_peer& o, u64 ts, const std::string& s) : 
        key(k), type(ty), value(v), origin(o), timestamp(ts), signature(s) { } 

    kv(hash_t k, const proto::stored_data& s) : 
        key(k), type(s.t), value(s.v), origin(s.o.to_peer()), timestamp(s.t), signature(s.s) { } 

    std::string sig_blob() const {
        proto::sig_blob sb;
        sb.k = util::enc58(key); // k: key
        sb.d = type; // d: store type
        sb.v = value; // v: value
        sb.i = util::enc58(origin.id); // i: origin ID
        sb.t = timestamp; // t: timestamp

        std::string s = util::serialize(sb);
        return s;
    }
};

class node {
public:
    using basic_callback = std::function<void(net_contact)>;
    using value_callback = std::function<void(std::vector<kv>)>;
    using contacts_callback = std::function<void(std::vector<net_contact>)>;

    basic_callback basic_nothing = [](net_contact) { };

    node(bool, u16);
    ~node();

    hash_t get_id() const;
    
    void run();
    void run(std::string, std::string);
    void generate_keypair();
    void export_keypair(std::string, std::string);

    void put(std::string, std::string, basic_callback, basic_callback);
    void get(std::string, value_callback);
    void provide(std::string, basic_callback, basic_callback);
    void get_providers(std::string, contacts_callback);
    void join(net_addr, basic_callback, basic_callback);
    void resolve(bool, hash_t, basic_callback, basic_callback);
    
private:
    using fv_value = boost::variant<boost::blank, kv, std::list<net_contact>>;
    using bucket_callback = std::function<void(net_contact, std::list<net_contact>)>;
    using find_value_callback = std::function<void(net_contact, fv_value)>;
    using identify_callback = std::function<void(net_peer, std::string)>;
    using addresses_callback = std::function<void(net_contact, std::list<net_peer>)>;
    using fut_t = std::tuple<net_contact, fv_value>;

    struct djc {
        std::mutex mutex;
        std::list<net_contact> shortlist;
    };

    using lkp_t = std::function<fv_value(std::deque<net_contact>, std::shared_ptr<djc>, int)>;

    void _run();

    std::list<node::fv_value> disjoint_lookup_value(hash_t target_id, int Q) {
        std::deque<routing_table_entry> initial = table->find_alpha(target_id);
        std::shared_ptr<djc> claimed = std::make_shared<djc>();
        std::list<fv_value> paths;
        std::list<std::future<fv_value>> tasks;

        int i = 0;

        auto task = [this, target_id] (
            const std::deque<net_contact>& shortlist, std::shared_ptr<djc> claimed, int Q) -> fv_value {
                return lookup_value(shortlist, claimed, target_id, Q);
            };

        // we cant do anything
        if(initial.size() < proto::disjoint_paths)
            return paths;

        int num_to_slice = initial.size() / proto::disjoint_paths;

        while(i++ < proto::disjoint_paths) {
            int n = 0;
            std::deque<net_contact> shortlist;

            while(n++ < num_to_slice) {
                shortlist.push_back(net_contact(initial.front()));
                initial.pop_front();
            }

            tasks.push_back(std::async(std::launch::async, task, shortlist, claimed, Q));
        }

        for(auto&& t : tasks) {
            paths.push_back(t.get());
        }
        
        return paths;
    }

    void refresh(tree*);
    void republish(kv);

    // internal functions
    std::future<net_peer> _verify_node(net_peer);
    std::future<fut_t> _lookup(bool, net_contact, hash_t);
    net_contact resolve_peer_in_table(net_peer);
    struct proto::provider_record parse_provider_record(std::string);
    bool validate_provider_record(const struct proto::provider_record&);
    void verify_provider_record(struct proto::provider_record, basic_callback, basic_callback);
    std::list<net_contact> lookup_nodes(std::deque<net_contact>, hash_t);
    fv_value lookup_value(std::deque<net_contact>, boost::optional<std::shared_ptr<djc>>, hash_t, int);

    // async interfaces
    void ping(net_contact, basic_callback, basic_callback);
    void iter_store(int, std::string, std::string, basic_callback, basic_callback);
    std::list<net_contact> iter_find_node(hash_t);
    void iter_find_node_async(hash_t, contacts_callback);
    void identify(net_contact, identify_callback, basic_callback);
    void get_addresses(net_contact, hash_t, addresses_callback, basic_callback);
    void store(bool, net_contact, kv, basic_callback, basic_callback);
    void find_node(net_contact, hash_t, bucket_callback, basic_callback);
    void find_value(net_contact, hash_t, find_value_callback, basic_callback);

    // message handlers
    void handler(net_peer, proto::message);
    void _handler(net_peer, proto::message);
    void handle_ping(net_peer, proto::message);
    void handle_store(net_peer, proto::message);
    void handle_find_node(net_peer, proto::message);
    void handle_find_value(net_peer, proto::message);
    void handle_identify(net_peer, proto::message);
    void handle_get_addresses(net_peer, proto::message);

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
    boost::asio::thread_pool pool;
};

}
}

#endif