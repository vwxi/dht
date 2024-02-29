#include "bucket.hpp"
#include "routing.hpp"
#include "network.hpp"

namespace lotus {
namespace dht {

template <typename Network>
bucket<Network>::bucket(std::shared_ptr<routing_table<Network, bucket>> rt) : 
    last_seen(0), table(rt) { }

template <typename Network>
void bucket<Network>::responded(net_peer req) {
    spdlog::debug("routing: responded, updating");
    auto it = std::find_if(begin(), end(), [&](routing_table_entry e) { return e.id; });
    if(it == end())
        return;

    auto a = std::find_if(it->addresses.begin(), it->addresses.end(), 
        [&](const routing_table_entry::mi_addr& ad) { return ad.first == req.addr; });

    if(a == it->addresses.end()) {
        // new address. ignore if limit is reached
        if(it->addresses.size() < proto::table_entry_addr_limit) {
            it->addresses.emplace_back(req.addr, 0);
            spdlog::debug("routing: new address for existing node {} found: {}, adding.", util::enc58(it->id), req.addr.to_string());
        }
    } else {
        if(a->second < proto::missed_pings_allowed) {
            if(a->second-- == 0) a->second = 0;
            spdlog::debug("routing: pending node {} updated", util::enc58(it->id));
            splice(end(), *this, it);
        } else {
            spdlog::debug("routing: erasing pending node {}", util::enc58(it->id));
            it = erase(it);
        }
    }

    last_seen = TIME_NOW();
}

template <typename Network>
void bucket<Network>::stale(net_peer req) {
    auto it = std::find_if(begin(), end(), 
        [&](const routing_table_entry& e) { return e.id == req.id; });

    if(it == end())
        return; // fail?

    auto itt = std::find_if(it->addresses.begin(), it->addresses.end(), 
        [&](routing_table_entry::mi_addr ad) { return ad.first == req.addr; });

    if(itt != it->addresses.end()) {
        // make address more stale
        // if too stale, evict address
        if(itt->second++ > proto::missed_pings_allowed) {
            spdlog::debug("routing: {} ({}) exceeded stale limit", 
                req.addr.to_string(), 
                util::enc58(req.id));

            // if this is the last address available, see if can remove from bucket
            itt = it->addresses.erase(itt);

            if(it->addresses.empty()) {
                // do we have something in the cache? if so, add to bucket instead
                if(!cache.empty()) {
                    LOCK(cache_mutex);

                    const net_peer& cit = cache.back();
                    spdlog::debug("routing: adding {} from cache to bucket and removing {}", 
                        util::enc58(cit.id), util::enc58(it->id));
                    emplace_back(cit.id, cit.addr);
                    cache.pop_back();
                } else {
                    // nothing left in cache, nothing left in address list, just erase it from bucket
                    spdlog::debug("routing: nothing in cache, just erasing node {} from bucket", 
                        util::enc58(it->id));
                }
                
                it = erase(it);
            } else {
                spdlog::debug("routing: node {} still has addresses in bucket entry", util::enc58(it->id));
            }
        }
    }

    last_seen = TIME_NOW();
}

template <typename Network>
void bucket<Network>::add_new(net_peer req) {
    if(size() < proto::bucket_size) {
        emplace_back(req.id, req.addr);
        spdlog::debug("routing: new node (id: {}, addr: {}), size: {}", util::enc58(req.id), req.addr.to_string(), size());
    
        last_seen = TIME_NOW();
    }
}

// called when entry is "nearby". 
// if doesn't exist, add to back
// if exists, move to back
// if exists but address is new, move to back and add to address list
template <typename Network>
void bucket<Network>::add_or_update_near_entry(net_peer req) {
    auto rit = std::find_if(begin(), end(), 
        [&](routing_table_entry e) { return e.id == req.id; });

    // id exists already
    if(rit != end()) {
        // move node to bucket tail
        splice(end(), *this, rit);
        
        // but address is new
        if(std::find_if(rit->addresses.begin(), rit->addresses.end(), 
            [&](const routing_table_entry::mi_addr& mi) { 
                return mi.first == req.addr; 
            }) == rit->addresses.end()) {
            
            // if limit reached, ignore
            if(rit->addresses.size() < proto::table_entry_addr_limit) {
                rit->addresses.emplace_back(req.addr, 0);
                spdlog::debug("routing: new address for existing node {} found: {}, adding.", util::enc58(rit->id), req.addr.to_string());
            }
        }

        spdlog::debug("routing: exists already, moved node {} to tail. size: {}", util::enc58(rit->id), size());
    } else {
        if(size() < proto::bucket_size) {
            emplace_back(req.id, req.addr);
            spdlog::debug("routing: new node (id: {}, addr: {}), size: {}", util::enc58(req.id), req.addr.to_string(), size());
        }
    }

    last_seen = TIME_NOW();
}

// entry isnt in own peer's bucket
// if replies, 
template <typename Network>
void bucket<Network>::update_far_entry(net_peer req) {
    if(empty()) 
        return;

    net_contact contact(front());
    spdlog::debug("routing: checking if node {} is alive", util::enc58(contact.id));

    // try what addresses are available if the first doesnt work out
    table->net.send(true,
        contact.addresses, proto::type::query, proto::actions::ping, 
        table->id, util::msg_id(), msgpack::type::nil_t(),
        [this, req](net_peer, std::string) {
            responded(req);
        },
        [this, req](net_peer) {
            stale(req);
        });
}

// add/update replacement cache
template <typename Network>
void bucket<Network>::update_cache(net_peer req) {
    LOCK(cache_mutex);
                
    // is node unknown
    auto cit = std::find_if(cache.begin(), cache.end(),
        [&](net_peer p) { return p.id == req.id; });
    
    // node is unknown
    if(cit == cache.end()) {
        // is the cache full? kick out oldest node and add this one
        if(cache.size() > proto::repl_cache_size) {
            spdlog::debug("routing: replacement cache is full, removing oldest candidate");
            cache.pop_front();
        }
        
        // node is unknown and doesn't exist in cache, add
        cache.push_back(req);
        spdlog::debug("routing: node {} is unknown, adding to replacement cache", util::enc58(req.id));
    } else {
        // node exists in cache, move to back
        cache.splice(cache.end(), cache, cit);
        spdlog::debug("routing: node {} is unknown, moving to end of replacement cache", util::enc58(req.id));
    }
}

// handle real case
EINST(bucket, network<upnp>);

///// FOR TESTS

// bucket that always returns response
template <>
void bucket<test::mock_network>::update_far_entry(net_peer req) {
    if(empty())
        return;

    net_contact contact(front());
    table->net.send(true,
        contact.addresses, proto::type::query, proto::actions::ping, 
        table->id, util::msg_id(), msgpack::type::nil_t(),
        [this, req](net_peer, std::string) {
            responded(req);
        },
        [this, req](net_peer) {
            stale(req);
        });
}

// handle test cases
EINST(bucket, test::mock_network);
EINST(bucket, test::mock_rt_net_resp);
EINST(bucket, test::mock_rt_net_unresp);
EINST(bucket, test::mock_rt_net_maybe);

}
}