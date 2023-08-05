#include "dht.h"

namespace tulip {

/////////////////////////////////////////////////
/// accessible node interface
/////////////////////////////////////////////////

/// @brief initialize tulip node
node::node() :
    node(dht::proto::Mport, dht::proto::Rport) { start(); rp_node_.start(); }

node::node(u16 m, u16 r) :
    dht::node(m, r) { start(); rp_node_.start(); }

/// @brief do nothing
void node::do_nothing(dht::peer) { }

/// @brief getter for own ID
dht::hash_t node::own_id() { return id; }

/// @brief send a ping to a peer, callbacks will have no data to process
/// @param p peer struct
/// @param ok_fn success callback
/// @param bad_fn failure callback
void node::ping(dht::peer p, c_callback ok_fn, c_callback bad_fn) {
    using namespace dht;

    send(true, 
        proto::context::request, 
        proto::responses::ok, 
        p, 
        proto::actions::ping, 
        0,
        [ok_fn, this](std::future<std::string> fut, pending_result res) {
            okay<proto::actions::ping>(std::move(fut), res);
            ok_fn(res.req);
        },
        [bad_fn, this](std::future<std::string> fut, pending_result res) {
            bad<proto::actions::ping>(std::move(fut), res);
            bad_fn(res.req);
        });
}

/// @brief find closest nodes to peer, callbacks will return bucket
/// @param p peer struct
/// @param h hash of peer
/// @param ok_fn success callback
/// @param bad_fn failure callback
void node::find_node(dht::peer p, dht::hash_t h, bkt_callback ok_fn, c_callback bad_fn) {
    using namespace dht;

    id_t a, m_id;
    util::btoh(h, a);
    util::msg_id(reng, m_id);

    // send initial request and await a first ack response
    hash_t h_ = send(
        true,
        proto::context::request,
        proto::responses::ok,
        p, 
        proto::actions::find_node, 
        proto::u32_hash_width, 
        [this, a, ok_fn, bad_fn](std::future<std::string> fut, pending_result res) {
            using namespace dht;
            OBTAIN_FUT_MSG;
        
            // prepare target ID buffer
            std::string a_;
            a_.resize(proto::u32_hash_width * sizeof(u32));
            std::memcpy((void*)a_.c_str(), a, proto::u32_hash_width * sizeof(u32));
         
            // send target ID to find over TCP
            peer r = res.req;
            hash_t m_id = res.msg_id;

            // await a UDP response detailing size
            await(r, m_id, proto::actions::find_node, false,
                [this, ok_fn, bad_fn](std::future<std::string> fut, pending_result res) {
                    // await the TCP response with bucket
                    await(res.req, res.msg_id, proto::actions::find_node, true,
                        [this, ok_fn, bad_fn](std::future<std::string> fut, pending_result res) {
                            // reply with an ack
                            send(
                                false,
                                proto::context::response,
                                proto::responses::ok,
                                res.req, 
                                proto::actions::ack, 
                                0, 
                                res.msg_id, 
                                p_do_nothing, 
                                p_do_nothing);

                            std::stringstream ss;
                            std::string f = fut.get();
                            bucket bkt;

                            ss << f;

                            try {
                                {
                                    boost::archive::binary_iarchive bia(ss);
                                    bia >> bkt;
                                }

                                ok_fn(res.req, std::move(bkt));
                            } catch (std::exception& e) {
                                spdlog::error("could not deserialize bucket, ec: {}", e.what());
                                bad<proto::actions::find_node>(std::move(fut), res);
                                bad_fn(res.req);
                            }
                        },
                        [this, ok_fn, bad_fn](std::future<std::string> fut, pending_result res) {
                            spdlog::error("did not get tcp response with bucket from {}:{}:{}", res.req.addr, res.req.port, res.req.reply_port);
                            bad<proto::actions::find_node>(std::move(fut), res);
                            bad_fn(res.req);
                        });
                        
                    // send second ack response 
                    send(
                        false, 
                        proto::context::response,
                        proto::responses::ok,
                        res.req, 
                        proto::actions::find_node, 
                        0,
                        res.msg_id, 
                        p_do_nothing, 
                        p_do_nothing);
                },
                [this, ok_fn, bad_fn](std::future<std::string> fut, pending_result res) {
                    spdlog::error("did not get udp response detailing size from {}:{}:{}", res.req.addr, res.req.port, res.req.reply_port);
                    bad<proto::actions::find_node>(std::move(fut), res);
                    bad_fn(res.req);
                });

            // send target ID over tcp
            rp_node_.send(res.req, res.msg_id, a_, se_do_nothing, se_do_nothing);
        },
        [bad_fn, this](std::future<std::string> fut, pending_result res) {
            spdlog::error("did not get first ack from udp from {}:{}:{}", res.req.addr, res.req.port, res.req.reply_port);
            bad<proto::actions::find_node>(std::move(fut), res);
            bad_fn(res.req);
        });
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
        LOCK(table.mutex);
        closest = table.find_bucket(peer(target_id));
    }

    while(true) {
        // base case: there are no close nodes 
        if(closest.empty())
            return closest;

        std::list<std::future<bucket>> tasks;
        std::list<bucket> responses;
        bucket candidate;

        for(peer p : closest) {
            visited.push_back(p);

            tasks.push_back(std::async(
                std::launch::async,
                [&](peer p_) {
                    std::promise<bucket> prom;
                    std::future<bucket> fut = prom.get_future();

                    find_node(p_, target_id,
                        [&, pr = std::make_shared<std::promise<bucket>>(std::move(prom))](peer r, bucket bkt) {
                            pr->set_value(std::move(bkt));
                        },
                        se_do_nothing);

                    switch(fut.wait_for(std::chrono::seconds(proto::net_timeout))) {
                    case std::future_status::ready:
                        return fut.get();
                    default:
                        return bucket{};
                    }
                },
                p
            ));
        }

        for(auto&& t : tasks) {
            responses.push_back(t.get());
        }

        responses.remove_if([&](bucket a) { return a.empty(); });

        if(responses.empty())
            break;

        candidate = *std::min_element(responses.begin(), responses.end(), 
            [&](bucket a, bucket b) { return a.closer(b, target_id); });

        // how do we really know when to remove ourselves?
        // furthermore, is it even necessary?
        candidate.remove_if([&](peer a) { 
            return std::count(visited.begin(), visited.end(), a) != 0 || 
                a.id == id; });

        if(candidate.empty())
            break;

        if(candidate.closer(closest, target_id)) {
            closest = candidate;
        } else break;
    }

    return closest;
}

}