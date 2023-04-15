#include "net.hpp"

namespace dht {

template <>
void peer::reply<proto::actions::ping>(std::shared_ptr<node> n, proto::msg m) {
    spdlog::info("PING FROM {}:{} (id: {})", n->addr, n->port, util::htos(n->id));

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
                if(std::memcmp(&msg.magic, &proto::consts.magic, 4)) 
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
            std::unique_lock<std::mutex> l(pending_mutex);
            spdlog::info("--- pending: ");
            for(auto& i : pending) {
                if(i->missed_pings > proto::M) {
                    spdlog::info("TO DELETE: {} - missed pings: {}", util::htos(i->id), i->missed_pings);
                    table.delete_pending(i);
                    pending.erase(
                        std::remove_if(
                            pending.begin(), pending.end(), 
                            [&](std::shared_ptr<node> p) { return p->id == i->id; }), 
                            pending.end());
                } else {
                    spdlog::info("AWAITING: {} - missed pings: {}", util::htos(i->id), i->missed_pings);
                }
            }
        }

        std::this_thread::sleep_for(seconds(proto::T));
    }
}

}