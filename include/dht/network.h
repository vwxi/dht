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
    void serialize(Archive& ar, const unsigned int version) {
        ar & addr;
        ar & port;
        ar & reply_port;
        ar & id;
    }

    bool operator==(const peer& rhs) {
        return !addr.compare(rhs.addr) && 
            port == rhs.port && 
            reply_port == rhs.reply_port;
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

class rp_node_peer;
class rp_node;
class node;

using pend_it = std::list<pending_item>::iterator;
// server error callback
using se_callback = std::function<void(peer)>;
// peer callback
using p_callback = std::function<void(std::future<std::string>, peer, pend_it)>;

using rpn_ptr = std::shared_ptr<rp_node_peer>;

/// @brief a TCP connection
class rp_node_peer :
    public std::enable_shared_from_this<rp_node_peer> {
public:
    typedef std::shared_ptr<rp_node_peer> ptr;

    rp_node_peer(rp_node&, tcp::socket);

    void handle();
    void read_handler(const boost::system::error_code&, std::size_t);
    void read_data_handler(const boost::system::error_code&, std::size_t, pend_it);
    void write(proto::rp_msg, std::string, peer, se_callback, se_callback);
    void write_handler(const boost::system::error_code&, std::size_t, std::string, peer, se_callback, se_callback);

    void kill();

private:
    tcp::socket socket;
    proto::rp_msg m_in;
    proto::rp_msg m_out;
    std::string buf;

    rp_node& rp_node_;
};

/// @brief a TCP server/client
class rp_node {
public:
    rp_node(boost::asio::io_context&, node&, u16);

    void run();

    void send(peer, hash_t, std::string, se_callback, se_callback);

    node& node_;

    u16 port;

    boost::asio::io_context& ioc;
    tcp::acceptor acceptor;

    std::set<rpn_ptr> peers;
};

/// @brief a class containing both the TCP and UDP servers
class node {
public:
    node(u16, u16);
    ~node();
    
    void start();

    hash_t send(peer, proto::actions, u64, p_callback, p_callback);
    hash_t reply(peer, proto::actions, hash_t, u64, p_callback, p_callback);

    p_callback p_do_nothing = [](std::future<std::string>, peer, dht::pend_it) { };
    se_callback se_do_nothing = [](peer) { };

    template <proto::actions a>
    void okay(std::future<std::string>, peer, pend_it);

    template <proto::actions a>
    void bad(std::future<std::string>, peer, pend_it);

    void queue_current(peer, hash_t, proto::actions, bool, p_callback, p_callback);
    
    hash_t id;
    u16 port;
    routing_table table;

    std::mutex pending_mutex;
    std::list<pending_item> pending;

protected:
    void run();
    
    void wait(peer, pend_it, p_callback, p_callback);

    template <proto::actions a>
    void reply(peer); // reply to a request

    hash_t send(peer, proto::actions, p_callback, p_callback);

    boost::asio::io_context ioc;

    std::thread server_thread;
    
    rp_node rp_node_;
    
    udp::socket socket;
    udp::endpoint client;

    proto::msg m_in;
    proto::msg m_out;
};

}
}

#endif