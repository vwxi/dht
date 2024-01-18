#include "bucket.h"
#include "routing.h"
#include "network.h"

namespace tulip {
namespace dht {

bucket::bucket(std::shared_ptr<routing_table> rt) : max_size(proto::bucket_size), last_seen(0), table(rt) { };

/// @brief update a peer in a bucket
/// @param table routing table
/// @param req peer struct
/// @param nearby is the current bucket nearby to root id?
void bucket::update(peer req, bool nearby) {
    auto rit = std::find_if(begin(), end(), 
        [req](peer p) { return p.id == req.id; });

    if(!nearby) {
        peer beg = *begin();
        spdlog::debug("routing: checking if node {} is alive", beg());

        table->net.send(true,
            beg, proto::type::query, proto::actions::ping, 
            table->id, util::msg_id(), msgpack::type::nil_t(),
            [this](peer p, std::string) {
                spdlog::debug("routing: responded, updating");
                table->update_pending(p);
            },
            [this](peer p) {
                int s;
                if((s = table->stale(p)) > proto::missed_pings_allowed) {
                    table->evict(p);
                    spdlog::debug("routing: did not respond, evicting {}", util::htos(p.id));
                } else if(s != -1) {
                    spdlog::debug("routing: did not respond. staleness: {}", s);
                }
            });

        goto end;
    }

    if(rit != end()) {
        peer _p = *rit;            
        splice(end(), *this, rit);
        spdlog::debug("routing: exists already, moved node {} to tail. size: {}", util::htos(_p.id), size());
        goto end;
    }

    if(size() < max_size) {
        push_back(req);
        spdlog::debug("routing: pushed back, size: {}", size());
        goto end;
    }

end:
    last_seen = TIME_NOW();
}

}
}