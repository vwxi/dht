#include "dht.h"
#include "proto.h"
#include "util.hpp"

namespace tulip {
namespace dht {

node::node(bool local, u16 port) :
    net(local, port,
        std::bind(&node::handle_ping, this, _1, _2),
        std::bind(&node::handle_store, this, _1, _2),
        std::bind(&node::handle_find_node, this, _1, _2),
        std::bind(&node::handle_find_value, this, _1, _2),
        std::bind(&node::handle_identify, this, _1, _2)),
    reng(rd()),
    treng(rd()),
    running(false) {
    std::srand(util::time_now());
}

node::~node() {
    if(running) {
        refresh_thread.join();
        republish_thread.join();
    }
}

hash_t node::get_id() const {
    return id;
} 

/// runners

void node::_run() {
    id = util::hash(crypto.pub_key());

    table = std::make_shared<routing_table>(id, net);
    table_ref = table;
    table->init();

    spdlog::debug("dht: running DHT node on port {} (id: {})", net.port, util::b58encode_h(id));
    
    running = true;

    net.run();
    
    spdlog::debug("dht: ip address: {}", net.get_ip_address());

    refresh_thread = std::thread([&, this]() {
        while(true) {
            std::this_thread::sleep_for(seconds(proto::refresh_interval));
            table->dfs([&, this](tree* ptr) {
                auto time_since = util::time_now() - ptr->data.last_seen;
                if(time_since > proto::refresh_time)
                    refresh(ptr);
            });
        }
    });

    republish_thread = std::thread([&, this]() {
        while(true) {
            std::this_thread::sleep_for(seconds(proto::refresh_interval));

            {
                LOCK(ht_mutex);
                for(const auto& entry : ht) {
                    if(util::time_now() - entry.second.timestamp > proto::republish_time)
                        republish(entry.second);
                }
            }
        }
    });
}

/// @brief generate keypair, start node
void node::run() {
    crypto.generate_keypair();
    _run();
}

/// @brief import keypair from files, start node
void node::run(std::string pub_filename, std::string priv_filename) {
    crypto.import_file(pub_filename, priv_filename);
    _run();
}

/// keypair stuff

void node::export_keypair(std::string pub_filename, std::string priv_filename) {
    crypto.export_file(pub_filename, priv_filename);
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

        hash_t k(util::b58decode_h(d.k));
        u32 chksum = util::crc32b((u8*)d.v.data());

        int s = proto::status::ok;
        
        try {
            LOCK(ht_mutex);
            ht[k] = kv(k, d.d, d.v, d.o.has_value() ? d.o.value().to_peer() : p, d.t, d.s);
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

        hash_t target_id(util::b58decode_h(d.t));
        bucket bkt = table->find_bucket(peer(target_id));

        std::vector<proto::peer_object> b;
        for(auto i : bkt)
            b.push_back(proto::peer_object(i));

        std::stringstream ss;
        msgpack::pack(ss, b);

        proto::find_node_resp_data resp {
            .b = std::move(b),
            .s = crypto.sign(ss.str())
        };

        net.send(false,
            p, proto::type::response, proto::actions::find_node,
            id, msg.q, resp,
            net.queue.q_nothing, net.queue.f_nothing);

        table->update(p);
    } else if(msg.m == proto::type::response) {
        proto::find_node_resp_data d;
        bucket bkt(table);
        msgpack::object_handle oh;

        msg.d.convert(d);

        // hacky, repack
        std::stringstream ss;
        msgpack::pack(ss, d);

        net.queue.satisfy(p, msg.q, ss.str());
        
        table->update(p);
    }
}

void node::handle_find_value(peer p, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::find_query_data d;
        msg.d.convert(d);

        hash_t target_id(util::b58decode_h(d.t));

        {
            LOCK(ht_mutex);
            decltype(ht)::iterator it;
            if((it = ht.find(target_id)) != ht.end()) {
                // key exists in hash table
                net.send(false,
                    p, proto::type::response, proto::actions::find_value,
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
                bucket bkt = table->find_bucket(peer(target_id));

                std::vector<proto::peer_object> b;
                for(auto i : bkt)
                    b.push_back(proto::peer_object(i));

                std::stringstream ss;
                msgpack::pack(ss, b);

                proto::find_node_resp_data resp {
                    .b = std::move(b),
                    .s = crypto.sign(ss.str())
                };

                net.send(false,
                    p, proto::type::response, proto::actions::find_value,
                    id, msg.q, proto::find_value_resp_data { .v = boost::none, .b = resp },
                    net.queue.q_nothing, net.queue.f_nothing);
            }
        }

        table->update(p);
    } else if(msg.m == proto::type::response) {
        proto::find_value_resp_data d;
        msg.d.convert(d);

        // hacky, repack
        std::stringstream ss;
        msgpack::pack(ss, d);
        
        net.queue.satisfy(p, msg.q, ss.str());

        table->update(p);
    }
}

void node::handle_identify(peer p, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::identify_query_data d;
        msg.d.convert(d);
        
        net.send(false,
            p, proto::type::response, proto::actions::identify,
            id, msg.q, proto::identify_resp_data{
                .k = crypto.pub_key(),
                .s = crypto.sign(
                    fmt::format("{}:{}:{}", 
                        d.s, p.addr, p.port)) // sign secret token
            },
            net.queue.q_nothing, net.queue.f_nothing);
    } else if(msg.m == proto::type::response) {
        proto::identify_resp_data d;
        msg.d.convert(d);

        // hacky, repack
        std::stringstream ss;
        msgpack::pack(ss, d);

        net.queue.satisfy(p, msg.q, ss.str());

        /// @note identify does not update table
    }
}

void node::handle_get_addresses(peer p, proto::message msg) {
    if(msg.m == proto::type::query) {
        proto::get_addresses_query_data d;
        msg.d.convert(d);

        boost::optional<peer> p_ = table->find(util::b58decode_h(d.i));

        std::vector<proto::get_addresses_peer_record> addresses;

        if(p_.has_value()) {
            // we should have already ignored bad peer records.
            for(auto a : p_.value().addresses) 
                addresses.push_back(proto::get_addresses_peer_record {
                    .t = a.address.transport(),
                    .a = a.address.addr,
                    .p = a.address.port,
                    .s = a.signature
                }
            );
        }

        net.send(false,
            p, proto::type::response, proto::actions::get_addresses,
            id, msg.q, proto::get_addresses_resp_data{
                .p = addresses
            },
            net.queue.q_nothing, net.queue.f_nothing);
    } else if(msg.m == proto::type::response) {
        proto::get_addresses_resp_data d;
        msg.d.convert(d);

        // erase all records with invalid signatures
        d.p.erase(std::remove_if(d.p.begin(), d.p.end(),
            [&, this](proto::get_addresses_peer_record r) {
                hash_t t_id = util::b58decode_h(d.i);
                peer_record record(r.t, r.a, r.p, r.s);
                
                // hacky
                if(identify_node(peer(t_id, record))) {
                    return !crypto.verify(t_id, record.address.to_string(), record.signature);
                } else return true; // could not acquire pub key     
            }));

        // hacky, repack
        std::stringstream ss;
        msgpack::pack(ss, d);

        net.queue.satisfy(p, msg.q, ss.str());

        /// @note get_addresses does not update table
    }
}

/// public interfaces 

void node::put(std::string key, std::string value) {
    iter_store(proto::store_type::data, key, value);
}

void node::get(std::string key, value_callback cb) {
    std::list<fv_value> l = disjoint_lookup_value(util::hash(key), proto::quorum);
    std::vector<kv> values;

    for(auto i : l) {
        if(i.type() == typeid(boost::blank) ||
            i.type() == typeid(bucket))
            continue;
        else if(i.type() == typeid(kv)) {
            kv v = boost::get<kv>(i);

            // get will only fetch valid data
            if(crypto.validate(v))
                values.push_back(v);
        }
    }

    cb(std::move(values));
}

void node::provide(std::string key, peer provider) {
    std::stringstream ss;
    proto::peer_object o(provider);
    msgpack::pack(ss, o);

    iter_store(proto::store_type::provider_record, key, ss.str());
}

void node::get_providers(std::string key, prov_callback cb) {
    get(key, [this, cb](std::vector<kv> values) {
        std::vector<peer> providers;

        (void)std::remove_if(values.begin(), values.end(), [](const kv& p) { 
            return p.type != proto::store_type::provider_record;
        });

        for(auto v : values) {
            msgpack::object_handle oh;
            msgpack::unpack(oh, v.value.data(), v.value.size());
            msgpack::object obj = oh.get();
            proto::peer_object p;
            obj.convert(p);
            providers.push_back(p.to_peer());
        }
        
        cb(providers);
    });
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
    
    // hacky
    val.origin.id = id;

    boost::optional<proto::peer_object> po = origin ? 
        boost::optional<proto::peer_object>(boost::none) : 
        boost::optional<proto::peer_object>(proto::peer_object(val.origin));

    net.send(true,
        p, proto::type::query, proto::actions::store,
        id, util::msg_id(), proto::store_query_data{ 
            .k = util::b58encode_h(val.key), 
            .d = val.type,
            .v = val.value, 
            .o = po,
            .t = val.timestamp,
            .s = origin ? crypto.sign(val.sig_blob()) : val.signature },
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
        bad);
}

void node::find_node(peer p, hash_t target_id, bucket_callback ok, basic_callback bad) {
    net.send(true,
        p, proto::type::query, proto::actions::find_node,
        id, util::msg_id(), proto::find_query_data { .t = util::b58encode_h(target_id) },
        [this, ok, bad](peer p_, std::string s) {
            msgpack::object_handle oh;
            msgpack::unpack(oh, s.data(), s.size());
            msgpack::object obj = oh.get();
            proto::find_node_resp_data b;
            obj.convert(b);

            bucket bkt(table);

            for(auto i : b.b)
                bkt.push_back(peer(i.t, i.a, i.p, util::b58decode_h(i.i)));

            // verify signature
            std::stringstream ss;
            {
                std::vector<proto::peer_object> po;
                for(auto i : bkt)
                    po.push_back(proto::peer_object(i));
                msgpack::pack(ss, po);
            }

            if(crypto.verify(p_.id, ss.str(), b.s))
                ok(p_, std::move(bkt));
            else
                bad(p_);
        },
        bad);
}

void node::find_value(peer p, hash_t target_id, find_value_callback ok, basic_callback bad) {
    net.send(true,
        p, proto::type::query, proto::actions::find_value,
        id, util::msg_id(), proto::find_query_data { .t = util::b58encode_h(target_id) },
        [this, ok, bad, target_id](peer p_, std::string s) {
            msgpack::object_handle oh;
            msgpack::unpack(oh, s.data(), s.size());
            msgpack::object obj = oh.get();
            proto::find_value_resp_data d;
            obj.convert(d);

            if(!d.v.has_value() != !d.b.has_value()) {
                if(d.v.has_value()) {
                    proto::stored_data sd = d.v.value();
                    ok(p_, kv(target_id, sd.d, sd.v, sd.o.to_peer(), sd.t, sd.s));
                } else if(d.b.has_value()) {
                    bucket bkt(table);

                    for(auto i : d.b.value().b)
                        bkt.push_back(peer(i.t, i.a, i.p, util::b58decode_h(i.i)));

                    // verify signature
                    std::stringstream ss;
                    {
                        std::vector<proto::peer_object> po;
                        for(auto i : bkt)
                            po.push_back(proto::peer_object(i));
                        msgpack::pack(ss, po);
                    }

                    if(crypto.verify(p_.id, ss.str(), d.b.value().s))
                        ok(p_, std::move(bkt));
                    else
                        bad(p_);
                }
            } else {
                bad(p_);
            }
        },
        bad);
}

void node::identify(peer p, identify_callback ok, basic_callback bad) {
    std::string token = util::gen_token(treng);

    net.send(true,
        p, proto::type::query, proto::actions::identify,
        id, util::msg_id(), proto::identify_query_data {
            .s = token
        },
        [this, ok, bad, token](peer p_, std::string s) {
            msgpack::object_handle oh;
            msgpack::unpack(oh, s.data(), s.size());
            msgpack::object obj = oh.get();
            proto::identify_resp_data d;
            obj.convert(d);

            if(p_.id != util::hash(d.k)) {
                // if peer id isn't hash(pkey), it's bad
                bad(p_);
                return;
            }

            // put into local keystore, will be removed if following verify fails
            crypto.ks_put(p_.id, d.k);

            // verify if signature for token is correct
            std::string blob = fmt::format("{}:{}:{}", token, net.get_ip_address(), net.port);
            if(crypto.verify(p_.id, blob, d.s))
                ok(p_, s);
            else
                bad(p_);
        },
        bad);
}

void node::get_addresses(peer p, hash_t target_id, get_addresses_callback ok, basic_callback bad) {
    net.send(true,
        p, proto::type::query, proto::actions::get_addresses,
        id, util::msg_id(), proto::get_addresses_query_data {
            .i = util::b58encode_h(target_id)
        },
        [this, ok, bad, target_id](peer p_, std::string s) {
            msgpack::object_handle oh;
            msgpack::unpack(oh, s.data(), s.size());
            msgpack::object obj = oh.get();
            proto::get_addresses_resp_data d;
            obj.convert(d);

            bucket& bkt = table->find_bucket_ref(target_id);
            auto it = std::find_if(bkt.begin(), bkt.end(), [target_id](const peer& p) {
                return p.id == target_id;
            });

            // already filtered in handler
            if(it != bkt.end()) {
                for(auto r : d.p) {
                    peer_record record(r.t, r.a, r.p, r.s);
                    if(!std::count(it->addresses.begin(), it->addresses.end(), record)) {
                        it->addresses.push_back(record);
                    }
                }

                ok(p_, it->addresses);
            } else {
                bad(p_);
            }
        },
        bad);
}

std::future<std::string> node::_identify(peer p) {
    std::shared_ptr<std::promise<std::string>> prom = std::make_shared<std::promise<std::string>>();
    std::future<std::string> fut = prom->get_future();

    identify(p,
        [&, prom](peer, std::string s) { prom->set_value(s); },
        [&, prom](peer) { spdlog::info("couldnt get pubkey"); prom->set_value(""); });

    return fut;
}

bool node::identify_node(peer p) {
    // acquire pubkey if not already got
    if(!crypto.ks_has(p.id)) {
        // send identify first, if fails to acquire, return bad
        std::future<std::string> fs = _identify(p);
        std::string s = fs.get();

        if(s.empty())
            return false;
    }

    return true;
}

std::future<node::fut_t> node::_lookup(bool fv, peer p, hash_t target_id) {
    std::shared_ptr<std::promise<fut_t>> prom = std::make_shared<std::promise<fut_t>>();
    std::future<fut_t> fut = prom->get_future();
    
    if(!identify_node(p)) {
        prom->set_value(fut_t{p, fv_value{boost::blank()}});
        return fut;
    }

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

// see xlattice/kademlia lookup
bucket node::lookup_nodes(std::deque<peer> shortlist, hash_t target_id) {
    std::list<peer> visited;
    bucket res(table);

    peer closest_node = *std::min_element(shortlist.begin(), shortlist.end(), 
        [target_id](peer a, peer b) { return (a.id ^ target_id) < (b.id ^ target_id); }),
        candidate{};
    bool first = true;

    auto filter = [&, this](peer a) { 
        return std::count(visited.begin(), visited.end(), a) != 0 || 
            std::count(shortlist.begin(), shortlist.end(), a) != 0 || 
            a.id == id; };
    auto filter2 = [&, this](peer a) { return std::count(res.begin(), res.end(), a) != 0; };
    auto sort = [&](peer a, peer b) { return (a.id ^ target_id) < (b.id ^ target_id); };

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
            peer p = std::get<0>(f);
            fv_value v = std::get<1>(f);

            visited.push_back(p);

            // res is a list of all successfully contacted peers, shortlist is just a queue
            if(v.type() != typeid(boost::blank) && !filter2(p))
                res.push_back(p);

            if(v.type() == typeid(bucket)) {
                // The node then fills the shortlist with contacts from the replies received.
                for(peer p_ : boost::get<bucket>(v)) {
                    if(!filter(p_))
                        shortlist.push_back(p_);
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
    res.remove_if([this](peer a) { return a.id == id; });
    res.sort(sort);

    // truncate results
    if(res.size() > proto::bucket_size)
        res.resize(proto::bucket_size);

    return res;
}

// see libp2p kad value retrieval 
node::fv_value node::lookup_value(
    std::deque<peer> starting_list,
    boost::optional<std::shared_ptr<djc>> claimed,
    hash_t key, 
    int Q) {
    int cnt = 0;
    std::atomic_int pending{0};
    kv best;
    bool best_empty = true;
    std::deque<peer> pb, pq, pn, po;

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
            spdlog::debug("dht: quorum reached. sending stores to outdated nodes.");

            // storing `best` at `po` nodes
            for(auto p : po) {
                spdlog::debug("dht: storing best value at {}", p());
                store(false, p, best, basic_nothing, basic_nothing);
            }

            identify_node(best.origin);

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
                    spdlog::debug("dht: disjoint: {} not seen, adding to claimed list", p());
                } else {
                    // if seen already, we exclude this "claimed" peer
                    spdlog::debug("dht: disjoint: {} seen already, excluding, {}", p());
                    if(n > 0)
                        n--;
                    continue;
                }
            }
            
            // `pending` should never be larger than `alpha`
            pending++;
            tasks.push_back(_lookup(true, p, key));
            spdlog::debug("dht: querying {}...", p());
            
            // mark it as queried in `pq`
            pq.push_back(p);
        }

        // if there's nothing to do, just stop
        if(tasks.empty())
            break;

        for(auto&& t : tasks) {
            pending--;

            fut_t f = t.get();
            peer p = std::get<0>(f);
            fv_value v = std::get<1>(f);

            // for loop is in order of pn, not in terms of arrival. this is ok?
            pn.pop_front();

            // if an error or timeout occurs, discard it
            if(v.type() == typeid(boost::blank)) {
                spdlog::debug("dht: timeout/error from {}, discarding.", p());        
                continue;
            }

            spdlog::debug("dht: message back from {} ->", p());

            // if without value, add not already queried/to be queried closest nodes to `pn`
            if(v.type() == typeid(bucket)) {
                spdlog::debug("dht: \treceived bucket, adding unvisited peers ->");
                for(auto p_ : boost::get<bucket>(v)) {
                    // make sure it hasn't been queried,
                    // isn't already part of the to-query list and
                    // isn't ourselves 
                    if(std::count(pq.begin(), pq.end(), p_) == 0 &&
                        std::count(pn.begin(), pn.end(), p_) == 0 &&
                        p_.id != id) {
                        spdlog::debug("dht: \t\tpeer {}", p_());
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
                                spdlog::debug("dht: \t\t\t\tmarking peer {} as outdated", o());
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
void node::iter_store(int type, std::string key, std::string value) {
    hash_t hash = util::hash(key);
    bucket b = iter_find_node(hash);
    // ignores the peer object anyways
    kv vl(hash, type, value, peer(), util::time_now(), "");

    // store operation does signing already
    for(auto i : b)
        store(true, i, vl, basic_nothing, basic_nothing);
}

// this is for republishing
void node::republish(kv val) {
    bucket b = iter_find_node(val.key);
    val.timestamp = TIME_NOW();

    for(auto i : b)
        store(false, i, val, basic_nothing, basic_nothing);
}

bucket node::iter_find_node(hash_t target_id) {
    std::deque<peer> shortlist = table->find_alpha(peer(target_id));
    bucket b = lookup_nodes(shortlist, target_id);
    return b;
}

void node::refresh(tree* ptr) {
    if(ptr == nullptr) return;
    if(!ptr->leaf) return;
    
    hash_t randomness = util::gen_randomness(reng);
    hash_t mask = ~hash_t(0) << (proto::bit_hash_width - ptr->prefix.cutoff);
    hash_t random_id = ptr->prefix.prefix | (randomness & ~mask);
    bucket bkt = iter_find_node(random_id);

    if(!bkt.empty()) {
        W_LOCK(table->mutex);
        ptr->data = bkt;
        spdlog::debug("dht: refreshed bucket {}, sz: {}", util::htos(ptr->prefix.prefix), ptr->data.size());
    }
}

void node::join(peer p_, basic_callback ok, basic_callback bad) {
    // add peer to routing table
    ping(p_, [this, ok](peer p) {
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