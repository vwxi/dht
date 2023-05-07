#include "network.h"
#include "bucket.h"
#include "routing.h"

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
        .sz = 0 \
    }; \
    std::memcpy(&m_out.magic, proto::consts.magic, proto::ML); \
    std::memcpy(&m_out.msg_id, &m_in.msg_id, proto::NL);

#define UPDATE \
    { std::lock_guard<std::mutex> l(table.mutex); table.update(req); }

#define OBTAIN_FUT_MSG \
    std::vector<u8> v = fut.get(); \
    proto::msg m; \
    std::copy(v.begin(), v.end(), reinterpret_cast<char*>(&m));

/// @private
/// @brief initialize a pending item
/// @param id_ id hash
/// @param msg_id_ msg_id hash
/// @param a queued action
pending_item::pending_item(hash_t id_, hash_t msg_id_, proto::actions a) : 
    id(id_), msg_id(msg_id_), action(a) { }

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
/// @brief kill connection
void rp_node_peer::kill() {
    socket.close();
}

/// @private
/// @brief handle peer connection
void rp_node_peer::handle() {
    auto self(shared_from_this());

    std::memset((void*)&m_in, 0, sizeof(proto::rp_msg));

    boost::asio::async_read(
        socket, 
        boost::asio::buffer((void*)&m_in, sizeof(proto::rp_msg)),
        [this, self](boost::system::error_code ec, std::size_t sz) {
            // correct magic, under size limit, messaging port is valid
            if(ec || 
                sz != sizeof(proto::rp_msg) ||
                std::memcmp(&m_in.magic, proto::consts.magic, proto::ML) ||
                m_in.sz > proto::MS ||
                !(m_in.msg_port > 0 && m_in.msg_port < 65536)) { kill(); return; }

            tcp::endpoint ep = socket.remote_endpoint();

            hash_t id_ = util::gen_id(ep.address().to_string(), m_in.msg_port);

            std::lock_guard<std::mutex> g(rp_node_.node_.pending_mutex);
            auto it = std::find_if(rp_node_.node_.pending.begin(), rp_node_.node_.pending.end(),
                [&](pending_item& i) { return i.id == id_ && i.msg_id == util::htob(m_in.msg_id); });

            // ignore if data isn't even in pending list
            if(it == rp_node_.node_.pending.end()) { kill(); return; }

            read(m_in, it);
            handle();
        });
}

/// @private
/// @brief read and handle pending message
/// @todo handle acks
/// @param it iterator to item
void rp_node_peer::read(proto::rp_msg m, pend_it it) {
    std::vector<u8> d(m.sz);

    boost::asio::async_read(
        socket,
        boost::asio::buffer(d),
        [this, m, it, d](boost::system::error_code ec, std::size_t sz) {
            if(!ec && sz == m.sz) {
                it->promise.set_value(d);
            }
        });
}

/// @private
/// @brief send data using blocking socket
/// @note if we're using this function, it's to answer a request
/// @param req peer struct
/// @param o stream of data to send
void rp_node_peer::send(peer req, hash_t msg_id, std::string o) {
    boost::system::error_code ec;

    std::size_t sz = o.length();
    m_out = {
        .ack = 0,
        .msg_port = rp_node_.node_.port,
        .sz = sz
    };

    sha1::digest_type msg_id_;
    util::btoh(msg_id, msg_id_);

    std::memcpy(&m_out.magic, proto::consts.magic, proto::ML);
    std::memcpy(&m_out.msg_id, &msg_id_, proto::NL);

    socket.open(tcp::v4());
    
    //socket.set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ proto::T * 1000 });

    socket.connect(req.rp_to_tcp_endpoint(), ec);
    if(ec) goto bad;

    socket.send(boost::asio::buffer((void*)&m_out, sizeof(proto::rp_msg)), 0, ec);
    if(ec) goto bad;
    
    socket.send(boost::asio::buffer(o), 0, ec);
    if(ec) goto bad;

bad:
    kill();
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
/// @brief send data to peer, run in a thread?
/// @note this will be a blocking operation on purpose (reconsider?)
/// @note also, if we're using this function, we're answering a request
/// @param req peer struct
/// @param s data to send
void rp_node::send(peer req, hash_t msg_id, std::string s) {
    // connect to peer over tcp
    spdlog::info("connecting to peer {} ({}:{}) to send data", util::htos(req.id), req.addr, req.reply_port);

    tcp::socket sock(ioc);

    std::make_shared<rp_node_peer>(*this, std::move(sock))->send(req, msg_id, std::move(s));
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
    spdlog::info("messaging on port {}, data transfer on port {}", port, rp_port);
    id = util::gen_id(socket.local_endpoint().address().to_string(), port_);
    spdlog::info("created node {}", util::htos(id));
    run();
    rp_node_.run();
    ioc.run();
}

/////////////////////////////////////////////////
/// UDP callbacks
/////////////////////////////////////////////////

/// @brief callback to do nothing 
void node::do_nothing(std::future<std::vector<u8>>, peer, pend_it) { return; }

template <>
void node::okay<proto::actions::ping>(
    std::future<std::vector<u8>> fut, 
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

        spdlog::info("responded, updating");
    }
}

