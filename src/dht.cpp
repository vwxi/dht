#include "dht.h"
#include "proto.h"
#include "util.hpp"

namespace tulip {
namespace dht {

node::node(u16 port) :
    net(port,
        std::bind(&node::handle_ping, this, _1, _2),
        std::bind(&node::handle_store, this, _1, _2),
        std::bind(&node::handle_find_node, this, _1, _2),
        std::bind(&node::handle_find_value, this, _1, _2)),
    reng(rd()) {
    std::srand(std::time(NULL));

    id = util::gen_id(reng);

    table = std::make_shared<routing_table>(id, net);
    table_ref = table;
    table->init();

    spdlog::debug("running DHT node on port {} (id: {})", port, util::htos(id));

    net.run();
    
    refresh_thread = std::thread([&, this]() {
        while(true) {
            std::this_thread::sleep_for(seconds(proto::refresh_interval));
            refresh_tree();
        }
    });
}

node::~node() {
    refresh_thread.join();
}

/// handlers

void node::handle_ping(peer p, proto::message msg) {
    if(msg.m == proto::type::query) {
        net.send(false,
            p, proto::type::response, proto::actions::ping, 
            id, msg.q, msgpack::type::nil_t(),
            net.queue.q_nothing, net.queue.f_nothing);

        table->update(p);
    } else if(msg.m == proto::type::response) {
        net.queue.satisfy(p, msg.q, std::string{});
    }
}

void node::handle_store(peer p, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::store_query_data d;
        msg.d.convert(d);

        hash_t k(util::to_bin(d.k));
        u32 chksum = util::crc32b((u8*)d.v.data());

        int s = proto::status::ok;
        
        try {
            LOCK(ht_mutex);
            ht[k] = kv(k, d.v, d.o.has_value() ? d.o.value().to_peer() : p, util::time_now());
        } catch (std::exception&) { s = proto::status::bad; }

        net.send(false,
            p, proto::type::response, proto::actions::store,
            id, msg.q, proto::store_resp_data { .c = chksum, .s = s },
            net.queue.q_nothing, net.queue.f_nothing);

        table->update(p);
    } else if(msg.m == proto::type::response) {
        proto::store_resp_data d;
        msg.d.convert(d);

        // we don't care about the timestamp

        // find a more elegant way to do this
        std::stringstream ss;
        ss << d.c;

        if(d.s == proto::status::ok)
            net.queue.satisfy(p, msg.q, ss.str());

        table->update(p);
    }
}

void node::handle_find_node(peer p, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::find_query_data d;
        msg.d.convert(d);

        hash_t target_id(util::to_bin(d.t));
        bucket bkt = table->find_bucket(peer(target_id));

        std::vector<proto::peer_object> b;
        for(auto i : bkt)
            b.push_back(proto::peer_object(i.addr, i.port, util::htos(i.id)));

        net.send(false,
            p, proto::type::response, proto::actions::find_node,
            id, msg.q, proto::find_node_resp_data { .b = std::move(b) },
            net.queue.q_nothing, net.queue.f_nothing);

        table->update(p);
    } else if(msg.m == proto::type::response) {
        proto::find_node_resp_data d;
        bucket bkt(table);
        msgpack::object_handle oh;

        msg.d.convert(d);

        std::stringstream ss;

        // hacky, repack
        {
            msgpack::zone z;
            proto::find_node_resp_data d_;
            msgpack::pack(ss, d);
        }

        net.queue.satisfy(p, msg.q, ss.str());
        
        table->update(p);
    }
}

void node::handle_find_value(peer p, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::find_query_data d;
        msg.d.convert(d);

        hash_t target_id(util::to_bin(d.t));

        {
            LOCK(ht_mutex);
            decltype(ht)::iterator it;
            if((it = ht.find(target_id)) != ht.end()) {
                // key exists in hash table
                net.send(false,
                    p, proto::type::response, proto::actions::find_value,
                    id, msg.q, proto::find_value_resp_data{ .v = proto::stored_data{
                        .v = it->second.value,
                        .o = proto::peer_object(it->second.origin),
                        .t = util::time_now()
                    }, .b = boost::none },
                    net.queue.q_nothing, net.queue.f_nothing);
            } else {
                // key does not exist in hash table
                bucket bkt = table->find_bucket(peer(target_id));

                std::vector<proto::peer_object> b;
                for(auto i : bkt)
                    b.push_back(proto::peer_object{i.addr, i.port, util::htos(i.id)});

                net.send(false,
                    p, proto::type::response, proto::actions::find_value,
                    id, msg.q, proto::find_value_resp_data { .v = boost::none, .b = std::move(b) },
                    net.queue.q_nothing, net.queue.f_nothing);
            }
        }

        table->update(p);
    } else if(msg.m == proto::type::response) {
        proto::find_value_resp_data d;
        msg.d.convert(d);

        std::stringstream ss;

        // hacky, repack
        {
            msgpack::zone z;
            proto::find_node_resp_data d_;
            msgpack::pack(ss, d);
        }

        net.queue.satisfy(p, msg.q, ss.str());

        table->update(p);
    }
}

