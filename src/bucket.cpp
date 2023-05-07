#include "bucket.h"
#include "routing.h"
#include "network.h"

namespace dht {

bucket::bucket(routing_table& rt) : max_size(proto::K), last_seen(0), table(rt) { };

bucket::~bucket() { }

/// @brief update a peer in a bucket
/// @param req peer struct
/// @param nearby is the current bucket nearby to root id?
void bucket::update(peer req, bool nearby) {
    auto rit = std::find_if(begin(), end(), 
        [req](peer p) { return p.id == req.id; });

    if(!nearby) {
        peer beg = *begin();
        spdlog::info("checking if node {} ({}:{}) is alive", util::htos(beg.id), beg.addr, beg.port);
        table.node_.send(
            beg,
            proto::actions::ping,
            std::bind(&node::okay<proto::actions::ping>, &table.node_, _1, _2, _3),
            std::bind(&node::bad<proto::actions::ping>, &table.node_, _1, _2, _3));

        goto end;
    }

    if(rit != end()) {
        peer _p = *rit;            
        splice(end(), *this, rit);
        spdlog::info("exists already, moved node {} to tail. size: {}", util::htos(_p.id), size());
        goto end;
    }

    if(size() < max_size) {
        push_back(req);
        spdlog::info("pushed back, size: {}", size());
        goto end;
    }

end:
    last_seen = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

}