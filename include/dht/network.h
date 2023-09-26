#ifndef _NETWORK_H
#define _NETWORK_H

#include "util.hpp"
#include "routing.h"
#include "proto.h"

namespace tulip {
namespace dht {

class msg_queue {
public:
    using q_callback = std::function<void(peer, std::string)>;
    using f_callback = std::function<void(peer)>;

    q_callback q_nothing = [](peer, std::string) { };
    f_callback f_nothing = [](peer) { };

    void await(peer, u64, q_callback, f_callback);
    void satisfy(peer, u64, std::string);
    bool pending(peer, u64);

private:
    struct item {
        peer req;
        u64 msg_id;
        std::promise<std::string> promise;
        bool satisfied;
        item(peer r, u64 m, bool s) : req(r), msg_id(m), satisfied(s) { }
    };

    void wait(std::list<item>::iterator, q_callback, f_callback);

    std::mutex mutex;
    std::list<item> items;
};

class node;

/// @brief interface for networking
class network {
public:
    using m_callback = std::function<void(peer, proto::message)>;

    network(u16, m_callback, m_callback, m_callback, m_callback);
    ~network();

    void run();
    void recv();

    template <typename T>
    void send(bool f, peer p, int m, int a, hash_t i, u64 q, T d, msg_queue::q_callback ok, msg_queue::f_callback bad) {
        // pack message
        msgpack::zone z;

        proto::message msg;
        msg.s = proto::schema_version; // s: schema
        msg.m = m; // m: message type
        msg.a = a; // a: action
        msg.i = util::b58encode_h(i); // i: serialized ID
        msg.q = q; // q: message ID
        msg.d = msgpack::object(d, z); // d: action-specific data
        
        msgpack::sbuffer sb;
        msgpack::pack(sb, msg);

        // await a response
        if(f)
            queue.await(p, q, ok, bad);

        // send
        socket.async_send_to(boost::asio::buffer(sb.data(), sb.size()), p.endpoint(), b_nothing);
    }

    using b_callback = std::function<void(boost::system::error_code, std::size_t)>;
    b_callback b_nothing = [](boost::system::error_code, std::size_t) { };

    msg_queue queue;
    u16 port;
    
private:
    void handle(std::string, udp::endpoint);

    m_callback handle_ping;
    m_callback handle_store;
    m_callback handle_find_node;
    m_callback handle_find_value;

    boost::asio::io_context ioc;
    std::thread ioc_thread;
    udp::socket socket;
    udp::endpoint endpoint;
};

}
}

#endif