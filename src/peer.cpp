#include "peer.hpp"

namespace dht {

template <>
void peer::reply<proto::actions::ping>(std::shared_ptr<node> n, proto::msg m) {
    spdlog::info("PING FROM {}:{} (id: {})", n->addr, n->port, util::htos(n->id));

    proto::msg r = {
        .action = proto::actions::ping,
        .reply = 1,
        .response = proto::responses::ok,
        .sz = 0
    };

    std::memcpy(&r.magic, &proto::consts.magic, proto::ML);
    std::memcpy(&r.nonce, &m.nonce, proto::ML * sizeof(unsigned int));

    socket.async_send_to(boost::asio::buffer(&r, sizeof(proto::msg)), n->endpoint, 
        [this](boost::system::error_code ec, std::size_t sz) {
            if(!ec) {
                std::lock_guard<std::mutex> lock(pending_mutex);
                auto it = std::find_if(pending.begin(), pending.end(),
                    [&](std::shared_ptr<node> p) { return p->id == id; });
                if(it != pending.end())
                    pending.erase(it);
            }
        });

    table.update(n);
}

template <>
void peer::handle<proto::actions::ping>(std::shared_ptr<node> n, proto::msg m) {
    spdlog::info("PONG FROM {}:{} (id: {})", n->addr, n->port, util::htos(n->id));
    std::lock_guard<std::mutex> g(pending_mutex);

    auto it = std::find_if(pending.begin(), pending.end(),
        [&](std::shared_ptr<node> p) { return p->id == n->id; });

    if(it != pending.end()) {
        table.update_pending(*it);
        pending.erase(it);
    } else table.update(n);
}

void peer::run() {
    socket.async_receive_from(boost::asio::buffer((void*)&msg, sizeof(proto::msg)), client,
        [this](const boost::system::error_code& ec, std::size_t sz) {
            if(!ec && sz == sizeof(proto::msg)) {
                // valid magic?
                if(std::memcmp(&msg.magic, &proto::consts.magic, proto::ML)) 
                    goto o;
                
                udp::endpoint client_c = client;
                std::shared_ptr<node> ptr = std::make_shared<node>(ioc, socket, client_c, pending, pending_mutex);

#define r(n) case proto::actions::n: reply<proto::actions::n>(ptr, msg); break;
#define R(n) case proto::actions::n: handle<proto::actions::n>(ptr, msg); break;

                if(!msg.reply) {
                    switch(msg.action) {
                    r(ping)
                    }
                } else {
                    switch(msg.action) {
                    R(ping)
                    }
                }
            } else {
                spdlog::error("{} bytes read, msg: {}", sz, ec.message());
            }

            o: run();
        }
    );
}

void peer::pending_check() {
    while(true) {
        {
            std::lock_guard<std::mutex> l(pending_mutex);
            if(pending.size() > 0) {
                spdlog::info("--- pending: ");
                for(auto& i : pending) {
                    if(i->staleness > proto::M || i->checks++ > proto::C) {
                        spdlog::info("TO EVICT: {} - staleness: {}, checks: {}", util::htos(i->id), i->staleness, i->checks);
                        table.evict_pending(i);
                        pending.erase(
                            std::remove_if(
                                pending.begin(), pending.end(), 
                                [&](std::shared_ptr<node> p) { return p->id == i->id; }), 
                                pending.end());
                    } else {
                        spdlog::info("AWAITING: {} - staleness: {}, checks: {}", util::htos(i->id), i->staleness, i->checks);
                    }
                }
            } else spdlog::info("no pending messages");
        }

        std::this_thread::sleep_for(seconds(proto::T));
    }
}

}