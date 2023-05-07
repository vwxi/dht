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
    u16 reply_port;

    int staleness;

    peer(hash_t id_) : id(id_) { }
    
    peer(std::string a, u16 p, u16 rp) : addr(a), port(p), reply_port(rp), staleness(0) {
        id = util::gen_id(addr, port);
    }

    peer(udp::endpoint e, u16 rp) :
        addr(e.address().to_string()),
        port(e.port()),
        reply_port(rp),
        staleness(0) {
        id = util::gen_id(addr, port);
    }

    udp::endpoint to_udp_endpoint() const {
        return udp::endpoint{boost::asio::ip::address::from_string(addr), port};
    }

    udp::endpoint rp_to_udp_endpoint() const {
        return udp::endpoint{boost::asio::ip::address::from_string(addr), reply_port};
    }

    tcp::endpoint to_tcp_endpoint() const {
        return tcp::endpoint{boost::asio::ip::address::from_string(addr), port};
    }

    tcp::endpoint rp_to_tcp_endpoint() const {
        return tcp::endpoint{boost::asio::ip::address::from_string(addr), reply_port};
    }

    template <class Archive>
    void save(Archive& ar, const unsigned int version) const {
        sha1::digest_type h;
        util::btoh(id, h);

        for(int i = 0; i < proto::NL; i++)
            ar << h[i];

        ar << addr;
        ar << port;
        ar << reply_port;
    }

    template <class Archive>
    void load(Archive& ar, const unsigned int version) const {
        sha1::digest_type h;

        for(int i = proto::NL-1; i >= 0; i++)
            ar >> h[i];

        id = util::htob(h);
        ar & addr;
        ar & port;
        ar & reply_port;
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()
};

struct pending_item {
    hash_t id;
    hash_t msg_id;
    proto::actions action;
    std::promise<std::vector<u8>> promise;
    pending_item(hash_t, hash_t, proto::actions);
};

using pend_it = std::list<pending_item>::iterator;
using p_callback = std::function<void(std::future<std::vector<u8>>, peer, pend_it)>;

class rp_node;
class node;

/// @brief a TCP connection
class rp_node_peer :
    public std::enable_shared_from_this<rp_node_peer> {
public:
    typedef std::shared_ptr<rp_node_peer> ptr;

    rp_node_peer(rp_node&, tcp::socket);

    void kill();
    void handle();
    void send(peer, hash_t, std::string);
    void read(proto::rp_msg, pend_it);

private:
    tcp::socket socket;
    proto::rp_msg m_in;
    proto::rp_msg m_out;

    rp_node& rp_node_;
};

/// @brief a TCP server/client
class rp_node {
public:
    rp_node(boost::asio::io_context&, node&, u16);

    void run();
    void send(peer, hash_t, std::string);

    node& node_;

    u16 port;

    boost::asio::io_context& ioc;
    tcp::acceptor acceptor;
};

/// @brief a class containing both the TCP and UDP servers
class node {
public:
    node(u16, u16);

    void run();
    bool connect(std::string, u16);

    void send(peer, proto::actions, p_callback, p_callback);

    // p_callbacks

    void do_nothing(std::future<std::vector<u8>>, peer, pend_it);

    template <proto::actions a>
    void okay(std::future<std::vector<u8>>, peer, pend_it);

    template <proto::actions a>
    void bad(std::future<std::vector<u8>>, peer, pend_it);

    void queue_current(peer, hash_t, proto::actions, p_callback, p_callback);

    u16 port;

    std::mutex pending_mutex;
    std::list<pending_item> pending;
    
private:
    void wait(peer, pend_it, p_callback, p_callback);

    template <proto::actions a>
    void reply(peer); // reply to a request

    template <proto::actions a>
    void handle(peer); // handle a response

    boost::asio::io_context ioc;

    hash_t id;
    
    routing_table table;

    rp_node rp_node_;
    
    udp::socket socket;
    udp::endpoint client;

    proto::msg m_in;
    proto::msg m_out;
};

}

#endif