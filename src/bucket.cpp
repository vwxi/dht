#include "bucket.h"
#include "routing.h"
#include "network.h"

namespace tulip {
namespace dht {

bucket::bucket(routing_table& rt) : max_size(proto::bucket_size), last_seen(0), table(rt) { };

bucket::~bucket() { }

/// @brief update a peer in a bucket
/// @param table routing table
/// @param req peer struct
/// @param nearby is the current bucket nearby to root id?
void bucket::update(peer req, bool nearby) {
    auto rit = std::find_if(begin(), end(), 
        [req](peer p) { return p.id == req.id; });

    if(!nearby) {
        peer beg = *begin();
        spdlog::debug("checking if node {} ({}:{}) is alive", util::htos(beg.id), beg.addr, beg.port);

        table.net.send(true,
            beg, proto::type::query, proto::actions::ping, 
            table.id, util::msg_id(), msgpack::type::nil_t(),
            [this](peer p, std::string) {
                spdlog::debug("responded, updating");
                table.update_pending(p);
            },
            [this](peer p) {
                int s;
                if((s = table.stale(p)) > proto::missed_pings_allowed) {
                    table.evict(p);
                    spdlog::debug("did not respond, evicting {}", util::htos(p.id));
                } else if(s != -1) {
                    spdlog::debug("did not respond. staleness: {}", s);
                }
            });

        goto end;
    }

    if(rit != end()) {
        peer _p = *rit;            
        splice(end(), *this, rit);
        spdlog::debug("exists already, moved node {} to tail. size: {}", util::htos(_p.id), size());
        goto end;
    }

    if(size() < max_size) {
        push_back(req);
        spdlog::debug("pushed back, size: {}", size());
        goto end;
    }

end:
    last_seen = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// is bucket closer to t than b
bool bucket::closer(const bucket& b, hash_t t) {
    auto cmp = [t](peer a, peer b) { return a.distance(t) < b.distance(t); };
    return std::min_element(begin(), end(), cmp)->id < std::min_element(b.begin(), b.end(), cmp)->id;
}

}
}