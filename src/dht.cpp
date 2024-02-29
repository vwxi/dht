#include "dht.hpp"
#include "proto.hpp"
#include "util.hpp"

namespace lotus {
namespace dht {

template <typename Network, typename Bucket>
node<Network, Bucket>::node(bool local, u16 port) :
    net(local, port, std::bind(&node<Network, Bucket>::handler, this, _1, _2)),
    reng(rd()),
    treng(rd()),
    running(false) {
    std::srand(util::time_now());
}

template <typename Network, typename Bucket>
node<Network, Bucket>::~node() {
    if(running) {
        if(refresh_thread.joinable()) refresh_thread.join();
        if(republish_thread.joinable()) republish_thread.join();
    }
}

template <typename Network, typename Bucket>
hash_t node<Network, Bucket>::get_id() const {
    return id;
} 

/// runners

template <typename Network, typename Bucket>
void node<Network, Bucket>::_run() {
    id = util::hash(crypto.pub_key());

    table = std::make_shared<routing_table<Network, Bucket>>(id, net);
    table_ref = table;
    table->init();

    spdlog::debug("dht: running DHT node on port {} (id: {})", net.port, util::enc58(id));
    
    running = true;

    net.run();
    
    spdlog::debug("dht: ip address: {}", net.get_ip_address());

    refresh_thread = std::thread([&, this]() {
        while(true) {
            std::this_thread::sleep_for(seconds(proto::refresh_interval));
            table->dfs([&, this](tree<Network, Bucket>* ptr) {
                auto time_since = util::time_now() - ptr->data.last_seen;
                if(time_since > proto::refresh_time)
                    refresh(ptr);
            });
        }
    });

    republish_thread = std::thread([&, this]() {
        while(true) {
            std::this_thread::sleep_for(seconds(proto::republish_interval));

            {
                LOCK(ht_mutex);
                for(auto it = ht.begin(); it != ht.end();) {
                    // remove expired provider records
                    try {
                        if(it->second.type == proto::store_type::provider_record) {
                            try {
                                struct proto::provider_record pr = parse_provider_record(it->second.value);
                                if((util::time_now() - pr.e) > proto::republish_time) {
                                    spdlog::debug("dht: removing expired provider record for {}", util::enc58(it->second.key));
                                    it = ht.erase(it);
                                } else it++;
                            } catch(std::exception& e) {
                                it = ht.erase(it);
                            }
                        } else {
                            if(util::time_now() - it->second.timestamp > proto::republish_time) {
                                it->second.timestamp = util::time_now();                            
                                republish(it->second);
                            }
                            
                            it++;
                        }
                    } catch(const std::exception& e) {  }
                }
            }
        }
    });
}

/// @brief generate keypair, start node
template <typename Network, typename Bucket>
void node<Network, Bucket>::run() {
    crypto.generate_keypair();
    _run();
}

/// @brief import keypair from files, start node
template <typename Network, typename Bucket>
void node<Network, Bucket>::run(const std::string& pub_filename, const std::string& priv_filename) {
    crypto.import_file(pub_filename, priv_filename);
    _run();
}

/// keypair stuff

template <typename Network, typename Bucket>
void node<Network, Bucket>::generate_keypair() {
    crypto.generate_keypair();
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::export_keypair(const std::string& pub_filename, const std::string& priv_filename) {
    crypto.export_file(pub_filename, priv_filename);
}

/// handlers

template <typename Network, typename Bucket>
void node<Network, Bucket>::_handler(net_peer peer, proto::message msg) {
    switch(msg.a) {
    case proto::actions::ping: 
        handle_ping(std::move(peer), std::move(msg)); 
        break;
    case proto::actions::store: 
        handle_store(std::move(peer), std::move(msg)); 
        break;
    case proto::actions::find_node: 
        handle_find_node(std::move(peer), std::move(msg)); 
        break;
    case proto::actions::find_value: 
        handle_find_value(std::move(peer), std::move(msg)); 
        break;
    case proto::actions::identify: 
        handle_identify(std::move(peer), std::move(msg)); 
        break;
    case proto::actions::get_addresses:
        handle_get_addresses(std::move(peer), std::move(msg)); 
        break;
    }
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::handler(net_peer peer, proto::message msg) {
    // identify before any query
    if(!crypto.ks_has(peer.id) && 
        msg.a != proto::actions::identify &&
        msg.a != proto::actions::get_addresses) {
        // repack to save lifetime
        std::string msg_s = util::serialize(msg);

        identify(resolve_peer_in_table(peer), 
            [this, msg_s](net_peer peer, std::string) {
                proto::message msg = util::deserialize<proto::message>(msg_s);
                _handler(std::move(peer), msg);
            },
            basic_nothing);
    } else {
        _handler(std::move(peer), msg);
    }
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::handle_ping(net_peer peer, proto::message msg) {
    if(msg.m == proto::type::query) {
        net.send(false,
            peer.addr, proto::type::response, proto::actions::ping, 
            id, msg.q, msgpack::type::nil_t(),
            net.queue.q_nothing, net.queue.f_nothing);
    } else if(msg.m == proto::type::response) {
        net.queue.satisfy(peer, msg.a, msg.q, std::string{});
    }
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::handle_store(net_peer peer, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::store_query_data d;
        msg.d.convert(d);

        hash_t k(util::dec58(d.k));
        u32 chksum = util::crc32b(const_cast<u8*>(reinterpret_cast<const u8*>(d.v.data())));

        // if data is a provider record, check if it is valid
        if(d.d == proto::store_type::provider_record) {
            try {
                struct proto::provider_record pr = parse_provider_record(d.v);
                
                verify_provider_record(pr, 
                    [this, k, d, peer, msg, chksum](net_contact c) {
                        int s = proto::status::ok;
                        try {
                            LOCK(ht_mutex);
                            if(ht.count(k) > 0) {
                                s = proto::status::bad;
                            } else {
                                ht[k] = kv(k, d.d, d.v, d.o.has_value() ? d.o.value().to_peer() : peer, d.t, d.s);
                                spdlog::debug("dht: stored provider record for {}", util::enc58(k));
                            }
                        } catch (std::exception&) { s = proto::status::bad; }

                        net.send(false,
                            peer.addr, proto::type::response, proto::actions::store,
                            id, msg.q, proto::store_resp_data { .c = chksum, .s = s },
                            net.queue.q_nothing, net.queue.f_nothing);
                    }, 
                    [this, k, d, peer, msg, chksum](net_contact c) {
                        net.send(false,
                            peer.addr, proto::type::response, proto::actions::store,
                            id, msg.q, proto::store_resp_data { .c = chksum, .s = proto::status::bad },
                            net.queue.q_nothing, net.queue.f_nothing);
                    });
            } catch(std::exception& e) { 
                net.send(false,
                    peer.addr, proto::type::response, proto::actions::store,
                    id, msg.q, proto::store_resp_data { .c = chksum, .s = proto::status::bad },
                    net.queue.q_nothing, net.queue.f_nothing);
            }
        } else {
            // store as normal
            int s = proto::status::ok;
            try {
                LOCK(ht_mutex);
                if(ht.count(k) > 0) {
                    s = proto::status::bad;
                } else {
                    ht[k] = kv(k, d.d, d.v, d.o.has_value() ? d.o.value().to_peer() : peer, d.t, d.s);
                }
            } catch (std::exception&) { s = proto::status::bad; }

            net.send(false,
                peer.addr, proto::type::response, proto::actions::store,
                id, msg.q, proto::store_resp_data { .c = chksum, .s = s },
                net.queue.q_nothing, net.queue.f_nothing);
        }

        table->update(peer);
    } else if(msg.m == proto::type::response) {
        proto::store_resp_data d;
        msg.d.convert(d);

        // we don't care about the timestamp

        // find a more elegant way to do this
        std::stringstream ss;
        ss << d.c;

        if(d.s == proto::status::ok) {
            net.queue.satisfy(peer, msg.a, msg.q, ss.str());
        }

        table->update(peer);
    }
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::handle_find_node(net_peer peer, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::find_query_data d;
        msg.d.convert(d);

        hash_t target_id(util::dec58(d.t));
        const bucket<Network>& bkt = table->find_bucket(target_id);

        std::vector<proto::peer_object> b;

        for(auto i : bkt) {
            b.emplace_back(
                i.addresses.front().first.transport(), 
                i.addresses.front().first.addr, 
                i.addresses.front().first.port, 
                util::enc58(i.id)
            );
        }

        std::string ser = util::serialize(b);

        net.send(false,
            peer.addr, proto::type::response, proto::actions::find_node,
            id, msg.q, proto::find_node_resp_data{
                .b = std::move(b),
                .s = crypto.sign(ser)
            }, net.queue.q_nothing, net.queue.f_nothing);

        table->update(peer);
    } else if(msg.m == proto::type::response) {
        // hacky, repack
        net.queue.satisfy(peer, msg.a, msg.q, util::serialize(msg.d));
        
        table->update(peer);
    }
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::handle_find_value(net_peer peer, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::find_query_data d;
        msg.d.convert(d);

        hash_t target_id(util::dec58(d.t));

        {
            LOCK(ht_mutex);
            typename decltype(ht)::iterator it;
            if((it = ht.find(target_id)) != ht.end()) {
                // key exists in hash table
                net.send(false,
                    peer.addr, proto::type::response, proto::actions::find_value,
                    id, msg.q, proto::find_value_resp_data{ .v = proto::stored_data{
                        .d = it->second.type,
                        .v = it->second.value,
                        .o = proto::peer_object(it->second.origin),
                        .t = it->second.timestamp,
                        .s = it->second.signature
                    }, .b = boost::none },
                    net.queue.q_nothing, net.queue.f_nothing);
            } else {
                // key does not exist in hash table
                const bucket<Network>& bkt = table->find_bucket(target_id);

                std::vector<proto::peer_object> b;

                /// @note hacky?
                for(auto i : bkt) {
                    for(auto a : i.addresses) {
                        b.emplace_back(
                            a.first.transport(), 
                            a.first.addr, 
                            a.first.port, 
                            util::enc58(target_id)
                        );
                    }
                }

                net.send(false,
                    peer.addr, proto::type::response, proto::actions::find_value,
                    id, msg.q, proto::find_value_resp_data { .v = boost::none, 
                        .b = proto::find_node_resp_data{
                            .b = std::move(b),
                            .s = crypto.sign(util::serialize(b))
                        } 
                    }, net.queue.q_nothing, net.queue.f_nothing);
            }
        }

        table->update(peer);
    } else if(msg.m == proto::type::response) {
        // hacky, repack
        net.queue.satisfy(peer, msg.a, msg.q, util::serialize(msg.d));

        table->update(peer);
    }
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::handle_identify(net_peer peer, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::identify_query_data d;
        msg.d.convert(d);

        net.send(false,
            peer.addr, proto::type::response, proto::actions::identify,
            id, msg.q, proto::identify_resp_data{
                .k = crypto.pub_key(),
                .s = crypto.sign(
                    fmt::format("{}:{}:{}", 
                        d.s, peer.addr.addr, peer.addr.port)) // sign secret token
            },
            net.queue.q_nothing, net.queue.f_nothing);
    } else if(msg.m == proto::type::response) {
        // hacky, repack
        net.queue.satisfy(peer, msg.a, msg.q, util::serialize(msg.d));

        /// @note identify does not update table
    }
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::handle_get_addresses(net_peer peer, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::get_addresses_query_data d;
        msg.d.convert(d);

        std::vector<proto::address_object> addrs;

        hash_t target_id = util::dec58(d.i);
        boost::optional<routing_table_entry> c = table->find(target_id);
        
        if(c.has_value()) {
            for(auto a : c.value().addresses) {
                addrs.emplace_back(a.first);
            }
        } else if(target_id == id) {
            // if requesting own ID, get own peer object
            addrs.emplace_back(net_addr("udp", net.get_ip_address(), net.port));
        }
        
        net.send(false,
            peer.addr, proto::type::response, proto::actions::get_addresses,
            id, msg.q, proto::get_addresses_resp_data{
                .i = d.i,
                .p = addrs
            },
            net.queue.q_nothing, net.queue.f_nothing);
    } else if(msg.m == proto::type::response) {
        // hacky, repack
        net.queue.satisfy(peer, msg.a, msg.q, util::serialize(msg.d));

        /// @note get_addresses does not update table
    }
}

/// public interfaces 

template <typename Network, typename Bucket>
void node<Network, Bucket>::put(const std::string& key, const std::string& value, basic_callback ok, basic_callback bad) {
    iter_store(proto::store_type::data, key, value, ok, bad);
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::get(const std::string& key, value_callback cb) {
    std::list<fv_value> l = disjoint_lookup_value(util::hash(key), proto::quorum);
    std::vector<kv> values;

    for(auto i : l) {
        if(i.type() == typeid(boost::blank) ||
            i.type() == typeid(bucket<Network>)) {
            continue;
        } else if(i.type() == typeid(kv)) {
            kv v = boost::get<kv>(i);
            if(std::count_if(values.begin(), values.end(), [&](const kv& val) {
                // this works i guess
                return val.sig_blob() == v.sig_blob();
            }) == 0)
                values.push_back(v);
        }
    }

    cb(std::move(values));
}

// always wrap in try-catch block
template <typename Network, typename Bucket>
struct proto::provider_record node<Network, Bucket>::parse_provider_record(const std::string& s) {
    struct proto::provider_record pr = util::deserialize<struct proto::provider_record>(s);
    return pr;
}

// check:
// - expiry date
// - signature
template <typename Network, typename Bucket>
bool node<Network, Bucket>::validate_provider_record(const struct proto::provider_record& pr) {
    try {
        hash_t p_id = util::dec58(pr.i);

        // record is older than republish_time seconds
        int time = (util::time_now() - pr.e);
        if(time > proto::republish_time) {
            return false;
        }

        // record has invalid signature
        std::string blob = fmt::format("{}:{}", pr.i, pr.e);
        if(!crypto.verify(p_id, blob, pr.s)) {
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::verify_provider_record(struct proto::provider_record pr, basic_callback ok, basic_callback bad) {
    resolve(false, util::dec58(pr.i), [this, pr, ok, bad](net_contact c) {
        // we found addresses
        if(crypto.ks_has(c.id)) {
            if(validate_provider_record(pr)) {
                spdlog::debug("dht: {} is a valid provider!", util::enc58(c.id));

                // we got a valid provider record
                ok(c);
            } else {
                bad(c);
            }
        } else {
            identify(c, [this, c, pr, ok, bad](net_peer, std::string) {
                // we got a public key
                if(validate_provider_record(pr)) {
                    // we got a valid provider record
                    spdlog::debug("dht: {} is a valid provider!", util::enc58(c.id));

                    ok(c);
                } else {
                    // don't keep the public key
                    crypto.ks_del(c.id);
                    bad(c);
                }
            }, [this, bad](net_contact c) { bad(c); });
        }
    }, [this, bad](net_contact c) { bad(c); });
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::provide(const std::string& key, basic_callback ok, basic_callback bad) {
    struct proto::provider_record pr{
        util::enc58(id), 
        util::time_now() + proto::republish_time, 
        crypto.sign(fmt::format("{}:{}", util::enc58(id), util::time_now() + proto::republish_time))
    };

    iter_store(proto::store_type::provider_record, key, util::serialize(pr), ok, bad);
} 

/// @note there is a design quirk that only allows for one provider per ID due to the nature of a key-value map
template <typename Network, typename Bucket>
void node<Network, Bucket>::get_providers(const std::string& key, contacts_callback cb) {
    get(key, [this, cb](std::vector<kv> values) {
        values.erase(std::remove_if(values.begin(), values.end(), [](const kv& p) { 
            return p.type != proto::store_type::provider_record;
        }), values.end());

        std::vector<net_contact> provs;
 
        for(auto v : values) {
            try {
                // gets should always have an origin to resolve from
                struct proto::provider_record pr = util::deserialize<struct proto::provider_record>(v.value);
                provs.push_back(resolve_peer_in_table(v.origin));
            } catch (std::exception&) { }
        }

        cb(provs);
    });
}

/// async actions

template <typename Network, typename Bucket>
void node<Network, Bucket>::ping(net_contact contact, basic_callback ok, basic_callback bad) {
    net.send(true,
        contact.addresses, proto::type::query, proto::actions::ping,
        id, util::msg_id(), msgpack::type::nil_t(),
        [this, ok](net_peer p_, const std::string& s) { 
            table->update(p_);
            ok(net_contact(p_)); 
        },
        [this, bad](net_peer p_) {
            table->stale(p_);
            bad(net_contact(p_)); 
        });
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::store(bool origin, net_contact p, kv val, basic_callback ok, basic_callback bad) {
    u32 chksum = util::crc32b(const_cast<u8*>(reinterpret_cast<const u8*>(val.value.data())));
    
    // hacky
    val.origin.id = id;

    boost::optional<proto::peer_object> po = origin ? 
        boost::optional<proto::peer_object>(boost::none) : 
        boost::optional<proto::peer_object>(proto::peer_object(val.origin));

    // store in local store
    {
        LOCK(ht_mutex);
        ht[val.key] = val;
    }

    net.send(true,
        p.addresses, proto::type::query, proto::actions::store,
        id, util::msg_id(), proto::store_query_data{ 
            .k = util::enc58(val.key), 
            .d = val.type,
            .v = val.value, 
            .o = po,
            .t = val.timestamp,
            .s = origin ? crypto.sign(val.sig_blob()) : val.signature },
        [this, ok, bad, chksum](net_peer p_, const std::string& s) { 
            net_contact c = resolve_peer_in_table(p_);

            u32 csum;
            std::stringstream ss;

            ss << s;
            ss >> csum;

            // check if checksum is valid
            if(csum == chksum)
                ok(c);
            else
                bad(c);
        },
        [this, bad](net_peer p_) {
            table->stale(p_);
            bad(net_contact(p_));
        });
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::find_node(net_contact p, hash_t target_id, bucket_callback ok, basic_callback bad) {
    net.send(true,
        p.addresses, proto::type::query, proto::actions::find_node,
        id, util::msg_id(), proto::find_query_data { .t = util::enc58(target_id) },
        [this, ok, bad](net_peer p_, std::string s) {
            net_contact c = resolve_peer_in_table(p_);
            proto::find_node_resp_data b = util::deserialize<proto::find_node_resp_data>(s);

            std::list<net_contact> l;

            for(auto i : b.b) {
                l.emplace_back(i.to_peer());
            }

            // verify signature
            std::vector<proto::peer_object> po;
            for(auto i : l)
                po.emplace_back(
                    i.addresses.front().transport(), 
                    i.addresses.front().addr, 
                    i.addresses.front().port, 
                    util::enc58(i.id)
                );

            std::string ser = util::serialize(po);
    
            if(crypto.verify(c.id, ser, b.s))
                ok(c, std::move(l));
            else {
                bad(c);
            }
        },
        [this, bad](net_peer p_) {
            table->stale(p_);
            bad(net_contact(p_));
        });
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::find_value(net_contact p, hash_t target_id, find_value_callback ok, basic_callback bad) {
    net.send(true,
        p.addresses, proto::type::query, proto::actions::find_value,
        id, util::msg_id(), proto::find_query_data { .t = util::enc58(target_id) },
        [this, ok, bad, target_id](net_peer p_, std::string s) {
            net_contact c = resolve_peer_in_table(p_);
            proto::find_value_resp_data d = util::deserialize<proto::find_value_resp_data>(s);

            if(d.v.has_value() != d.b.has_value()) {
                if(d.v.has_value()) {
                    proto::stored_data sd = d.v.value();
                    kv val(target_id, sd.d, sd.v, sd.o.to_peer(), sd.t, sd.s);

                    // check if provider record is valid
                    if(val.type == proto::store_type::provider_record) {
                        try {
                            struct proto::provider_record pr = parse_provider_record(val.value);
                            verify_provider_record(pr, 
                                [this, ok, c, val](net_contact) {
                                    ok(c, val);
                                }, 
                                [this, bad, c](net_contact) {
                                    bad(c);
                                });
                        } catch (std::exception& e) {
                            bad(c);
                        }
                    } else {
                        // callback as normal
                        if(crypto.verify(c.id, val.sig_blob(), val.signature)) {
                            ok(c, val);
                        } else {
                            bad(c);
                        }
                    }
                } else if(d.b.has_value()) {
                    std::list<net_contact> l;

                    for(auto i : d.b.value().b)
                        l.emplace_back(net_peer(util::dec58(i.i), net_addr(i.t, i.a, i.p)));

                    // verify signature
                    std::vector<proto::peer_object> po;
                    for(auto i : l) {
                        po.emplace_back(
                            i.addresses.front().transport(), 
                            i.addresses.front().addr, 
                            i.addresses.front().port, 
                            util::enc58(i.id)
                        );
                    }

                    std::string ser = util::serialize(po);
                    if(crypto.verify(c.id, ser, d.b.value().s)) {
                        ok(c, std::move(l));
                    } else {
                        bad(c);
                    }
                }
            } else {
                bad(c);
            }
        },
        [this, bad](net_peer p_) {
            table->stale(p_);
            bad(net_contact(p_));
        });
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::identify(net_contact contact, identify_callback ok, basic_callback bad) {
    std::string token = util::gen_token(treng);

    if(crypto.ks_has(contact.id)) {
        auto key = crypto.ks_get(contact.id);
        if(key.has_value() && !contact.addresses.empty()) {
            ok(net_peer(contact.id, contact.addresses.front()), crypto.pub_key(key.value()));
        } else {
            bad(contact);
        }

        return;
    }

    net.send(true,
        contact.addresses, proto::type::query, proto::actions::identify,
        id, util::msg_id(), proto::identify_query_data {
            .s = token
        },
        [this, ok, bad, token](net_peer p_, std::string s) {
            proto::identify_resp_data d = util::deserialize<proto::identify_resp_data>(s);

            if(p_.id != util::hash(d.k)) {
                // if peer id isn't hash(pkey), it's bad
                bad(net_contact(p_));
                return;
            }

            // put into local keystore, will be removed if following verify fails
            crypto.ks_put(p_.id, d.k);

            // verify if signature for token is correct
            std::string blob = fmt::format("{}:{}:{}", token, net.get_ip_address(), net.port);
            if(crypto.verify(p_.id, blob, d.s)) {
                spdlog::debug("dht: identified {}", util::enc58(p_.id));
                ok(p_, d.k);
            } else {
                bad(net_contact(p_));
            }
        },
        [this, bad](net_peer p_) {
            bad(net_contact(p_));
        });
}

template <typename Network, typename Bucket>
std::future<net_peer> node<Network, Bucket>::_verify_node(net_peer peer) {
    std::shared_ptr<std::promise<net_peer>> prom = std::make_shared<std::promise<net_peer>>();
    std::future<net_peer> fut = prom->get_future();

    identify(resolve_peer_in_table(peer),
        [prom](net_peer p_, std::string) {
            prom->set_value(p_);
        },
        [prom](net_contact) {
            prom->set_value(empty_net_peer);
        });

    return fut;
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::get_addresses(net_contact contact, hash_t target_id, addresses_callback ok, basic_callback bad) {
    net.send(true,
        contact.addresses, proto::type::query, proto::actions::get_addresses,
        id, util::msg_id(), proto::get_addresses_query_data{
            .i = util::enc58(target_id)
        },
        [this, ok, bad, target_id](net_peer peer, std::string s) {
            net_contact c = resolve_peer_in_table(peer);
            proto::get_addresses_resp_data d = util::deserialize<proto::get_addresses_resp_data>(s);

            std::vector<std::future<net_peer>> tasks;
            std::list<net_peer> valid_peers;

            net_addr our_addr("udp", net.get_ip_address(), net.port);

            for(auto a : d.p) {
                try {
                    net_peer peer(target_id, net_addr(a.t, a.a, std::atoi(a.p.c_str())));
                    if(peer.addr == our_addr) continue;
                    tasks.push_back(_verify_node(peer));
                } catch (std::exception&) { }
            }

            for(auto&& t : tasks) {
                net_peer p = t.get();

                // if the address does in fact correspond to the ID, 
                // "resolve" (find in table and add new address) then add to valid list
                if(p != empty_net_peer) {
                    valid_peers.push_back(p);
                }
            }

            ok(c, valid_peers);
        },
        [this, bad](net_peer p_) {
            table->stale(p_);
            bad(net_contact(p_));
        });
}

template <typename Network, typename Bucket>
std::future<typename node<Network, Bucket>::fut_t> node<Network, Bucket>::_lookup(bool fv, net_contact p, hash_t target_id) {
    std::shared_ptr<std::promise<fut_t>> prom = std::make_shared<std::promise<fut_t>>();
    std::future<fut_t> fut = prom->get_future();
    
    if(fv) {
        find_value(p, target_id,
            [&, prom](net_contact c, fv_value v) { prom->set_value(fut_t{std::move(c), std::move(v)}); }, 
            [&, prom](net_contact c) { prom->set_value(fut_t{std::move(c), fv_value{boost::blank()}}); });
    } else {
        find_node(p, target_id, 
            [&, prom](net_contact c, std::list<net_contact> v) { prom->set_value(fut_t{std::move(c), std::move(v)}); },
            [&, prom](net_contact c) { prom->set_value(fut_t{std::move(c), fv_value{boost::blank()}}); });        
    }

    return fut;
}

/// @brief lookup in routing table, if exists create an entry and return it filled w/ addresses
/// @brief otherwise, just
template <typename Network, typename Bucket>
net_contact node<Network, Bucket>::resolve_peer_in_table(net_peer peer) {
    boost::optional<routing_table_entry> res = table->find(peer.id);
    return res.has_value() ? net_contact(res.value()) : net_contact(peer);
}

/// @brief see xlattice/kademlia lookup
template <typename Network, typename Bucket>
std::list<net_contact> node<Network, Bucket>::lookup_nodes(std::deque<net_contact> shortlist, hash_t target_id) {
    std::list<net_contact> res;
    std::deque<net_peer> visited;

    net_contact closest_node = *std::min_element(shortlist.begin(), shortlist.end(), 
        [target_id](net_contact a, net_contact b) { 
            return (a.id ^ target_id) < (b.id ^ target_id); 
        }
    ), candidate;

    bool first = true;

    // check if:
    // - we are about to query ourselves
    // - we've already visited this specific IP:ID
    // - we've already added this IP or ID to the shortlist
    auto filter = [&, this](net_contact a) { 
        return a.id == id ||
            (std::count_if(a.addresses.begin(), a.addresses.end(),
                [&](const net_addr& ad) { 
                    return std::count_if(visited.begin(), visited.end(),
                        [&](const net_peer& pe) { return pe.addr == ad && pe.id == a.id; }
                    ) != 0;
                }) != 0);
    };

    auto filter2 = [&, this](net_contact a) { 
        return std::count_if(res.begin(), res.end(), 
            [&](const net_contact& e) { return e.id == a.id; }) != 0; 
    };

    auto sort = [&](net_contact a, net_contact b) { 
        return (a.id ^ target_id) < (b.id ^ target_id); 
    };

    while(!shortlist.empty()) {
        std::list<std::future<fut_t>> tasks;

        // send out alpha RPCs
        int n = 0;
        while(n++ < proto::alpha && !shortlist.empty()) {
            tasks.push_back(_lookup(false, shortlist.front(), target_id));
            shortlist.pop_front();
        }

        // get back replies
        for(auto&& t : tasks) {
            fut_t f = t.get();
            net_contact p = std::get<0>(f);
            fv_value v = std::get<1>(f);

            for(auto a : p.addresses)
                visited.emplace_back(p.id, a);
            
            // res is a list of all successfully contacted peers, shortlist is just a queue
            if(v.type() != typeid(boost::blank) && !filter2(p)) {
                res.push_back(p);
            }

            if(v.type() == typeid(std::list<net_contact>)) {
                // The node then fills the shortlist with contacts from the replies received.
                for(net_contact c : boost::get<std::list<net_contact>>(v)) {
                    if(!filter(c)) {
                        shortlist.push_back(c);
                    }
                }
            }

            // unlike xlattice's design, we do not handle values as we're
            // only looking for nodes
        }

        // nobody responded
        if(res.empty())
            break;
        
        std::sort(shortlist.begin(), shortlist.end(), sort);
        candidate = *std::min_element(res.begin(), res.end(), sort);

        if((candidate.id ^ target_id) < (closest_node.id ^ target_id) || first) {
            closest_node = candidate;
            first = false;
        } else break;
    }

    // remove ourselves, sort by closest
    res.remove_if([this](net_contact a) { return a.id == id; });
    res.sort(sort);

    // truncate results
    if(res.size() > proto::bucket_size)
        res.resize(proto::bucket_size);

    return res;
}

// see libp2p kad value retrieval 
template <typename Network, typename Bucket>
typename node<Network, Bucket>::fv_value node<Network, Bucket>::lookup_value(
    std::deque<net_contact> starting_list,
    boost::optional<std::shared_ptr<djc>> claimed,
    hash_t key, 
    int Q) {
    int cnt = 0;
    std::atomic_int pending{0};
    kv best;
    bool best_empty = true;
    std::deque<net_contact> pb, pq, pn, po;

    // search for key in local store, if `Q` == 0 or 1, the search is complete
    {
        LOCK(ht_mutex);
        auto it = ht.find(key);

        if(it != ht.end() && Q < 2) {
            spdlog::debug("dht: Q<2, found in local store, returning.");
            return it->second;
        } else if(it != ht.end()) {
            // otherwise, we count it as one of the values
            cnt++;
            best = it->second;
            spdlog::debug("dht: found already in local store, adding to values.");
        }
    }

    // seed `pn` with `a` peers
    pn = starting_list;

    // start iterative search
    while(true) {
        // if we've collected `Q` or more answers, return `best`.
        // if there are no requests pending and `pn` is empty, return `best`.
        if(cnt >= Q || (pending == 0 && pn.empty())) {
            spdlog::debug("dht: quorum/count reached. sending stores to outdated nodes.");

            // storing `best` at `po` nodes
            for(auto p : po) {
                spdlog::debug("dht: storing best value at {}", util::enc58(p.id));
                store(false, p, best, basic_nothing, basic_nothing);
            }

            return best;
        }

        std::list<std::future<fut_t>> tasks;

        // send `alpha` `pn` peers a find_value
        int n = 0;
        for(auto p : pn) {
            if(n++ >= proto::alpha)
                break;

            // for disjoint path lookups: check if peer appears in any other search
            if(claimed.has_value()) {
                LOCK(claimed.get()->mutex);
                
                // if not seen already in other paths, add to claimed list
                if(std::count(
                    claimed.get()->shortlist.begin(),
                    claimed.get()->shortlist.end(),
                    p) == 0) {
                    claimed.get()->shortlist.push_back(p);
                    spdlog::debug("dht: disjoint: {} not seen, adding to claimed list", util::enc58(p.id));
                } else {
                    // if seen already, we exclude this "claimed" peer
                    spdlog::debug("dht: disjoint: {} seen already, excluding, {}", util::enc58(p.id));
                    if(n > 0)
                        n--;
                    continue;
                }
            }
            
            // `pending` should never be larger than `alpha`
            pending++;
            tasks.push_back(_lookup(true, p, key));
            spdlog::debug("dht: querying {}...", util::enc58(p.id));
            
            // mark it as queried in `pq`
            pq.push_back(p);
        }

        // if there's nothing to do, just stop
        if(tasks.empty())
            break;

        for(auto&& t : tasks) {
            if(pending-- == 0) pending = 0;

            fut_t f = t.get();
            net_contact p = std::get<0>(f);
            fv_value v = std::get<1>(f);

            // for loop is in order of pn, not in terms of arrival. this is ok?
            pn.pop_front();

            // if an error or timeout occurs, discard it
            if(v.type() == typeid(boost::blank)) {
                spdlog::debug("dht: timeout/error from {}, discarding.", util::enc58(p.id));        
                continue;
            }

            spdlog::debug("dht: message back from {} ->", util::enc58(p.id));

            // if without value, add not already queried/to be queried closest nodes to `pn`
            if(v.type() == typeid(std::list<net_contact>)) {
                spdlog::debug("dht: \treceived bucket, adding unvisited peers ->");
                for(auto p_ : boost::get<std::list<net_contact>>(v)) {
                    // make sure it hasn't been queried,
                    // isn't already part of the to-query list and
                    // isn't ourselves 
                    if(std::count(pq.begin(), pq.end(), p_) == 0 &&
                        std::count(pn.begin(), pn.end(), p_) == 0 &&
                        p_.id != id) {
                        spdlog::debug("dht: \t\tpeer {}", util::enc58(p.id));
                        pn.push_back(p_);
                    }
                }
            }

            // if we receive a value,
            else if(v.type() == typeid(kv)) {
                kv kv_ = boost::get<kv>(v);
                cnt++;

                spdlog::debug("dht: \treceived value ->");

                // if this is the first value we've seen, 
                // store it in `best` and store peer in `pb` (best peer list)
                if(best_empty) {
                    spdlog::debug("dht: \t\tfirst value received, adding to best.");
                    best_empty = false;
                    best = kv_;

                    pb.push_back(p);
                } else {
                    // otherwise, we resolve the conflict by calling validator

                    spdlog::debug("dht: \t\tresolving conflict with validator ->");
                    // select newest and most valid between `best` and this value.
                    // if equal(?) just add peer to `pb`
                    if(crypto.validate(kv_) && kv_.timestamp >= best.timestamp) {
                        // if new value is equal just add to `pb`
                        if(kv_.timestamp == best.timestamp) {
                            spdlog::debug("dht: \t\t\tnew value is equal to best, adding peer to pb.");
                            pb.push_back(p);
                        }
                        
                        // if new value wins, mark all peers in `pb` as 
                        // outdated (empty `pb` into `po`) and set new peer as `best`
                        // and also add it to `pb`
                        else {
                            spdlog::debug("dht: \t\t\tnew value wins, marking peers as outdated ->");

                            for(auto o : pb) {
                                spdlog::debug("dht: \t\t\t\tmarking peer {} as outdated", util::enc58(o.id));
                                po.push_back(o);
                            }
                            
                            spdlog::debug("dht: \t\t\tclearing pb, setting new value as best, pushing peer to pb");
                            pb.clear();
                            best = kv_;

                            pb.push_back(p);
                        }
                    } else {
                        // if new value loses, add current peer to `po`
                        spdlog::debug("dht: \t\t\tnew value lost, adding current peer to po");
                        po.push_back(p);
                    }
                }
            }
        }
    }

    return best;
}

// this is for a new key-value pair
template <typename Network, typename Bucket>
void node<Network, Bucket>::iter_store(int type, const std::string& key, const std::string& value, basic_callback ok, basic_callback bad) {
    hash_t hash = util::hash(key);
    std::list<net_contact> b = iter_find_node(hash);

    // ignores the peer object anyways
    kv vl(hash, type, value, empty_net_peer, util::time_now(), "");

    // store operation does signing already
    for(auto i : b)
        store(true, i, vl, 
            [ok, bad](net_contact c) {
                ok(c);
            }, 
            [ok, bad](net_contact c) {
                bad(c);
            });
}

// this is for republishing
template <typename Network, typename Bucket>
void node<Network, Bucket>::republish(kv val) {
    std::list<net_contact> b = iter_find_node(val.key);

    for(auto i : b)
        store(false, i, val, basic_nothing, basic_nothing);
}

template <typename Network, typename Bucket>
std::list<net_contact> node<Network, Bucket>::iter_find_node(hash_t target_id) {
    std::deque<routing_table_entry> a = table->find_alpha(target_id);
    std::deque<net_contact> shortlist(a.size());

    if(a.empty()) return {};

    auto it = a.begin();
    std::generate(shortlist.begin(), shortlist.end(), [&]() {
        return net_contact(*(it++));
    });

    return lookup_nodes(shortlist, target_id);
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::iter_find_node_async(hash_t target_id, contacts_callback ok) {
    boost::asio::post(pool, [this, target_id, ok]() {
        std::list<net_contact> nodes = iter_find_node(target_id);
        std::vector<net_contact> vec(nodes.begin(), nodes.end());
        ok(vec);
    });
}

// refreshing buckets will remove all alternate IP addresses from the table
template <typename Network, typename Bucket>
void node<Network, Bucket>::refresh(tree<Network, Bucket>* ptr) {
    if(ptr == nullptr) return;
    if(!ptr->leaf) return;
    
    hash_t randomness = util::gen_randomness(reng);
    hash_t mask = ~hash_t(0) << (proto::bit_hash_width - ptr->prefix.cutoff);
    hash_t random_id = ptr->prefix.prefix | (randomness & ~mask);
    std::list<net_contact> bkt = iter_find_node(random_id);

    if(!bkt.empty()) {
        W_LOCK(table->mutex);

        ptr->data.clear();
        for(auto p : bkt) {
            routing_table_entry e{ id, {} };
            e.id = p.id;

            for(auto a : p.addresses)
                e.addresses.push_back(routing_table_entry::mi_addr{ a, 0 });

            ptr->data.push_back(e);
        }

        spdlog::debug("dht: refreshed bucket {}, sz: {}", util::enc58(ptr->prefix.prefix), ptr->data.size());
    }
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::join(const net_addr& a, basic_callback ok, basic_callback bad) {
    // add peer to routing table
    ping(net_contact(a), [this, ok](net_contact c) {
        // lookup our own id
        std::list<net_contact> bkt = iter_find_node(id);

        // populate routing table
        // only add one address (?? for now)
        for(auto i : bkt) {
            table->update(net_peer{ i.id, i.addresses.front() });
        }

        // it refreshes all buckets further away than its closest neighbor, 
        // which will be in the occupied bucket with the lowest index.
        table->dfs([&, this](tree<Network, Bucket>* ptr) {
            hash_t mask(~hash_t(0) << (proto::bit_hash_width - ptr->prefix.cutoff));
            if((c.id & mask) != ptr->prefix.prefix)
                refresh(ptr);
        });

        ok(c);
    }, bad);
}

template <typename Network, typename Bucket>
void node<Network, Bucket>::resolve(bool add, hash_t target_id, basic_callback ok, basic_callback bad) {
    iter_find_node_async(target_id, [this, add, target_id, ok, bad](std::vector<net_contact> nodes) {
        std::size_t sz = nodes.size();
        std::shared_ptr<std::size_t> cnt = std::make_shared<std::size_t>(0);

        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
            [&](const net_contact& c) { return c.id == id; }), nodes.end());

        if(nodes.empty()) {
            bad(net_contact(target_id, {}));
            return;
        }
        
        // may/may not be used
        std::shared_ptr<net_contact> non_add = std::make_shared<net_contact>();
        non_add->id = target_id;

        for(auto n : nodes) {
            get_addresses(n, target_id, 
                [this, add, target_id, sz, cnt, ok, bad, non_add](net_contact c, std::list<net_peer> peers) {
                    peers.erase(std::remove_if(peers.begin(), peers.end(),
                        [&](const net_peer& p) { return p.id != target_id || p.id == id; }),
                        peers.end());

                    // are we adding these addresses to the routing table?
                    if(add) {
                        for(auto p : peers) {
                            table->update(p);
                        }

                        boost::optional<routing_table_entry> rte = table->find(target_id);
                        if(!rte.has_value()) {
                            bad(c);
                            return;
                        }

                        if(++(*cnt) >= sz)
                            ok(net_contact(rte.value()));
                    } else {
                        for(auto p : peers) {
                            non_add->addresses.push_back(p.addr);
                        }

                        if(++(*cnt) >= sz) {
                            ok(*non_add);
                        }
                    }
                }, 
                [this, sz, cnt, bad](net_contact c) {
                    if(++(*cnt) >= sz)
                        bad(c);
                });
        }
    });
}

}
}