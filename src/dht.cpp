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

        hash_t k(util::sha1(d.k));
        u32 chksum = util::crc32b((u8*)d.v.data());

        int s = proto::status::ok;

        try {
            LOCK(ht_mutex);
            ht[k] = d.v;
        } catch (std::exception&) { s = proto::status::bad; }

        net.send(false,
            p, proto::type::response, proto::actions::store,
            id, msg.q, proto::store_resp_data { .c = chksum, .s = s },
            net.queue.q_nothing, net.queue.f_nothing);

        table->update(p);
    } else if(msg.m == proto::type::response) {
        proto::store_resp_data d;
        msg.d.convert(d);

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

        std::vector<proto::bucket_peer> b;
        for(auto i : bkt)
            b.push_back(proto::bucket_peer{i.addr, i.port, util::htos(i.id)});

        net.send(false,
            p, proto::type::response, proto::actions::find_node,
            id, msg.q, proto::find_node_resp_data { .b = std::move(b) },
            net.queue.q_nothing, net.queue.f_nothing);

        table->update(p);
    } else if(msg.m == proto::type::response) {
        proto::find_node_resp_data d;
        bucket bkt(*table);
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
                    id, msg.q, proto::find_value_resp_data { .v = it->second, .b = boost::none },
                    net.queue.q_nothing, net.queue.f_nothing);
            } else {
                // key does not exist in hash table
                bucket bkt = table->find_bucket(peer(target_id));

                std::vector<proto::bucket_peer> b;
                for(auto i : bkt)
                    b.push_back(proto::bucket_peer{i.addr, i.port, util::htos(i.id)});

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

void node::store(peer p, std::string key, std::string value, basic_callback ok, basic_callback bad) {
    u32 chksum = util::crc32b((u8*)value.data());

    net.send(true,
        p, proto::type::query, proto::actions::store,
        id, util::msg_id(), proto::store_query_data{ .k = key, .v = value },
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

            bucket bkt(*table);

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
        [this, ok, bad](peer p_, std::string s) {
            msgpack::object_handle oh;
            msgpack::unpack(oh, s.data(), s.size());
            msgpack::object obj = oh.get();
            proto::find_value_resp_data d;
            obj.convert(d);

            if((d.v == boost::none) != (d.b == boost::none)) {
                if(d.v != boost::none) {
                    ok(p_, d.v.value());
                } else if(d.b != boost::none) {
                    bucket bkt(*table);

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

fv_value node::lookup(bool fv, std::string fv_key, hash_t target_id) {
    std::list<peer> visited;
    bucket closest = table->find_bucket(peer(target_id));

    while(true) {
        if(closest.empty())
            return closest;

        using fut_t = std::tuple<peer, fv_value>;

        std::list<std::future<fut_t>> tasks;
        std::list<fut_t> responses;
        bucket candidate(*table);

        // start queries
        for(peer p : closest) {
            visited.push_back(p);

            tasks.push_back(std::async(
                std::launch::async,
                [&](peer p_) -> fut_t {
                    std::promise<fut_t> prom;
                    std::future<fut_t> fut = prom.get_future();

                    if(fv) {
                        find_value(p_, target_id,
                            [&, pr = std::make_shared<std::promise<fut_t>>(std::move(prom))](peer p__, fv_value v) {
                                pr->set_value(fut_t{std::move(p__), std::move(v)});
                            }, net.queue.f_nothing);
                    } else {
                        find_node(p_, target_id,
                            [&, pr = std::make_shared<std::promise<fut_t>>(std::move(prom))](peer p__, bucket v) {
                                pr->set_value(fut_t{std::move(p__), std::move(v)});
                            }, net.queue.f_nothing);
                    }

                    switch(fut.wait_for(seconds(proto::net_timeout))) {
                    case std::future_status::ready: return fut.get();
                    default: return fut_t{p_, fv_value(boost::blank{})};
                    }
                },
                p
            ));
        }

        // wait for responses
        for(auto&& t : tasks)
            responses.push_back(t.get());

        // remove empty responses
        responses.remove_if([&](fut_t v) { return std::get<1>(v).type() == typeid(boost::blank); });

        // end if we don't have any responses
        if(responses.empty())
            break;

        // check if an actual value came through
        for(auto r : responses) {
            // we got a value, return immediately
            peer pr = std::get<0>(r);
            fv_value v = std::get<1>(r);
            
            if(v.type() == typeid(std::string) && fv) {
                // When an iterativeFindValue succeeds, the initiator must store the key/value pair at the 
                // closest node seen which did not return the value. (xlattice/kademlia)

                std::string r_ = boost::get<std::string>(v);

                std::list<fut_t> sub;
                std::copy_if(responses.begin(), responses.end(), std::back_inserter(sub),
                    [&](const fut_t& f) { return std::get<1>(f).type() != typeid(std::string); });

                std::list<fut_t>::iterator i = std::min_element(sub.begin(), sub.end(),
                    [&](fut_t a, fut_t b) { 
                        return std::get<0>(a).distance(target_id) < std::get<0>(b).distance(target_id); 
                    });

                if(i != sub.end()) {
                    peer cl = std::get<0>(*i);
                    store(cl, fv_key, r_, basic_nothing, basic_nothing);
                }

                return v;
            }
        }
        
        // from here on it's exclusively buckets, look for next candidate bucket
        auto it = std::min_element(responses.begin(), responses.end(),
            [&](fut_t a, fut_t b) { return boost::get<bucket>(std::get<1>(a)).closer(boost::get<bucket>(std::get<1>(b)), target_id); });

        bucket& g = boost::get<bucket>(std::get<1>(*it));
        std::copy(g.begin(), g.end(), std::back_inserter(candidate));

        // remove already visited peers and ourselves
        candidate.remove_if([&](peer a) {
            return std::count(visited.begin(), visited.end(), a) != 0 || a.id == id;
        });

        // end if we don't have a candidate anymore
        if(candidate.empty())
            break;

        // if we got a closer candidate, make candidate the closest and iterate 
        if(candidate.closer(closest, target_id)) {
            closest.clear();
            std::copy(candidate.cbegin(), candidate.cend(), std::back_inserter(closest));
        } else break;
    }

    return closest;
}

void node::iter_store(std::string key, std::string value) {
    bucket b = iter_find_node(util::sha1(key));

    for(auto i : b) {
        spdlog::info("storing at {}:{} id {}", i.addr, i.port, util::htos(i.id));
        store(i, key, value, basic_nothing, basic_nothing);
    }
}

bucket node::iter_find_node(hash_t target_id) {
    fv_value v = lookup(false, std::string{}, target_id);
    bucket b(*table);
    
    bucket& g = boost::get<bucket>(v);
    for(auto i : g)
        b.push_back(i);

    return b;
}

fv_value node::iter_find_value(std::string key) {
    return lookup(true, key, util::sha1(key));
}

}
}