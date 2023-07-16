#include "dht.h"

namespace tulip {

/////////////////////////////////////////////////
/// accessible node interface
/////////////////////////////////////////////////

/// @brief initialize tulip node
node::node() :
    dht::node(dht::proto::Mport, dht::proto::Rport) { start(); }

node::node(u16 m, u16 r) :
    dht::node(m, r) { start(); }

/// @brief do nothing
void node::do_nothing(dht::peer) { }

/// @brief getter for own ID
dht::hash_t node::own_id() { return id; }

/// @brief send a ping to a peer, callbacks will have no data to process
/// @param p peer struct
/// @param ok_fn success callback
/// @param bad_fn failure callback
void node::ping(dht::peer p, c_callback ok_fn, c_callback bad_fn) {
    send(p, dht::proto::actions::ping, 0,
        [ok_fn, this](std::future<std::string> fut, dht::peer p_, dht::pend_it pit) {
            okay<dht::proto::actions::ping>(std::move(fut), p_, pit);
            ok_fn(p_);
        },
        [bad_fn, this](std::future<std::string> fut, dht::peer p_, dht::pend_it pit) {
            bad<dht::proto::actions::ping>(std::move(fut), p_, pit);
            bad_fn(p_);
        });
}

/// @brief find closest nodes to peer, callbacks will return bucket
/// @param p peer struct
/// @param h hash of peer
/// @param ok_fn success callback
/// @param bad_fn failure callback
void node::find_node(dht::peer p, dht::hash_t h, bkt_callback ok_fn, c_callback bad_fn) {
    dht::id_t a, m_id;
    dht::util::btoh(h, a);
    dht::util::msg_id(reng, m_id);

    dht::hash_t h_ = send(p, dht::proto::actions::find_node, dht::proto::NL, 
        [&](std::future<std::string> fut, dht::peer, dht::pend_it pit) {
            using namespace dht;
            OBTAIN_FUT_MSG;

            // we now have an id for this peer
            p.id = util::htob(m.id);
        },
        [bad_fn, this](std::future<std::string> fut, dht::peer p_, dht::pend_it pit) {
            bad<dht::proto::actions::find_node>(std::move(fut), p_, pit);
            bad_fn(p_);
        });

    std::string a_;
    a_.resize(dht::proto::NL * sizeof(u32));
    std::memcpy((void*)a_.c_str(), a, dht::proto::NL * sizeof(u32));

    rp_node_.send(p, h_, a_, se_do_nothing, se_do_nothing);

    queue_current(p, h_, dht::proto::actions::find_node, true,
        [h_, ok_fn, bad_fn, this](std::future<std::string> fut_, dht::peer p_, dht::pend_it pit_) {
            std::stringstream ss;
            std::string f = fut_.get();
            dht::bucket bkt;

            ss << f;

            try {
                {
                    boost::archive::binary_iarchive bia(ss);
                    bia >> bkt;
                }

                // send an ack back
                reply(p_, dht::proto::actions::ack, h_, 0, p_do_nothing, p_do_nothing);

                ok_fn(p_, std::move(bkt));
            } catch (std::exception& e) {
                spdlog::error("could not deserialize bucket, ec: {}", e.what());
                bad<dht::proto::actions::find_node>(std::move(fut_), p_, pit_);
                bad_fn(p_);
            }
        },
        [bad_fn, this](std::future<std::string> fut_, dht::peer p_, dht::pend_it pit_) {
            bad<dht::proto::actions::find_node>(std::move(fut_), p_, pit_);
            bad_fn(p_);
        }
    );
}

/*
 * lookup algorithm:
 * 
 * - have a set of visited nodes
 * - have a current set of closest nodes
 * 
 * 1. pick a nodes from closest local bucket
 * 2. for each node picked
 *    2a. add to visited node list
 *    2b. perform a find_node of the target on node
 *      2b.a. with list of k nodes, pick a closest nodes that aren't in visited
 *      2b.b. with list of a closest nodes, have as current closest node list candidate and goto 3
 * 3. if list candidate features nodes no closer than current set of closest nodes, terminate lookup
 * 4. otherwise, set current set of closest nodes to candidate and commence another round with new 
 *    closest nodes list instead of local bucket
 * 
 * THIS IS BLOCKING!!!
 */
dht::bucket node::lookup(dht::hash_t target_id) {
    using namespace dht;

    std::list<peer> visited;
    bucket closest;

    {
        std::lock_guard<std::mutex> l(table.mutex);
        closest = table.find_bucket(peer(target_id));
    }

    while(true) {
        // base case: there are no close nodes 
        if(closest.empty())
            return closest;

        spdlog::warn("new round");

        std::list<std::future<bucket>> tasks;
        std::list<bucket> responses;
        bucket candidate;

        for(peer p : closest) {
            spdlog::info("contacting {}:{}:{}...", p.addr, p.port, p.reply_port);

            visited.push_back(p);

            tasks.push_back(std::async(
                std::launch::async,
                [&](peer p_) {
                    std::promise<bucket> prom;
                    std::future<bucket> fut = prom.get_future();

                    find_node(p_, target_id,
                        [&, pr = std::make_shared<std::promise<bucket>>(std::move(prom))](peer r, bucket bkt) {
                            spdlog::info("results of find_node from {}:{}:{}:", r.addr, r.port, r.reply_port);
                            for(auto c : bkt)
                                spdlog::info("\t peer {}:{}:{} id {} distance {}", c.addr, c.port, c.reply_port, util::htos(c.id), peer(c.id).distance(target_id).to_string());
                            pr->set_value(std::move(bkt));
                        },
                        se_do_nothing);

                    switch(fut.wait_for(std::chrono::seconds(proto::T))) {
                    case std::future_status::ready:
                        return fut.get();
                    default:
                        return bucket{};
                    }
                },
                p
            ));
        }

        for(auto&& t : tasks)
            responses.push_back(t.get());

        responses.remove_if([&](bucket a) { return a.empty(); });

        if(responses.empty())
            break;

        candidate = *std::min_element(responses.begin(), responses.end(), 
            [&](bucket a, bucket b) { return a.closer(b, target_id); });

        // how do we really know when to remove ourselves?
        // further, is it even necessary?
        candidate.remove_if([&](peer a) { 
            return std::count(visited.begin(), visited.end(), a) != 0 || 
                a.id == id; });

        if(candidate.empty())
            break;

        spdlog::info("candidates:");

        for(auto c : candidate)
            spdlog::info("\t candidate {}:{}:{} id {} distance {}", c.addr, c.port, c.reply_port, util::htos(c.id), peer(c.id).distance(target_id).to_string());

        spdlog::info("closest:");

        for(auto c : closest)
            spdlog::info("\t closest {}:{}:{} id {} distance {}", c.addr, c.port, c.reply_port, util::htos(c.id), peer(c.id).distance(target_id).to_string());

        if(candidate.closer(closest, target_id)) {
            spdlog::info("new candidate");
            closest = candidate;
        } else break;
    }

    return closest;
}

}