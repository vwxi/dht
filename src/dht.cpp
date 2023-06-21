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
    sha1::digest_type a;
    dht::util::btoh(h, a);
    
    dht::hash_t h_ = send(p, dht::proto::actions::find_node, dht::proto::NL, 
        p_do_nothing,
        [bad_fn, this](std::future<std::string> fut, dht::peer p_, dht::pend_it pit) {
            bad<dht::proto::actions::find_node>(std::move(fut), p_, pit);
            bad_fn(p_);
        });

    std::string a_;
    a_.resize(dht::proto::NL * sizeof(unsigned int));
    std::memcpy((void*)a_.c_str(), a, dht::proto::NL * sizeof(unsigned int));

    rp_node_.send(p, h_, a_, se_do_nothing, se_do_nothing);

    queue_current(p, h_, dht::proto::actions::find_node, true,
        [h_, ok_fn, this](std::future<std::string> fut_, dht::peer p_, dht::pend_it pit_) {
            std::stringstream ss;
            std::string f = fut_.get();
            dht::bucket bkt;

            ss << f;

            {
                boost::archive::binary_iarchive bia(ss);
                bia >> bkt;
            }

            // send an ack back
            reply(p_, dht::proto::actions::ack, h_, 0, p_do_nothing, p_do_nothing);

            ok_fn(p_, std::move(bkt));
        },
        [bad_fn, this](std::future<std::string> fut_, dht::peer p_, dht::pend_it pit_) {
            bad<dht::proto::actions::find_node>(std::move(fut_), p_, pit_);
            bad_fn(p_);
        }
    );
}

}