#ifndef _NETWORK_H
#define _NETWORK_H

#include "util.hpp"
#include "routing.h"
#include "proto.h"

namespace tulip {
namespace dht {

#define OBTAIN_FUT_MSG \
    std::string v = fut.get(); \
    proto::msg m; \
    std::memcpy(reinterpret_cast<char*>(&m), v.c_str(), v.size());

struct peer {
    friend boost::serialization::access;

    hash_t id;
    std::string addr;
    u16 port;
    u16 reply_port;

    int staleness;

    peer() { }

    peer(hash_t id_) : id(id_) { }
    
    peer(std::string a, u16 p, u16 rp, hash_t id_) : 
        addr(a), 
        port(p), 
        reply_port(rp), 
        staleness(0),
        id(id_) { }

    peer(std::string a, u16 p, u16 rp) : 
        addr(a), 
        port(p), 
        reply_port(rp), 
        staleness(0),
        id(0) { }

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
    void serialize(Archive& ar, const u32 version) {
        ar & addr;
        ar & port;
        ar & reply_port;
        ar & id;
    }

    bool operator==(const peer& rhs) const {
        return !addr.compare(rhs.addr) && 
            port == rhs.port && 
            reply_port == rhs.reply_port;
    }

    hash_t distance(hash_t from) { return id ^ from; }
};

struct store_data {
    friend boost::serialization::access;

    hash_t key;
    std::string data;

    template <class Archive>
    void serialize(Archive& ar, const u32 version) {
        ar & key;
        ar & data;
    }
};


struct pending_item {
    peer req;
    hash_t msg_id;
    proto::actions action;
    std::promise<std::string> promise;
    bool satisfied;
    bool rp;
    pending_item(peer, hash_t, proto::actions, bool);
};

struct pending_result {
    peer req;
    hash_t msg_id;
    proto::actions action;
    pending_result(const pending_item& i) :
        req(i.req), msg_id(i.msg_id), action(i.action) { }
};

class rp_node_peer;
class rp_node;
class node;

using pend_it = std::list<pending_item>::iterator;
// server error callback
using se_callback = std::function<void(peer)>;
// peer callback
using p_callback = std::function<void(std::future<std::string>, pending_result)>;
// boost asio callback
using ba_callback = std::function<void(boost::system::error_code, std::size_t)>;

using rpn_ptr = std::shared_ptr<rp_node_peer>;

/// @brief a TCP connection
class rp_node_peer :
    public std::enable_shared_from_this<rp_node_peer> {
public:
    rp_node_peer(rp_node&, tcp::socket);
    rp_node_peer(rp_node&, tcp::endpoint);
    
    void handle();
    void write(proto::rp_msg, std::string, peer, se_callback, se_callback);

private:
    tcp::socket socket;
    tcp::endpoint endpoint;
    proto::rp_msg m_in;
    proto::rp_msg m_out;
    std::string buf;
    rp_node& rp_node_;
};

/// @brief a TCP server/client
class rp_node {
public:
    rp_node(node&, u16);
    ~rp_node();

    void loop();
    void start();

    void send(peer, hash_t, std::string, se_callback, se_callback);

    node& node_;
    u16 port;
    boost::asio::io_context ioc;
    std::thread ioc_thread;
    tcp::acceptor acceptor;
};

/// @brief a class containing both the TCP and UDP servers
class node {
public:
    node(u16, u16);
    ~node();

    void start();

    hash_t send(bool, proto::context, proto::responses, peer, proto::actions, u64, p_callback, p_callback);
    hash_t send(bool, proto::context, proto::responses, peer, proto::actions, u64, hash_t, p_callback, p_callback);

    p_callback p_do_nothing = [](std::future<std::string>, dht::pending_result) { };
    se_callback se_do_nothing = [](peer) { };

    template <proto::actions a>
    void okay(std::future<std::string>, pending_result);

    template <proto::actions a>
    void bad(std::future<std::string>, pending_result);

    void await(peer, hash_t, proto::actions, bool, p_callback, p_callback);
    void await_ack(peer, hash_t);
    
    hash_t id;
    u16 port;
    routing_table table;
    std::mutex pending_mutex;
    std::list<pending_item> pending;
    boost::asio::io_context ioc;
    std::thread ioc_thread;

protected:
    ba_callback ba_do_nothing = [&](boost::system::error_code ec, std::size_t sz) { };

    void loop();
    
    void wait(pend_it, p_callback, p_callback);

    template <proto::actions a>
    void reply(peer); // reply to a request

    hash_t send(bool, proto::context, peer, proto::actions, p_callback, p_callback);

    rp_node rp_node_;
    udp::socket socket;
    udp::endpoint client;
    std::mutex m_in_mutex;
    std::mutex m_out_mutex;
    proto::msg m_in;
    proto::msg m_out;
    std::random_device rd;
    std::default_random_engine reng;
};

}
}

#endif