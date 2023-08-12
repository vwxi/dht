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
    table(id, net),
    reng(rd()) {
    std::srand(std::time(NULL));
    id = table.id = util::gen_id(reng);
    net.run();
}

/// handlers

void node::handle_ping(peer p, proto::message msg) {
    if(msg.m == proto::type::query) {
        net.send(false,
            p, proto::type::response, proto::actions::ping, 
            id, msg.q, msgpack::type::nil_t(),
            net.queue.q_nothing, net.queue.f_nothing);

        table.update(p);
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

        spdlog::debug("store key {} with val {}", util::htos(k), d.v);
        
        int s = proto::status::ok;

        try {
            LOCK(ht_mutex);
            ht[k] = d.v;
        } catch (std::exception&) { s = proto::status::bad; }

        net.send(false,
            p, proto::type::response, proto::actions::store,
            id, msg.q, proto::store_resp_data { .c = chksum, .s = s },
            net.queue.q_nothing, net.queue.f_nothing);

        table.update(p);
    } else if(msg.m == proto::type::response) {
        proto::store_resp_data d;
        msg.d.convert(d);

        // find a more elegant way to do this
        std::stringstream ss;
        ss << d.c;

        if(d.s == proto::status::ok)
            net.queue.satisfy(p, msg.q, ss.str());

        table.update(p);
    }
}

void node::handle_find_node(peer p, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::find_query_data d;
        msg.d.convert(d);

        hash_t target_id(util::to_bin(d.t));
        bucket bkt = table.find_bucket(peer(target_id));

        std::vector<proto::bucket_peer> b;
        for(auto i : bkt)
            b.push_back(proto::bucket_peer{i.addr, i.port, util::htos(i.id)});

        net.send(false,
            p, proto::type::response, proto::actions::find_node,
            id, msg.q, proto::find_node_resp_data { .b = std::move(b) },
            net.queue.q_nothing, net.queue.f_nothing);

        table.update(p);
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
        
        table.update(p);
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
                bucket bkt = table.find_bucket(peer(target_id));

                std::vector<proto::bucket_peer> b;
                for(auto i : bkt)
                    b.push_back(proto::bucket_peer{i.addr, i.port, util::htos(i.id)});

                net.send(false,
                    p, proto::type::response, proto::actions::find_value,
                    id, msg.q, proto::find_value_resp_data { .v = boost::none, .b = std::move(b) },
                    net.queue.q_nothing, net.queue.f_nothing);
            }
        }

        table.update(p);
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

        table.update(p);
    }
}

/// async actions

void node::ping(peer p, basic_callback ok, basic_callback bad) {
    net.send(true,
        p, proto::type::query, proto::actions::ping,
        id, util::msg_id(), msgpack::type::nil_t(),
        [this, ok](peer p_, std::string) { 
            table.update_pending(p_);
            ok(p_); 
        },
        [this, bad](peer p_) {
            if(table.stale(p_) > proto::missed_pings_allowed)
                table.evict(p_);
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

}
}