template <>
void node::bad<proto::actions::ping>(
    std::future<std::vector<u8>> fut, 
    peer req, 
    pend_it it) {
    OBTAIN_FUT_MSG;

    {
        std::lock_guard<std::mutex> g(table.mutex);
        int s;
        if((s = table.stale(req)) > proto::M) {
            table.evict(req);
            spdlog::info("did not respond, evicting {}", util::htos(req.id));
        } else if(s != -1) {
            spdlog::info("did not respond. staleness: {}", s);
        }
    }
}

template <>
void node::okay<proto::actions::find_node>(
    std::future<std::vector<u8>> fut, 
    peer req, 
    pend_it it) {    
    std::stringstream ss;
    
    sha1::digest_type id_;
    
    std::vector<u8> v = fut.get();
    std::string s(v.begin(), v.end()), str{};
    
    // TODO: check if node id is good
    if(s.size() < proto::NL)
        return;

    std::memcpy((void*)&id_, (void*)s.c_str(), proto::NL);

    {
        boost::archive::binary_oarchive boa(ss, boost::archive::no_header | boost::archive::no_tracking);
        std::lock_guard<std::mutex> l(table.mutex);
        bucket bkt = table.find_bucket(peer(util::htob(id_)));
        boa << bkt;
    }

    m_out.sz = ss.tellp();
    str = ss.str();

    socket.async_send_to(boost::asio::buffer((void*)&m_out, sizeof(proto::msg)), req.to_udp_endpoint(),
        [this, req, s = std::move(str)](boost::system::error_code ec, std::size_t sz) {
            if(!ec && sz == sizeof(proto::msg)) {
                rp_node_.send(req, util::htob(m_in.msg_id), s);
            }
        });
}

template <>
void node::bad<proto::actions::find_node>(
    std::future<std::vector<u8>> fut, 
    peer req, 
    pend_it it) {
    OBTAIN_FUT_MSG;
    
    spdlog::critical("node did not send find_node information");
}

/////////////////////////////////////////////////
/// UDP node functions
/////////////////////////////////////////////////

/// @private
/// @brief queue current peer into pending list
/// @param req peer struct
/// @param msg_id message id
/// @param a action to queue under
/// @param ok_fn success callback
/// @param bad_fn failure callback
void node::queue_current(peer req, hash_t msg_id, proto::actions a, p_callback ok_fn, p_callback bad_fn) {
    std::lock_guard<std::mutex> g(pending_mutex);
    pending.emplace_back(req.id, msg_id, a);
    auto pit = pending.end();
    pit--;
    
    /// @todo write callbacks for tcp reply back acks
    std::thread(
        &node::wait, 
        this, 
        req, 
        pit, 
        ok_fn,
        bad_fn).detach();
}