/// async actions

void node::ping(peer p, basic_callback ok, basic_callback bad) {
    net.send(true,
        p, proto::type::query, proto::actions::ping,
        id, util::msg_id(), msgpack::type::nil_t(),
        [this, ok](peer p_, std::string s) { 
            table->update(p_);
            ok(p_); 
        },
        [this, bad](peer p_) {
            table->stale(p_);
            bad(p_); 
        });
}

void node::store(bool origin, peer p, kv val, basic_callback ok, basic_callback bad) {
    u32 chksum = util::crc32b((u8*)val.value.data());

    boost::optional<proto::peer_object> po = origin ? 
        boost::optional<proto::peer_object>(boost::none) : 
        boost::optional<proto::peer_object>(proto::peer_object(val.origin));

    net.send(true,
        p, proto::type::query, proto::actions::store,
        id, util::msg_id(), proto::store_query_data{ .k = util::htos(val.key), .v = val.value, .o = po },
        [this, ok, bad, chksum](peer p_, std::string s) { 
            u32 csum;
            std::stringstream ss;

            ss << s;
            ss >> csum;

            // check if checksum is valid
            if(csum == chksum)
                ok(p_);
            else
                bad(p_);
        },
        [this, bad](peer p_) { 
            bad(p_); 
        });
}

void node::find_node(peer p, hash_t target_id, bucket_callback ok, basic_callback bad) {
    net.send(true,
        p, proto::type::query, proto::actions::find_node,
        id, util::msg_id(), proto::find_query_data { .t = util::htos(target_id) },
        [this, ok, bad](peer p_, std::string s) {
            msgpack::object_handle oh;
            msgpack::unpack(oh, s.data(), s.size());
            msgpack::object obj = oh.get();
            proto::find_node_resp_data b;
            obj.convert(b);

            bucket bkt(table);

            for(auto i : b.b)
                bkt.push_back(peer(i.a, i.p, hash_t(util::to_bin(i.i))));

            ok(p_, std::move(bkt));
        },
        [this, bad](peer p_) {
            bad(p_);
        });
}

void node::find_value(peer p, hash_t target_id, find_value_callback ok, basic_callback bad) {
    net.send(true,
        p, proto::type::query, proto::actions::find_value,
        id, util::msg_id(), proto::find_query_data { .t = util::htos(target_id) },
        [this, ok, bad, target_id](peer p_, std::string s) {
            msgpack::object_handle oh;
            msgpack::unpack(oh, s.data(), s.size());
            msgpack::object obj = oh.get();
            proto::find_value_resp_data d;
            obj.convert(d);

            if(!d.v.has_value() != !d.b.has_value()) {
                if(d.v.has_value()) {
                    ok(p_, kv(target_id, d.v.value()));
                } else if(d.b.has_value()) {
                    bucket bkt(table);

                    for(auto i : d.b.value())
                        bkt.push_back(peer(i.a, i.p, hash_t(util::to_bin(i.i))));

                    ok(p_, std::move(bkt));
                }
            } else {
                bad(p_);
            }
        },
        [this, bad](peer p_) {
            bad(p_);
        });
}

void node::find_value(peer p, std::string key, find_value_callback ok, basic_callback bad) {
    find_value(p, util::sha1(key), ok, bad);
}

std::future<node::fut_t> node::_find_wrapper(bool fv, peer p, hash_t target_id) {
    std::shared_ptr<std::promise<fut_t>> prom = std::make_shared<std::promise<fut_t>>();
    std::future<fut_t> fut = prom->get_future();

    if(fv) {
        find_value(p, target_id,
            [&, prom](peer p_, fv_value v) { prom->set_value(fut_t{std::move(p_), std::move(v)}); }, 
            [&, prom](peer p_) { prom->set_value(fut_t{std::move(p_), fv_value{boost::blank()}}); });
    } else {
        find_node(p, target_id, 
            [&, prom](peer p_, bucket v) { prom->set_value(fut_t{std::move(p_), std::move(v)}); },
            [&, prom](peer p_) { prom->set_value(fut_t{std::move(p_), fv_value{boost::blank()}}); });        
    }

    return fut;
}

