#include "network.h"
#include "bucket.h"
#include "routing.h"

namespace tulip {
namespace dht {

/////////////////////////////////////////////////
/// Misc.
/////////////////////////////////////////////////

/// @private
/// @brief initialize a pending item
/// @param req_ peer struct
/// @param msg_id_ msg_id hash
/// @param a action
pending_item::pending_item(peer req_, hash_t msg_id_, proto::actions a, bool rp_) : 
    req(req_), msg_id(msg_id_), action(a), satisfied(false), rp(rp_) { }

/////////////////////////////////////////////////
/// TCP functions
/////////////////////////////////////////////////

rp_node_peer::rp_node_peer(rp_node& rp_node__, tcp::socket sock) :
    rp_node_(rp_node__), socket(std::move(sock)), endpoint(socket.remote_endpoint()) { }

rp_node_peer::rp_node_peer(rp_node& rp_node__, tcp::endpoint ep) :
    rp_node_(rp_node__), socket(rp_node__.ioc), endpoint(ep) { }

void rp_node_peer::handle() {
    auto self(shared_from_this());

    boost::asio::async_read(
        socket, 
        boost::asio::buffer((void*)&m_in, sizeof(proto::rp_msg)),
        [this, self](boost::system::error_code ec, std::size_t sz) {
            hash_t id_ = util::htob(m_in.id);

            if(ec || 
                sz != sizeof(proto::rp_msg) ||
                std::memcmp(m_in.magic, proto::consts.magic, proto::magic_length) ||
                m_in.sz > proto::max_data_size ||
                id_ == hash_t(0)) { 
                return; 
            }

            spdlog::debug("rp_msg from {}:{} (mp: {}) msg {}", 
                endpoint.address().to_string(), 
                endpoint.port(), 
                m_in.msg_port, 
                util::htos(util::htob(m_in.msg_id)));

            peer req(endpoint.address().to_string(), m_in.msg_port, m_in.reply_port, id_);

            auto it = rp_node_.node_.pending.end();

            {
                LOCK(rp_node_.node_.pending_mutex);

                it = std::find_if(rp_node_.node_.pending.begin(), rp_node_.node_.pending.end(),
                    [&](pending_item& i) { 
                        return i.req == req && 
                        i.msg_id == util::htob(m_in.msg_id) && 
                        i.rp == true; });
            }

            // ignore if data isn't even in pending list
            if(it == rp_node_.node_.pending.end())
                return; 

            buf.clear();
            buf.resize(m_in.sz);

            boost::asio::async_read(
                socket,
                boost::asio::buffer(buf),
                [this, self, it](boost::system::error_code ec, std::size_t sz) {
                    if((!ec || ec == boost::asio::error::eof) && sz == m_in.sz) {
                        LOCK(rp_node_.node_.pending_mutex);

                        if(!it->satisfied)
                            it->promise.set_value(buf);

                        it->satisfied = true;
                    }
                });
        });
}

void rp_node_peer::write(
    proto::rp_msg out, 
    std::string s, 
    peer req,
    se_callback ok_fn, 
    se_callback bad_fn) {
    auto self(shared_from_this());

    tcp::endpoint ep = req.rp_to_tcp_endpoint();

    socket.async_connect(ep, [this, self, out, s, req, ok_fn, bad_fn](boost::system::error_code er) {
        if(er) {
            spdlog::error("could not connect to rp, ec: {}", er.message());
            bad_fn(req);
            return;
        }

        boost::asio::async_write(socket, 
            boost::asio::buffer((void*)&out, sizeof(proto::rp_msg)),
            [this, self, s, req, ok_fn, bad_fn](boost::system::error_code ec, std::size_t) {
                if(ec) { 
                    spdlog::error("could not send hdr to rp {}:{}, ec: {}", endpoint.address().to_string(), endpoint.port(), ec.message()); 
                    bad_fn(req);
                    return;
                }

                boost::asio::async_write(socket, boost::asio::buffer(s),
                    [this, ok_fn, bad_fn, req](boost::system::error_code ec_, std::size_t sz) {
                        if(ec_) { 
                            spdlog::error("could not send data to rp {}:{}, ec: {}", endpoint.address().to_string(), endpoint.port(), ec_.message()); 
                            bad_fn(req);
                            return;
                        }

                        ok_fn(req);
                    }
                );
            });
    });
}

/////////////////////////////////////////////////
/// TCP server functions
/////////////////////////////////////////////////

/// @private
/// @brief initialize TCP socket 
/// @param ioc_ io_context reference
/// @param pending_mutex_ mutex reference
/// @param pending_ pending list reference
/// @param port_ port to bind to (cannot be same port as UDP)
rp_node::rp_node(node& node__, u16 port_) :
    port(port_),
    node_(node__),
    acceptor(ioc, tcp::endpoint(tcp::v4(), port_)) { }

rp_node::~rp_node() {
    ioc_thread.join();
}

void rp_node::loop() {
    acceptor.async_accept([&, this](boost::system::error_code ec, tcp::socket sock) {
        if(!ec) {
            std::make_shared<rp_node_peer>(*this, std::move(sock))->handle();
        }

        loop();
    });
}

void rp_node::start() {
    loop();
    ioc_thread = std::thread([&, this]() { 
        auto w = boost::asio::make_work_guard(ioc);
        ioc.run(); 
    });
}

void rp_node::send(
    peer req, 
    hash_t msg_id, 
    std::string s,
    se_callback ok_fn, 
    se_callback bad_fn) {

    boost::system::error_code ec;

    std::size_t sz = s.length();
    proto::rp_msg out = {
        .ack = 0,
        .msg_port = node_.port,
        .reply_port = port,
        .sz = sz
    };

    id_t msg_id_ = {0}, id_ = {0};
    util::btoh(msg_id, msg_id_);
    util::btoh(node_.id, id_);

    std::memcpy(out.magic, proto::consts.magic, proto::magic_length);
    std::memcpy(out.id, id_, proto::u32_hash_width * sizeof(u32));
    std::memcpy(out.msg_id, msg_id_, proto::u32_hash_width * sizeof(u32));

    try {
        std::make_shared<rp_node_peer>(*this, req.rp_to_tcp_endpoint())->write(
            std::move(out),
            std::move(s),
            std::move(req),
            std::move(ok_fn),
            std::move(bad_fn)
        );
    } catch (std::exception& e) {
        spdlog::error("rp_node write exception: {}", e.what());
        bad_fn(req);
    }
}

/////////////////////////////////////////////////
/// UDP node
/////////////////////////////////////////////////

/// @brief initialize a new node
/// @param port_ UDP port number
/// @param rp_port TCP port number
node::node(u16 port_, u16 rp_port) :
    port(port_), 
    socket(ioc, udp::endpoint(udp::v4(), port_)), 
    table(id, *this),
    rp_node_(*this, rp_port),
    reng(rd()) {
    spdlog::info("messaging at addr {} on port {}, data transfer on port {}", 
        socket.local_endpoint().address().to_string(), port, rp_port);
    id = table.id = util::gen_id(reng);
    spdlog::info("node id: {}", util::htos(id));
}

node::~node() {
    ioc_thread.join();
}

void node::start() {
    loop();
    ioc_thread = std::thread([&, this]() { ioc.run(); });
}

/////////////////////////////////////////////////
/// UDP callbacks
/////////////////////////////////////////////////

template <>
void node::okay<proto::actions::ping>(std::future<std::string> fut, pending_result res) {
    OBTAIN_FUT_MSG;
                        
    if(!std::memcmp(m.magic, proto::consts.magic, proto::magic_length) &&
        m.action == proto::actions::ping &&
        m.reply == proto::context::response) {
        {
            // we now have an id for this peer
            res.req.id = util::htob(m.id);
            
            LOCK(table.mutex);
            table.update(res.req);
        }

        spdlog::debug("responded, updating");
    }
}

template <>
void node::bad<proto::actions::ping>(std::future<std::string> fut, pending_result res) {
    {
        LOCK(table.mutex);
        int s;
        if((s = table.stale(res.req)) > proto::missed_pings_allowed) {
            table.evict(res.req);
            spdlog::debug("did not respond, evicting {}", util::htos(res.req.id));
        } else if(s != -1) {
            spdlog::debug("did not respond. staleness: {}", s);
        }
    }
}

template <>
void node::bad<proto::actions::find_node>(std::future<std::string> fut, pending_result res) {
    spdlog::debug("node {}:{}:{} id {} did not send find_node information", 
        res.req.addr, res.req.port, res.req.reply_port, util::htos(res.req.id));
}

/////////////////////////////////////////////////
/// UDP node functions
/////////////////////////////////////////////////

/// @private
/// @brief add message to pending list and start a thread to check for response
/// @param req peer struct
/// @param it iterator to item in pending list
/// @param ok_fn callback for success
/// @param bad_fn callback for failure
void node::wait(
    pend_it it,
    p_callback ok_fn,
    p_callback bad_fn) {
    try {
        pending_result res(*it);

        {
            LOCK(pending_mutex);
            if(it == pending.end()) {
                spdlog::error("wait: bad iterator!");
                bad_fn(std::future<std::string>(), res);
                return;
            }
        }

        std::future<std::string> fut = it->promise.get_future();
        std::future_status fs = fut.wait_for(seconds(proto::net_timeout));

        {
            LOCK(pending_mutex);
            pending.erase(it);
        }
        
        switch(fs) {
        case std::future_status::ready:
            ok_fn(std::move(fut), res);
            break;

        case std::future_status::deferred:
        case std::future_status::timeout:
            bad_fn(std::move(fut), res);
            break;
        }
    } catch(std::future_error& e) {
        if(e.code() == std::future_errc::future_already_retrieved) {
            LOCK(table.mutex);
            table.update_pending(it->req);

            spdlog::debug("already responded, updating node {}", util::htos(it->req.id));
        }
    }
}

/// @private
/// @brief await current peer's response
/// @param req peer struct
/// @param msg_id message id
/// @param a action to await under
/// @param rp is this on reply port (TCP, true) or message port (UDP, false) ?
/// @param ok_fn success callback
/// @param bad_fn failure callback
void node::await(
    peer req, 
    hash_t msg_id, 
    proto::actions a, 
    bool rp,
    p_callback ok_fn, 
    p_callback bad_fn) {
    LOCK(pending_mutex);

    pending.emplace_back(req, msg_id, a, rp);
    auto pit = pending.end();
    pit--;

    std::thread(
        &node::wait, 
        this, 
        pit, 
        ok_fn,
        bad_fn).detach();
}


/// @private
/// @brief await an ack response
/// @param req peer struct
/// @param msg_id message id
void node::await_ack(peer req, hash_t msg_id) {
    // NOTE: we don't really need an ACK packet, just to ensure the data was sent completely for UI
    await(req,
        util::htob(m_in.msg_id),
        proto::actions::ack,
        false,
        [this](std::future<std::string> fut, pending_result res) {
            LOCK(pending_mutex);
            OBTAIN_FUT_MSG;
            
            // we have a proper ack?
            if(!std::memcmp(m.magic, proto::consts.magic, proto::magic_length) &&
                m.action == proto::actions::ack &&
                m.reply == proto::context::response) {
                spdlog::debug("we got a proper ack back");
            } else {
                spdlog::debug("we didnt get a proper ack back");
            }
        },
        [this](std::future<std::string> fut, pending_result res) {
            spdlog::error("we did not get an ack back or the peer sent bad data for find_node");
        });
}

/// @private
/// @brief send whatever's in m_out to peer
/// @param q do we await?
/// @param req peer struct
/// @param a action
/// @param ok_fn success callback
/// @param bad_fn failure callback
/// @return msg id of sent message
hash_t node::send(
    bool q,
    proto::context c, 
    peer req, 
    proto::actions a, 
    p_callback ok_fn, 
    p_callback bad_fn) {
    hash_t orig_id(0);

    LOCK(m_out_mutex);    
    
    orig_id = util::htob(m_out.msg_id);

    if(q)
        await(req, orig_id, a, false, ok_fn, bad_fn);

    socket.async_send_to(
        boost::asio::buffer(&m_out, sizeof(proto::msg)), 
        req.to_udp_endpoint(),
        ba_do_nothing);
    
    return orig_id;
}

/// @private
/// @brief send a message over UDP to a peer
/// @param q do we await a response?
/// @param c what context is this? (request, response)
/// @param r response code
/// @param req peer struct
/// @param a request action
/// @param sz size of payload (0 for messages only)
/// @param ok_fn callback for success
/// @param bad_fn callback for failure (timeout, error, etc.)
/// @return msg id of sent message
hash_t node::send(
    bool q, 
    proto::context c, 
    proto::responses r,
    peer req, 
    proto::actions a, 
    u64 sz, 
    p_callback ok_fn, 
    p_callback bad_fn) {
    id_t id_ = {0}; util::btoh(id, id_);
    
    {
        LOCK(m_out_mutex); 

        m_out = {
            .action = (u8)a,
            .reply = (u8)c,
            .response = (u8)r,
            .msg_port = port,
            .reply_port = rp_node_.port,
            .sz = sz
        };
    
        std::memcpy(m_out.magic, proto::consts.magic, proto::magic_length);
        std::memcpy(m_out.id, id_, proto::u32_hash_width * sizeof(u32));
        util::msg_id(reng, m_out.msg_id);
    }

    return send(q, c, req, a, ok_fn, bad_fn);
}

/// @private
/// @brief send a message over UDP to a peer with a specified message ID
/// @param q do we await a response?
/// @param c what context is this? (request, response)
/// @param r response code
/// @param req peer struct
/// @param a request action
/// @param sz size of payload (0 for messages only)
/// @param msg_id message ID
/// @param ok_fn callback for success
/// @param bad_fn callback for failure (timeout, error, etc.)
/// @return msg id of sent message
hash_t node::send(
    bool q, 
    proto::context c, 
    proto::responses r,
    peer req, 
    proto::actions a, 
    u64 sz, 
    hash_t msg_id,
    p_callback ok_fn, 
    p_callback bad_fn) {
    id_t id_ = {0}, msg_id_ = {0}; 
    
    util::btoh(id, id_);
    util::btoh(msg_id, msg_id_);

    {
        LOCK(m_out_mutex); 

        m_out = {
            .action = (u8)a,
            .reply = (u8)c,
            .response = (u8)r,
            .msg_port = port,
            .reply_port = rp_node_.port,
            .sz = sz
        };
    
        std::memcpy(m_out.magic, proto::consts.magic, proto::magic_length);
        std::memcpy(m_out.id, id_, proto::u32_hash_width * sizeof(u32));
        std::memcpy(m_out.msg_id, msg_id_, proto::u32_hash_width * sizeof(u32));
    }

    return send(q, c, req, a, ok_fn, bad_fn);
}

/////////////////////////////////////////////////
/// UDP node message handlers
/////////////////////////////////////////////////

/// @brief reply to a ping request
/// @param req peer struct
template <>
void node::reply<proto::actions::ping>(peer req) {
    send(
        false, 
        proto::context::response, 
        proto::responses::ok, 
        req, 
        proto::actions::ping,
        0, 
        util::htob(m_in.msg_id),
        p_do_nothing, 
        p_do_nothing);

    table.update(req);
}

/// @brief reply to a find_node request
/// @param req peer struct
template <>
void node::reply<proto::actions::find_node>(peer req) {
    // send first msg ack back
    send(
        false, 
        proto::context::response, 
        proto::responses::ok, 
        req, 
        proto::actions::find_node,
        0, 
        util::htob(m_in.msg_id),
        p_do_nothing, 
        p_do_nothing);

    // drop request if there's no tcp data to recv
    if(m_in.sz <= 0)
        return;

    hash_t orig_id = util::htob(m_in.msg_id);

    // await target ID over TCP
    await(
        req,
        util::htob(m_in.msg_id),
        proto::actions::find_node,
        true,
        [this, orig_id](std::future<std::string> fut, pending_result res) {
            std::stringstream ss;

            id_t id_ = {0};

            std::string v = fut.get();
            std::string s(v.begin(), v.end()), str{};

            // TODO: check if node id is good
            if(s.size() != proto::u32_hash_width * sizeof(u32))
                return;

            std::memcpy((void*)id_, (void*)s.c_str(), proto::u32_hash_width * sizeof(u32));

            {
                boost::archive::binary_oarchive boa(ss);
                LOCK(table.mutex);
                bucket bkt = table.find_bucket(peer(util::htob(id_)));
                boa << bkt;
            }

            u64 sz = ss.tellp();
            str = ss.str();

            spdlog::debug("looked up node {}", util::htos(util::htob(id_)));

            // send udp response detailing payload size
            send(
                true,
                proto::context::response,
                proto::responses::ok,
                res.req, 
                proto::actions::find_node,
                sz,
                res.msg_id,
                [this, orig_id, str](std::future<std::string> fut, dht::pending_result res) {
                    // this callback means we got a response ack back
                    rp_node_.send(res.req, orig_id, str, se_do_nothing, se_do_nothing);
                    
                    // await a final ack
                    await_ack(res.req, orig_id);
                },
                p_do_nothing);
        },
    std::bind(&node::bad<proto::actions::find_node>, this, _1, _2));

    // update peer in table regardless
    {
        LOCK(table.mutex);
        table.update(req);
    }
}

/////////////////////////////////////////////////
/// UDP node dispatcher
/////////////////////////////////////////////////

/// @brief event loop for UDP socket
void node::loop() {
    socket.async_receive_from(boost::asio::buffer((void*)&m_in, sizeof(proto::msg)), client,
        [this](boost::system::error_code ec, std::size_t sz) {
            if((!ec || ec == boost::asio::error::eof) && sz == sizeof(proto::msg)) {
                LOCK(m_in_mutex);

                hash_t id_ = util::htob(m_in.id);

                // correct magic, reply port is valid
                if(std::memcmp(m_in.magic, proto::consts.magic, proto::magic_length) ||
                    id_ == hash_t(0))
                    goto o;

#define r(a) case proto::actions::a: reply<proto::actions::a>(req); break;

                peer req(client.address().to_string(), m_in.msg_port, m_in.reply_port, id_);

                spdlog::debug("msg from {}:{} (rp: {}) id {} msg {} act {} resp? {}", 
                    req.addr, req.port, req.reply_port, 
                    util::htos(req.id), util::htos(util::htob(m_in.msg_id)), m_in.action, (bool)m_in.reply);

                if (m_in.reply == proto::context::response) {
                    LOCK(pending_mutex);

                    // fulfill pending promise, if any
                    auto it = std::find_if(pending.begin(), pending.end(),
                        [&](pending_item& i) {
                            return i.req == req && 
                                i.msg_id == util::htob(m_in.msg_id) && 
                                i.action == m_in.action &&
                                i.rp == false &&
                                !i.satisfied; });

                    if(it != pending.end()) {
                        std::string v;
                        v.resize(sizeof(proto::msg));
                        std::memcpy((void*)v.data(), &m_in, sizeof(proto::msg));

                        it->req = req;
                        it->promise.set_value(std::move(v));
                        it->satisfied = true;

                        // if you have a pending request, we will ignore you
                        if(m_in.reply == proto::context::request)
                            goto o;
                    }
                } else {
                    try {
                        switch(m_in.action) {
                        r(ping) r(find_node)
                        }
                    } catch(std::exception& e) {
                        spdlog::error("exception caught: {}", e.what());
                    }
                }

#undef r
            }

            o: loop();
        });
}

#undef MAKE_MSG
}
}