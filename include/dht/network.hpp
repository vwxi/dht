#ifndef _NETWORK_H
#define _NETWORK_H

#include "util.hpp"
#include "routing.hpp"
#include "proto.hpp"
#include "upnp.hpp"

namespace lotus {
namespace dht {

class msg_queue {
public:
    using q_callback = std::function<void(net_peer, std::string)>;
    using f_callback = std::function<void(net_peer)>;

    q_callback q_nothing = [](net_peer, std::string) { };
    f_callback f_nothing = [](net_peer) { };

    void await(net_peer, int, u64, q_callback, f_callback);
    void satisfy(net_peer, int, u64, std::string);
    bool pending(net_peer, int, u64);

private:
    struct item {
        net_peer req;
        u64 msg_id;
        int action;
        std::promise<std::string> promise;
        bool satisfied;
        item(const net_peer& r, u64 m, int a, bool s) : req(r), msg_id(m), action(a), satisfied(s) { }
    };

    void wait(std::list<item>::iterator, q_callback, f_callback);

    std::mutex mutex;
    std::list<item> items;
};

template <typename Network, typename Bucket>
class node;

/// @brief interface for networking
template <typename Fwd>
class network {
public:
    using h_callback = std::function<void(net_peer, proto::message)>;

    network(bool, u16, h_callback);
    ~network();

    void run();
    void recv();

    // send to individual address
    template <typename T>
    void send(bool f, const net_addr& addr, int m, int a, hash_t i, u64 q, T d, msg_queue::q_callback ok, msg_queue::f_callback bad) {
        std::string s = prepare_message(m, a, i, q, d);

        if(f) {
            queue.await(net_peer{ 0, addr }, a, q, ok, bad);
        }

        socket.async_send_to(
            boost::asio::buffer(s.data(), s.size()), 
            addr.udp_endpoint(), b_nothing);
    }

    // send with alternate addresses
    template <typename T>
    void send(bool f, std::vector<net_addr> addresses, int m, int a, hash_t i, u64 q, T d, msg_queue::q_callback ok, msg_queue::f_callback bad) {
        std::string s = prepare_message(m, a, i, q, d);

        if(addresses.empty())
            return;

        // await a response, if none, try next address
        if(f) {
            queue.await(net_peer{ 0, *addresses.begin() }, a, q, ok, 
                [this, ad = addresses, f, m, a, i, q, d, ok, bad](net_peer p) mutable {
                    if(ad.empty())
                        bad(p);
                    else {
                        ad.erase(ad.begin());
                        if(ad.empty()) {
                            bad(p);
                            return;
                        }

                        spdlog::debug("network: message expired. trying new address {}", ad.begin()->to_string());
                        send(f, ad, m, a, i, q, d, ok, bad);
                    }
                });
        }

        // send
        socket.async_send_to(
            boost::asio::buffer(s.data(), s.size()), 
            addresses.begin()->udp_endpoint(), b_nothing);
    }

    using b_callback = std::function<void(boost::system::error_code, std::size_t)>;
    b_callback b_nothing = [](boost::system::error_code, std::size_t) { };

    msg_queue queue;
    u16 port;
    bool local;
    
    std::string get_ip_address() {
        return local ? 
            fwd_.get_local_ip_address() : 
            fwd_.get_external_ip_address();
    }

protected:
    template <typename T>
    std::string prepare_message(int m, int a, hash_t i, u64 q, T d) {
        msgpack::zone z;

        proto::message msg;
        msg.s = proto::schema_version; // s: schema
        msg.m = m; // m: message type
        msg.a = a; // a: action
        msg.i = util::enc58(i); // i: serialized ID
        msg.q = q; // q: message ID
        msg.d = msgpack::object(d, z); // d: action-specific data

        return util::serialize(msg);
    }

    void handle(std::string, udp::endpoint);

    h_callback message_handler;

    boost::asio::io_context ioc;
    std::thread ioc_thread;
    std::thread release_thread;
    udp::socket socket;
    udp::endpoint endpoint;

    Fwd fwd_;
};

///// FOR TESTS

namespace test {

class mock_network : public network<mock_forwarder> {
public:
    mock_network(bool, u16, h_callback);
    ~mock_network();
};

// network that always responds (for routing table tests)
class mock_rt_net_resp : public mock_network {
public:
    using mock_network::mock_network;

    template <typename T>
    void send(bool, const net_addr&, int, int, hash_t, u64, T, msg_queue::q_callback, msg_queue::f_callback);

    template <typename T>
    void send(bool, std::vector<net_addr>, int, int, hash_t, u64, T, msg_queue::q_callback, msg_queue::f_callback);
};

// network that never responds (for routing table tests)
class mock_rt_net_unresp : public mock_network {
public:
    using mock_network::mock_network;

    template <typename T>
    void send(bool, const net_addr&, int, int, hash_t, u64, T, msg_queue::q_callback, msg_queue::f_callback);

    template <typename T>
    void send(bool, std::vector<net_addr>, int, int, hash_t, u64, T, msg_queue::q_callback, msg_queue::f_callback);
};

// network that sometimes responds (for routing table tests)
class mock_rt_net_maybe : public mock_network {
public:
    using mock_network::mock_network;

    template <typename T>
    void send(bool, const net_addr&, int, int, hash_t, u64, T, msg_queue::q_callback, msg_queue::f_callback);

    template <typename T>
    void send(bool, std::vector<net_addr>, int, int, hash_t, u64, T, msg_queue::q_callback, msg_queue::f_callback);
};

}

}
}

#endif