// synchronous operation
// see xlattice/kademlia lookup
fv_value node::lookup(bool fv, std::string fv_key, hash_t target_id) {
    std::list<peer> visited;
    std::deque<peer> shortlist = table->find_alpha(peer(target_id));
    bucket res(table);

    peer closest_node = *std::min_element(shortlist.begin(), shortlist.end(), 
        [target_id](peer a, peer b) { return (a.id ^ target_id) < (b.id ^ target_id); });

    auto filter = [&, this](peer a) { return std::count(visited.begin(), visited.end(), a) != 0 || a.id == id; };

    while(!shortlist.empty()) {
        std::list<std::future<fut_t>> tasks;

        // send out alpha RPCs
        int n = 0;
        for(peer contact : shortlist) {
            if(n++ < proto::alpha) {
                auto i = shortlist.front();
                tasks.push_back(_find_wrapper(fv, shortlist.front(), target_id));
                shortlist.pop_front();
            } else break;
        }

        // get back replies
        for(auto&& t : tasks) {
            fut_t f = t.get();
            peer p = std::get<0>(f);
            fv_value v = std::get<1>(f);

            visited.push_back(p);

            // res is a list of all successfully contacted peers, shortlist is just a queue
            if(v.type() != typeid(boost::none))
                res.push_back(p);

            if(v.type() == typeid(bucket)) {
                // The node then fills the shortlist with contacts from the replies received.
                for(peer p_ : boost::get<bucket>(v)) {
                    if(!filter(p_))
                        shortlist.push_back(p_);
                }
            } else if(v.type() == typeid(kv) && fv) {
                // When an iterativeFindValue succeeds, the initiator must store the key/value pair at the 
                // closest node seen which did not return the value. (xlattice/kademlia)
                kv vl = boost::get<kv>(v);
                store(false, closest_node, vl, basic_nothing, basic_nothing);

                return v;
            }
        }

        peer candidate = *std::min_element(shortlist.begin(), shortlist.end(), 
            [target_id](peer a, peer b) { return (a.id ^ target_id) < (b.id ^ target_id); });

        if((candidate.id ^ target_id) < (closest_node.id ^ target_id)) {
            closest_node = candidate;
        } else break;
    }

    res.remove_if([this](peer a) { return a.id == id; });
    res.sort([&](peer a, peer b) { return (a.id ^ target_id) < (b.id ^ target_id); });

    if(res.size() > proto::bucket_size)
        res.resize(proto::bucket_size);

    return res;
}

// this is for a new key-value pair
void node::iter_store(std::string key, std::string value) {
    hash_t hash = util::sha1(key);
    bucket b = iter_find_node(hash);
    // ignores the peer object anyways
    kv vl(hash, value, peer(), util::time_now());

    for(auto i : b)
        store(true, i, vl, basic_nothing, basic_nothing);
}

// this is for republishing
void node::republish(kv val) {
    bucket b = iter_find_node(val.key);

    for(auto i : b)
        store(true, i, val, basic_nothing, basic_nothing);
}

bucket node::iter_find_node(hash_t target_id) {
    fv_value v = lookup(false, std::string{}, target_id);
    bucket b(table);
    
    bucket& g = boost::get<bucket>(v);
    for(auto i : g)
        b.push_back(i);

    return b;
}

fv_value node::iter_find_value(std::string key) {
    return lookup(true, key, util::sha1(key));
}

void node::refresh_prefix(hash_t prefix) {
    tree* ptr = table->root;
    assert(ptr != nullptr);

    int i = 0;
    table->traverse(true, prefix, &ptr, i);

    refresh(ptr);
}

void node::refresh(tree* ptr) {
    if(ptr == nullptr) return;
    if(!ptr->leaf) return;
    
    hash_t randomness = util::gen_id(reng);
    hash_t mask = ~hash_t(0) << (proto::bit_hash_width - ptr->prefix.cutoff);
    hash_t random_id = ptr->prefix.prefix | (randomness & ~mask);
    bucket bkt = iter_find_node(random_id);

    if(!bkt.empty()) {
        W_LOCK(table->mutex);
        ptr->data = bkt;
        spdlog::debug("refreshed bucket {}, sz: {}", ptr->prefix.prefix.to_string(), ptr->data.size());
    }
}

void node::refresh_tree() {
    table->dfs([&, this](tree* ptr) {
        auto time_since = TIME_NOW() - ptr->data.last_seen;
        if(time_since > proto::refresh_time)
            refresh(ptr);
    });
}

/// @todo resolve to get our own IP?
void node::join(peer p_, basic_callback ok, basic_callback bad) {
    // add peer to routing table
    ping(p_, [&, this](peer p) {
        // lookup our own id
        bucket bkt = iter_find_node(id);

        // populate routing table
        for(auto i : bkt) {
            table->update(i);
        }

        // it refreshes all buckets further away than its closest neighbor, 
        // which will be in the occupied bucket with the lowest index.
        table->dfs([&, this](tree* ptr) {
            hash_t mask(~hash_t(0) << (proto::bit_hash_width - ptr->prefix.cutoff));
            if((p.id & mask) != ptr->prefix.prefix)
                refresh(ptr);
        });

        ok(p);
    }, bad);
}

}
}