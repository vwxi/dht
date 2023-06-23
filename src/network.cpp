#include "network.h"
#include "bucket.h"
#include "routing.h"

namespace tulip {
namespace dht {

/////////////////////////////////////////////////
/// Misc.
/////////////////////////////////////////////////

/// @private
/// @brief private macro to fill message struct
#define MAKE_MSG(a,R,r) \
    m_out = { \
        .action = proto::actions::a, \
        .reply = proto::context::R, \
        .response = proto::responses::r, \
        .msg_port = port, \
        .reply_port = rp_node_.port, \
        .sz = 0 \
    }; \
    id_t id_ = {0}; util::btoh(id, id_); \
    std::memcpy(m_out.magic, proto::consts.magic, proto::ML); \
    std::memcpy(m_out.id, id_, proto::NL * sizeof(u32)); \
    std::memcpy(m_out.msg_id, m_in.msg_id, proto::NL * sizeof(u32));

/// @private
/// @brief initialize a pending item
/// @param req_ peer struct
/// @param msg_id_ msg_id hash
/// @param a queued action
pending_item::pending_item(peer req_, hash_t msg_id_, proto::actions a, bool rp_) : 
    req(req_), msg_id(msg_id_), action(a), satisfied(false), rp(rp_) { }

/////////////////////////////////////////////////
/// TCP functions
/////////////////////////////////////////////////

/// @private
/// @brief initialize TCP peer
/// @param peers_mutex_ mutex reference
/// @param peers_ peer list reference
/// @param sock socket object
rp_node_peer::rp_node_peer(
    rp_node& rp_node__,
    tcp::socket sock) :
    rp_node_(rp_node__),
    socket(std::move(sock)) { }

/// @private
/// @brief kill peer connection
void rp_node_peer::kill() {
    try {
        socket.shutdown(socket.shutdown_both);
        socket.close();
        rp_node_.peers.erase(shared_from_this());
    } catch (std::exception& e) { }
}

/// @private
/// @brief handle peer connection
void rp_node_peer::handle() {
    auto self(shared_from_this());

    std::memset((void*)&m_in, 0, sizeof(proto::rp_msg));

    boost::asio::async_read(
        socket, 
        boost::asio::buffer((void*)&m_in, sizeof(proto::rp_msg)),
        std::bind(&rp_node_peer::read_handler, shared_from_this(), _1, _2));
}

/// @private
/// @brief just a handler
void rp_node_peer::read_handler(const boost::system::error_code& ec, std::size_t sz) {
    // correct magic, under size limit, messaging port is valid
    hash_t id_ = util::htob(m_in.id);

    if(ec || 
        sz != sizeof(proto::rp_msg) ||
        std::memcmp(&m_in.magic, proto::consts.magic, proto::ML) ||
        m_in.sz > proto::MS ||
        !(m_in.msg_port > 0 && m_in.msg_port < 65536) ||
        !(m_in.reply_port > 0 && m_in.reply_port < 65536) ||
        id_ == hash_t(0)) { 
        kill();
        return; }

    tcp::endpoint ep = socket.remote_endpoint();

    rp_node_.peers.insert(shared_from_this());

    spdlog::debug("rp_msg from {}:{} (mp: {}) msg {}", 
        ep.address().to_string(), 
        ep.port(), 
        m_in.msg_port, 
        util::htos(util::htob(m_in.msg_id)));

    peer req(socket.remote_endpoint().address().to_string(), m_in.msg_port, m_in.reply_port, id_);

    auto it = rp_node_.node_.pending.end();
    
    {
        std::lock_guard<std::mutex> g(rp_node_.node_.pending_mutex);

        it = std::find_if(rp_node_.node_.pending.begin(), rp_node_.node_.pending.end(),
            [&](pending_item& i) { 
                return i.req == req && 
                i.msg_id == util::htob(m_in.msg_id) && 
                i.rp == true; });
    }

    // ignore if data isn't even in pending list
    if(it == rp_node_.node_.pending.end()) { 
        kill();
        return; }

    buf.clear();
    buf.resize(m_in.sz);

    boost::asio::async_read(
        socket,
        boost::asio::buffer(buf),
        std::bind(&rp_node_peer::read_data_handler, shared_from_this(), _1, _2, it));
}

/// @private
/// @brief just a handler
void rp_node_peer::read_data_handler(
    const boost::system::error_code& ec, 
    std::size_t sz, 
    pend_it it) {
    if((!ec || ec == boost::asio::error::eof) && sz == m_in.sz) {
        std::lock_guard<std::mutex> g(rp_node_.node_.pending_mutex);

        if(!it->satisfied)
            it->promise.set_value(buf);
        it->satisfied = true;
    } else {
        kill();
    }
}

/// @private
/// @brief write to socket
void rp_node_peer::write(
    proto::rp_msg out, 
    std::string s, 
    peer req,
    se_callback ok_fn, 
    se_callback bad_fn) {
    boost::asio::async_write(socket, 
        boost::asio::buffer((void*)&out, sizeof(proto::rp_msg)),
        std::bind(&rp_node_peer::write_handler, 
            shared_from_this(), _1, _2, 
            std::move(s), std::move(req), std::move(ok_fn), std::move(bad_fn))
    );
}

/// @private
/// @brief just a handler
void rp_node_peer::write_handler(
    const boost::system::error_code& ec, 
    std::size_t sz,
    std::string s, 
    peer req,
    se_callback ok_fn, 
    se_callback bad_fn) {
    if(ec) { 
        spdlog::error("could not send hdr to rp, ec: {}", ec.message()); 
        kill();
        bad_fn(req);
        return;
    }

    boost::asio::async_write(socket, boost::asio::buffer(s),
        [this, ok_fn, bad_fn, req](boost::system::error_code ec_, std::size_t) {
            if(ec_) { 
                spdlog::error("could not send data to rp, ec: {}", ec_.message()); 
                kill();
                bad_fn(req);
                return;
            }

            ok_fn(req);
            kill();
        }
    );
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
rp_node::rp_node(
    boost::asio::io_context& ioc_, 
    node& node__,
    u16 port_) :
    port(port_),
    ioc(ioc_),
    node_(node__),
    acceptor(ioc_, tcp::endpoint(tcp::v4(), port_)) { }

/// @private
/// @brief main event loop for TCP socket
void rp_node::run() {
    acceptor.async_accept(
        [this](boost::system::error_code ec, tcp::socket sock) {
            if(!ec)
                std::make_shared<rp_node_peer>(*this, std::move(sock))->handle();

            run();
        }
    );
}

/// @private
/// @brief send data to peer
/// @note this will be a blocking operation on purpose (reconsider?)
/// @note also, if we're using this function, we're answering a request
/// @param req peer struct
/// @param s data to send
void rp_node::send(
    peer req, 
    hash_t msg_id, 
    std::string s,
    se_callback ok_fn, 
    se_callback bad_fn) {
    spdlog::debug("sending peer {} ({}:{}) data", util::htos(req.id), req.addr, req.reply_port);

    tcp::socket socket(ioc);
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

    std::memcpy(out.magic, proto::consts.magic, proto::ML);
    std::memcpy(out.id, id_, proto::NL * sizeof(u32));
    std::memcpy(out.msg_id, msg_id_, proto::NL * sizeof(u32));

    socket.open(tcp::v4());
    
    socket.connect(req.rp_to_tcp_endpoint(), ec);

    if(ec) {
        spdlog::error("could not connect to rp"); 
        socket.close(); 
        bad_fn(req);
        return; 
    }

    std::make_shared<rp_node_peer>(*this, std::move(socket))->write(
        std::move(out),
        std::move(s),
        std::move(req),
        std::move(ok_fn),
        std::move(bad_fn)
    );
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
    rp_node_(ioc, *this, rp_port) {
    spdlog::info("messaging at addr {} on port {}, data transfer on port {}", 
        socket.local_endpoint().address().to_string(), port, rp_port);
    id = table.id = util::gen_id();
    spdlog::info("node id: {}", util::htos(id));
}

node::~node() {
    server_thread.join();
}

void node::start() {
    run();
    server_thread = std::thread([&, this]() {
        rp_node_.run();
        ioc.run();
    });
}

/////////////////////////////////////////////////
/// UDP callbacks
/////////////////////////////////////////////////

template <>
void node::okay<proto::actions::ping>(
    std::future<std::string> fut, 
    peer req, 
    pend_it it) {
    OBTAIN_FUT_MSG;
                        
    if(!std::memcmp(&m.magic, &proto::consts.magic, proto::ML) &&
        m.action == proto::actions::ping &&
        m.reply == proto::context::response) {
        {
            std::lock_guard<std::mutex> g(table.mutex);
            table.update_pending(req);
        }

        spdlog::debug("responded, updating");
    }
}

template <>
void node::bad<proto::actions::ping>(
    std::future<std::string> fut, 
    peer req, 
    pend_it it) {
    {
        std::lock_guard<std::mutex> g(table.mutex);
        int s;
        if((s = table.stale(req)) > proto::M) {
            table.evict(req);
            spdlog::debug("did not respond, evicting {}", util::htos(req.id));
        } else if(s != -1) {
            spdlog::debug("did not respond. staleness: {}", s);
        }
    }
}

template <>
void node::bad<proto::actions::find_node>(
    std::future<std::string> fut, 
    peer req, 
    pend_it it) {
    spdlog::debug("node did not send find_node information");
}

/////////////////////////////////////////////////
/// UDP node functions
/////////////////////////////////////////////////

/// @private
/// @brief queue current peer into pending list
/// @param req peer struct
/// @param msg_id message id
/// @param a action to queue under
/// @param rp is this on reply port (TCP, true) or message port (UDP, false) ?
/// @param ok_fn success callback
/// @param bad_fn failure callback
void node::queue_current(
    peer req, 
    hash_t msg_id, 
    proto::actions a, 
    bool rp,
    p_callback ok_fn, 
    p_callback bad_fn) {
    std::lock_guard<std::mutex> g(pending_mutex);
    pending.emplace_back(req, msg_id, a, rp);
    auto pit = pending.end();
    pit--;

    std::thread(
        &node::wait, 
        this, 
        req, 
        pit, 
        ok_fn,
        bad_fn).detach();
}

/// @private
/// @brief queue an ack response
/// @param req peer struct
/// @param msg_id message id
void node::queue_ack(peer req, hash_t msg_id) {
    // NOTE: we don't really need an ACK packet, just to ensure the data was sent completely for UI
    queue_current(req,
        util::htob(m_in.msg_id),
        proto::actions::ack,
        false,
        [this](std::future<std::string> fut, peer req, pend_it it) {
            std::lock_guard<std::mutex> g(pending_mutex);
            OBTAIN_FUT_MSG;
            
            // we have a proper ack?
            if(!std::memcmp(&m.magic, &proto::consts.magic, proto::ML) &&
                m.action == proto::actions::ack &&
                m.reply == proto::context::response) {
                spdlog::debug("we got a proper ack back");
            } else {
                spdlog::debug("we didnt get a proper ack back");
            }
        },
        [this](std::future<std::string> fut, peer req, pend_it it) {
            spdlog::error("we did not get an ack back or the peer sent bad data for find_node");
        });
}

/// @private
/// @brief send whatever's in m_out to peer
/// @param req peer struct
/// @param a action
/// @param ok_fn success callback
/// @param bad_fn failure callback
/// @return msg id of sent message
hash_t node::send(peer req, proto::actions a, p_callback ok_fn, p_callback bad_fn) {
    // don't double send
    {
        std::lock_guard<std::mutex> l(pending_mutex);
        auto it = std::find_if(pending.begin(), pending.end(),
            [&](pending_item& i) { 
                return i.req == req && 
                    i.msg_id == util::htob(m_out.msg_id) && 
                    i.action == a; });

        if(it != pending.end()) return hash_t(0);
    }

    socket.async_send_to(
        boost::asio::buffer(&m_out, sizeof(proto::msg)), 
        udp::endpoint{
            boost::asio::ip::address::from_string(req.addr),
            req.port},
        [this, req, a, ok_fn, bad_fn](boost::system::error_code ec, std::size_t sz) {
            if((!ec || ec == boost::asio::error::eof) && sz == sizeof(proto::msg)) {
                // queue and await a reply
                queue_current(req, util::htob(m_out.msg_id), a, false, ok_fn, bad_fn);
            }
        });
        
    return util::htob(m_out.msg_id);
}

/// @private
/// @brief send a message over UDP to a peer
/// @param req peer struct
/// @param a request action
/// @param sz size of payload (0 for messages only)
/// @param ok_fn callback for success
/// @param bad_fn callback for failure (timeout, error, etc.)
/// @return msg id of sent message
hash_t node::send(peer req, proto::actions a, u64 sz, p_callback ok_fn, p_callback bad_fn) {
    m_out = {
        .action = (u8)a,
        .reply = proto::context::request,
        .response = proto::responses::ok,
        .msg_port = port,
        .reply_port = rp_node_.port,
        .sz = sz
    };

    id_t id_ = {0}; util::btoh(id, id_);

    std::memcpy(&m_out.magic, proto::consts.magic, proto::ML);
    std::memcpy(m_out.id, id_, proto::NL * sizeof(u32));
    util::msg_id(m_out.msg_id);

    return send(req, a, ok_fn, bad_fn);
}

/// @private
/// @brief reply to a message over UDP to a peer specifying the msg id
/// @param req peer struct
/// @param a request action
/// @param msg_id msg id
/// @param sz size of payload (0 for messages only)
/// @param ok_fn callback for success
/// @param bad_fn callback for failure (timeout, error, etc.)
/// @return msg id of sent message
hash_t node::reply(peer req, proto::actions a, hash_t msg_id, u64 sz, p_callback ok_fn, p_callback bad_fn) {
    m_out = {
        .action = (u8)a,
        .reply = proto::context::response,
        .response = proto::responses::ok,
        .msg_port = port,
        .reply_port = rp_node_.port,
        .sz = sz
    };

    id_t id_ = {0}; util::btoh(id, id_);

    std::memcpy(&m_out.magic, proto::consts.magic, proto::ML);
    std::memcpy(m_out.id, id_, proto::NL * sizeof(u32));
    util::btoh(msg_id, m_out.msg_id);

    return send(req, a, ok_fn, bad_fn);
}

/// @private
/// @brief add message to pending list and start a thread to check for response
/// @param req peer struct
/// @param it iterator to item in pending list
/// @param ok_fn callback for success
/// @param bad_fn callback for failure
void node::wait(
    peer req, 
    pend_it it,
    p_callback ok_fn,
    p_callback bad_fn) {
    try {
        {
            std::lock_guard<std::mutex> g(pending_mutex);
            if(it == pending.end()) {
                spdlog::error("bad iterator!");
                bad_fn(std::future<std::string>(), req, it);
                return;
            }
        }

        std::future<std::string> fut = it->promise.get_future();

        switch(fut.wait_for(seconds(proto::T))) {
        case std::future_status::ready:
            ok_fn(std::move(fut), req, it);
            break;

        case std::future_status::deferred:
        case std::future_status::timeout:
            bad_fn(std::move(fut), req, it);
            break;
        }

        {
            std::lock_guard<std::mutex> g(pending_mutex);
            pending.erase(it);
        }
    } catch(std::future_error& e) {
        if(e.code() == std::future_errc::future_already_retrieved) {
            std::lock_guard<std::mutex> g(table.mutex);
            table.update_pending(req);

            spdlog::debug("already responded, updating node {}", util::htos(req.id));
        }
    }
}

/////////////////////////////////////////////////
/// UDP node message handlers
/////////////////////////////////////////////////

/// @brief reply to a ping request
/// @param req peer struct
template <>
void node::reply<proto::actions::ping>(peer req) {
    MAKE_MSG(ping, response, ok)

    socket.async_send_to(
        boost::asio::buffer((void*)&m_out, sizeof(proto::msg)), 
        req.to_udp_endpoint(),
        [&](boost::system::error_code ec, std::size_t sz) {
            /// @todo do nothing?
        });

    table.update(req);
}

/// @brief reply to a find_node request
/// @param req peer struct
template <>
void node::reply<proto::actions::find_node>(peer req) {
    MAKE_MSG(find_node, response, ok)

    // drop request if there's no tcp data to recv
    if(m_in.sz <= 0)
        return;

    // read node from tcp socket 
    queue_current(
        req,
        util::htob(m_in.msg_id),
        proto::actions::find_node,
        true,
        [this](std::future<std::string> fut, peer req, pend_it it) {    
            std::stringstream ss;

            id_t id_ = {0};

            std::lock_guard<std::mutex> g(pending_mutex);

            std::string v = fut.get();
            std::string s(v.begin(), v.end()), str{};

            // TODO: check if node id is good
            if(s.size() != proto::NL * sizeof(u32))
                return;

            std::memcpy((void*)id_, (void*)s.c_str(), proto::NL * sizeof(u32));

            {
                boost::archive::binary_oarchive boa(ss);
                std::lock_guard<std::mutex> l(table.mutex);
                bucket bkt = table.find_bucket(peer(util::htob(id_)));
                boa << bkt;
            }

            m_out.sz = ss.tellp();
            str = ss.str();

            spdlog::debug("looked up node {}", util::htos(util::htob(id_)));

            socket.async_send_to(
                boost::asio::buffer((void*)&m_out, sizeof(proto::msg)), 
                req.to_udp_endpoint(),
                [this, req, s = std::move(str)](boost::system::error_code ec, std::size_t sz) {
                    if((!ec || ec == boost::asio::error::eof) && sz == sizeof(proto::msg)) {
                        // we do not care about the result of this
                        rp_node_.send(req, util::htob(m_in.msg_id), s, se_do_nothing, se_do_nothing);
                    }
                });
            },
        std::bind(&node::bad<proto::actions::find_node>, this, _1, _2, _3));

    
    queue_ack(req, util::htob(m_in.msg_id));
    
    table.update(req);
}

/////////////////////////////////////////////////
/// UDP node dispatcher
/////////////////////////////////////////////////

/// @brief event loop for UDP socket
void node::run() {
    socket.async_receive_from(boost::asio::buffer((void*)&m_in, sizeof(proto::msg)), client,
        [this](boost::system::error_code ec, std::size_t sz) {
            if((!ec || ec == boost::asio::error::eof) && sz == sizeof(proto::msg)) {
                hash_t id_ = util::htob(m_in.id);

                spdlog::info("id_: {}", util::htos(id_));

                // correct magic, reply port is valid
                if(std::memcmp(&m_in.magic, &proto::consts.magic, proto::ML) ||
                    !(m_in.msg_port > 0 && m_in.msg_port < 65536) ||
                    !(m_in.reply_port > 0 && m_in.reply_port < 65536) ||
                    id_ == hash_t(0)) 
                    goto o;

                peer req(client.address().to_string(), m_in.msg_port, m_in.reply_port, id_);

                {
                    std::lock_guard<std::mutex> l(pending_mutex);

                    // fulfill pending promise, if any
                    auto pit = std::find_if(pending.begin(), pending.end(),
                        [&](pending_item& i) { 
                            return i.req == req && 
                                i.msg_id == util::htob(m_in.msg_id) && 
                                i.action == m_in.action &&
                                i.rp == false; });

                    if(pit != pending.end()) {
                        std::string v;
                        v.resize(sizeof(proto::msg));
                        std::memcpy((void*)v.data(), &m_in, sizeof(proto::msg));

                        if(!pit->satisfied)
                            pit->promise.set_value(std::move(v));
                        pit->satisfied = true;

                        // if you have a pending request, we will ignore you
                        if(m_in.reply == proto::context::request)
                            goto o;
                    }
                }

                spdlog::debug("msg from {}:{} (rp: {}) id {} msg {}", 
                    req.addr, req.port, req.reply_port, 
                    util::htos(req.id), util::htos(util::htob(m_in.msg_id)));

#define r(a) case proto::actions::a: reply<proto::actions::a>(req); break;
                if(!m_in.reply) {
                    switch(m_in.action) {
                    r(ping) r(find_node)
                    }
                }
#undef r
            }

            o: run();
        });
}

#undef MAKE_MSG
}
}