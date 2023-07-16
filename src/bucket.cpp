#include "bucket.h"
#include "routing.h"
#include "network.h"

namespace tulip {
namespace dht {

bucket::bucket() : max_size(proto::K), last_seen(0) { };

bucket::~bucket() { }

/// @brief update a peer in a bucket
/// @param table routing table
/// @param req peer struct
/// @param nearby is the current bucket nearby to root id?
void bucket::update(routing_table& table, peer req, bool nearby) {
    auto rit = std::find_if(begin(), end(), 
        [req](peer p) { return p.id == req.id; });

    if(!nearby) {
        peer beg = *begin();
        spdlog::debug("checking if node {} ({}:{}) is alive", util::htos(beg.id), beg.addr, beg.port);
        table.node_.send(
            beg,
            proto::actions::ping,
            0,
            [t = &table.node_](std::future<std::string> fut, peer req, pend_it it) {
                OBTAIN_FUT_MSG;

                if(!std::memcmp(&m.magic, &proto::consts.magic, proto::ML) &&
                    m.action == proto::actions::ping &&
                    m.reply == proto::context::response) {
                    {
                        std::lock_guard<std::mutex> g(t->table.mutex);
                        t->table.update_pending(req);
                    }

                    spdlog::debug("responded, updating");
                }
            },
            [t = &table.node_](std::future<std::string> fut, peer req, pend_it it) {
                {
                    std::lock_guard<std::mutex> g(t->table.mutex);
                    int s;
                    if((s = t->table.stale(req)) > proto::M) {
                        t->table.evict(req);
                        spdlog::debug("did not respond, evicting {}", util::htos(req.id));
                    } else if(s != -1) {
                        spdlog::debug("did not respond. staleness: {}", s);
                    }
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