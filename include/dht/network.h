#ifndef _NETWORK_H
#define _NETWORK_H

#include "util.hpp"
#include "routing.h"

namespace dht {

struct peer {
    friend boost::serialization::access;

    hash_t id;
    std::string addr;
    u16 port;
    int staleness;

    peer(udp::endpoint e) :
        addr(e.address().to_string()),
        port(e.port()),
        staleness(0) {
        id = util::gen_id(addr, port);
    }

    udp::endpoint to_endpoint() const {
        return udp::endpoint{boost::asio::ip::address::from_string(addr), port};
    }

    template <class Archive>
    void serialize(Archive& ar, const unsigned int version) const {
        ar & id;
        ar & addr;
        ar & port;
    }
};

struct pending_item {
    hash_t id;
    hash_t nonce;
    proto::actions action;
    std::shared_ptr<std::promise<proto::msg>> promise;
    pending_item(hash_t, hash_t, proto::actions);
};

class node {
public:
    using wait_fn = std::function<void(peer, std::list<pending_item>::iterator)>;

    node(u16);

    void run();
    bool connect(std::string, u16);

    void send(peer, proto::actions, wait_fn);

    template <proto::actions a>
    void wait(peer, std::list<pending_item>::iterator);

private:
    template <proto::actions a>
    void reply(peer); // reply to a request

    template <proto::actions a>
    void handle(peer); // handle a response

    boost::asio::io_context ioc;

    u16 port;

    hash_t id;
    
    routing_table table;
    
    udp::socket socket;
    udp::endpoint client;

    std::mutex pending_mutex;
    std::list<pending_item> pending;

    proto::msg m_in;
    proto::msg m_out;
};

}

#endif