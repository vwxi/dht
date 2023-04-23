#include "network.h"
#include "bucket.h"
#include "routing.h"

namespace dht {

pending_item::pending_item(hash_t id_, hash_t nonce_, proto::actions a) : 
    id(id_), nonce(nonce_), action(a) { 
    promise = std::make_shared<std::promise<proto::msg>>();
}

/// @brief initialize a new node
/// @param port_ port number (0-65535)
node::node(u16 port_) : 
    port(port_), socket(ioc, udp::endpoint(udp::v4(), port_)), table(id, *this) {
    id = util::gen_id(socket.local_endpoint().address().to_string(), port_);
    spdlog::info("created node {}", util::htos(id));
    run();
    ioc.run();
}

/// @brief send a message to a peer
/// @param req peer struct
/// @param fn thread function
void node::send(peer req, proto::actions a, node::wait_fn fn) {
    m_out = {
        .action = (u8)a,
        .reply = 0,
        .response = proto::responses::ok,
        .sz = 0
    };

    std::memcpy(&m_out.magic, proto::consts.magic, proto::ML);
    util::nonce(m_out.nonce);

    std::lock_guard<std::mutex> l(pending_mutex);
    auto it = std::find_if(pending.begin(), pending.end(),
        [&](pending_item& i) { 
            return i.id == req.id && 
                i.nonce == req.id && 
                i.action == a; });
    
    if(it != pending.end()) return;

    socket.async_send_to(
        boost::asio::buffer(&m_out, sizeof(proto::msg)), 
        udp::endpoint{
            boost::asio::ip::address::from_string(req.addr),
            req.port},
        [this, req, fn, a](boost::system::error_code ec, std::size_t sz) {
            if(!ec && sz == sizeof(proto::msg)) {
                pending.emplace_back(req.id, util::htob(m_out.nonce), a);
                auto pit = pending.end();
                pit--;

                std::thread t(fn, req, pit);
                t.detach();
            }
        });
}

template <>
void node::wait<proto::actions::ping>(peer req, std::list<pending_item>::iterator it) {
    try {
        std::future<proto::msg> fut = it->promise->get_future();

        spdlog::info("waiting for response from {}...", util::htos(req.id));

        switch(fut.wait_for(seconds(proto::G))) {
        case std::future_status::ready: {
            {
                std::lock_guard<std::mutex> g(table.mutex);
                table.update_pending(req);
            }
            
            break;
        }

        case std::future_status::deferred:
        case std::future_status::timeout: {
            {
                std::lock_guard<std::mutex> g(table.mutex);
                table.evict(req);
            }

            spdlog::info("did not respond, evicting {}", util::htos(req.id));

            {
                std::lock_guard<std::mutex> g2(pending_mutex);
                pending.erase(it);
            }

            break;
        }
        }
    } catch(std::future_error& e) {
        if(e.code() == std::future_errc::future_already_retrieved) {
            std::lock_guard<std::mutex> g(table.mutex);
            table.update_pending(req);

            spdlog::info("already responded, updating node {}", util::htos(req.id));
        }
    }
}

template <>
void node::reply<proto::actions::ping>(peer req) {
    m_out = {
        .action = proto::actions::ping,
        .reply = 1,
        .response = proto::responses::ok,
        .sz = 0
    };

    std::memcpy(&m_out.magic, proto::consts.magic, proto::ML);
    std::memcpy(&m_out.nonce, &m_in.nonce, 5); // TODO: CONSTANT THIS?

    socket.async_send_to(boost::asio::buffer((void*)&m_out, sizeof(proto::msg)), req.to_endpoint(),
        [&](boost::system::error_code ec, std::size_t sz) {
            // do nothing?
        });

    std::lock_guard<std::mutex> l(table.mutex);
    table.update(req);
}

/// @brief main function for handling messages
void node::run() {
    socket.async_receive_from(boost::asio::buffer((void*)&m_in, sizeof(proto::msg)), client,
        [this](boost::system::error_code ec, std::size_t sz) {
            if(!ec && sz == sizeof(proto::msg)) {
                // valid magic?
                if(std::memcmp(&m_in.magic, &proto::consts.magic, proto::ML)) 
                    goto o;

                peer p(client);
                spdlog::info("msg from {}:{} id {}", p.addr, p.port, util::htos(p.id));

                {
                    std::lock_guard<std::mutex> l(pending_mutex);

                    // fulfill pending promise, if any
                    auto pit = std::find_if(pending.begin(), pending.end(),
                        [&](pending_item& i) { 
                            return i.id == p.id && 
                                i.nonce == util::htob(m_in.nonce) && 
                                i.action == m_in.action; });

                    if(pit != pending.end()) {
                        pit->promise->set_value(m_in);
                        pending.erase(pit);
                    }
                }

#define r(a) case proto::actions::a: reply<proto::actions::a>(p); break;
#define R(a) case proto::actions::a: handle<proto::actions::a>(p); break;
                if(!m_in.reply) {
                    switch(m_in.action) {
                    r(ping)
                    }
                } else {
                    switch(m_in.action) {

                    }
                }
#undef r
#undef R
            }

            o: run();
        });
}

}