/// @public
/// @brief send a message to a peer
/// @param req peer struct
/// @param a request action
/// @param ok_fn callback for success
/// @param bad_fn callback for failure (timeout, error, etc.)
void node::send(peer req, proto::actions a, p_callback ok_fn, p_callback bad_fn) {
    m_out = {
        .action = (u8)a,
        .reply = proto::context::request,
        .response = proto::responses::ok,
        .sz = 0
    };

    std::memcpy(&m_out.magic, proto::consts.magic, proto::ML);
    util::msg_id(m_out.msg_id);

    {
        std::lock_guard<std::mutex> l(pending_mutex);
        auto it = std::find_if(pending.begin(), pending.end(),
            [&](pending_item& i) { return i.id == req.id && i.action == a; });

        if(it != pending.end()) return;
    }

    socket.async_send_to(
        boost::asio::buffer(&m_out, sizeof(proto::msg)), 
        udp::endpoint{
            boost::asio::ip::address::from_string(req.addr),
            req.port},
        [this, req, a, ok_fn, bad_fn](boost::system::error_code ec, std::size_t sz) {
            if(!ec && sz == sizeof(proto::msg)) {
                // queue and await a reply
                queue_current(req, util::htob(m_out.msg_id), a, ok_fn, bad_fn);
            }
        });
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
                bad_fn(std::future<std::vector<u8>>(), req, it);
                return;
            }
        }

        std::future<std::vector<u8>> fut = it->promise.get_future();

        switch(fut.wait_for(seconds(proto::T))) {
        case std::future_status::ready:
            ok_fn(std::move(fut), req, it);
            break;

        case std::future_status::deferred:
        case std::future_status::timeout:
            bad_fn(std::move(fut), req, it);
            break;
        }
    } catch(std::future_error& e) {
        if(e.code() == std::future_errc::future_already_retrieved) {
            std::lock_guard<std::mutex> g(table.mutex);
            table.update_pending(req);

            spdlog::info("already responded, updating node {}", util::htos(req.id));
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

    socket.async_send_to(boost::asio::buffer((void*)&m_out, sizeof(proto::msg)), req.to_udp_endpoint(),
        [&](boost::system::error_code ec, std::size_t sz) {
            /// @todo do nothing?
        });

    UPDATE
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
        std::bind(&node::okay<proto::actions::find_node>, this, _1, _2, _3),
        std::bind(&node::bad<proto::actions::find_node>, this, _1, _2, _3));

    // response will be handled in callback
    // we don't want to reach this if we didn't get a node over tcp vvvvvvv

    // await ack packet
    // NOTE: we don't really need an ACK packet, just to ensure the data was sent completely for UX
    queue_current(req,
        util::htob(m_in.msg_id),
        proto::actions::ack,
        [this](std::future<std::vector<u8>> fut, peer req, pend_it it) {
            OBTAIN_FUT_MSG;
            
            // we have a proper ack?
            if(!std::memcmp(&m.magic, &proto::consts.magic, proto::ML) &&
                m.action == proto::actions::ack &&
                m.reply == proto::context::response) {
                spdlog::info("we got a proper ack back");
            } else {
                spdlog::warn("we didnt get a proper ack back");
            }
        },
        [this](std::future<std::vector<u8>> fut, peer req, pend_it it) {
            spdlog::error("we did not get an ack back or the peer sent bad data for find_node");
        });

    UPDATE
}

/////////////////////////////////////////////////
/// UDP node dispatcher
/////////////////////////////////////////////////

/// @brief event loop for UDP socket
void node::run() {
    socket.async_receive_from(boost::asio::buffer((void*)&m_in, sizeof(proto::msg)), client,
        [this](boost::system::error_code ec, std::size_t sz) {
            if(!ec && sz == sizeof(proto::msg)) {
                // correct magic, reply port is valid
                if(std::memcmp(&m_in.magic, &proto::consts.magic, proto::ML) ||
                    !(m_in.reply_port > 0 && m_in.reply_port < 65536)) 
                    goto o;

                peer p(client, m_in.reply_port);

                {
                    std::lock_guard<std::mutex> l(pending_mutex);

                    // fulfill pending promise, if any
                    auto pit = std::find_if(pending.begin(), pending.end(),
                        [&](pending_item& i) { 
                            return i.id == p.id && 
                                i.msg_id == util::htob(m_in.msg_id) && 
                                i.action == m_in.action; });

                    if(pit != pending.end()) {
                        std::vector<u8> v(sizeof(proto::msg));
                        std::memcpy(&v[0], &m_in, sizeof(proto::msg));

                        pit->promise.set_value(std::move(v));
                        pending.erase(pit);

                        // if you have a pending request, we will ignore you
                        if(m_in.reply == proto::context::request)
                            goto o;
                    }
                }

                spdlog::info("msg from {}:{} (rp: {}) id {}", p.addr, p.port, p.reply_port, util::htos(p.id));

#define r(a) case proto::actions::a: reply<proto::actions::a>(p); break;
#define R(a) case proto::actions::a: handle<proto::actions::a>(p); break;
                if(!m_in.reply) {
                    switch(m_in.action) {
                    r(ping) r(find_node)
                    }
                }
#undef r
#undef R
            }

            o: run();
        });
}

#undef MAKE_MSG
#undef UPDATE
}