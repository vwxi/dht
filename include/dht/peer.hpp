#ifndef _PEER_HPP
#define _PEER_HPP

#include "util.hpp"
#include "proto.hpp"
#include "routing.hpp"

namespace dht {

class peer {
public:
    peer(boost::asio::io_context& ioc_, u16 port_) :
        ioc(ioc_),
        port(port_),
        table(id, cache_mutex),
        pc(&peer::pending_check, this),
        socket(ioc_, udp::endpoint(udp::v4(), port_)) {
        id = util::gen_id(socket.local_endpoint().address().to_string(), port_);
        table.id = id;
        spdlog::info("peer setup [id: {}]", util::htos(id));
        run();
    }

    ~peer() {
        pc.join();
    }

    template <proto::actions a>
    void reply(std::shared_ptr<node>, proto::msg);

    template <proto::actions a>
    void handle(std::shared_ptr<node>, proto::msg);

    void run();

    void pending_check();

    hash_t id;

private:
    std::string addr;
    u16 port;

    udp::socket socket;
    boost::asio::io_context& ioc;

    proto::msg msg;
    udp::endpoint client;

    routing_table table;

    std::mutex cache_mutex;

    std::vector<std::shared_ptr<node>> pending;
    std::mutex pending_mutex;

    std::thread pc;
};

}

#endif