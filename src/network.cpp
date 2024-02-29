#include "network.hpp"
#include "bucket.hpp"
#include "proto.hpp"
#include "routing.hpp"
#include "util.hpp"

namespace lotus {
namespace dht {

/// message queue

void msg_queue::await(net_peer p, int action, u64 msg_id, q_callback ok, f_callback bad) {
    LOCK(mutex);

    items.emplace_back(p, msg_id, action, false);
    auto pit = items.end();

    std::thread(&msg_queue::wait, this, --pit, ok, bad).detach();
}

void msg_queue::wait(std::list<item>::iterator it, q_callback ok, f_callback bad) {
    try {
        if(it == items.end()) {
            bad(empty_net_peer);
            return; // bad iterator
        }

        std::future<std::string> fut = it->promise.get_future();
        std::future_status fs = fut.wait_for(seconds(proto::net_timeout));
        
        // since every action is one query-response we don't need to feed callback the message ID
        net_peer req(empty_net_peer);

        {
            LOCK(mutex);
            req = it->req;
            items.erase(it);
        }

        switch(fs) {
        case std::future_status::ready:
            ok(req, fut.get());
            break;

        case std::future_status::deferred:
        case std::future_status::timeout:
            bad(req);
            break;
        }

    } catch (std::future_error& e) {
        bad(it->req);
        
        {
            LOCK(mutex);
            items.erase(it);
        }
    }
}

void msg_queue::satisfy(net_peer p, int action, u64 msg_id, std::string data) {
    LOCK(mutex);

    auto it = std::find_if(items.begin(), items.end(),
        [&](const item& i) { 
            return (i.req.id == p.id || 
                i.req.addr == p.addr) && 
                i.action == action &&
                i.msg_id == msg_id && 
                !i.satisfied; 
        });

    if(it == items.end())
        return;

    it->satisfied = true;
    it->req = p;
    it->promise.set_value(data);
}

bool msg_queue::pending(net_peer p, int action, u64 msg_id) {
    LOCK(mutex);

    auto it = std::find_if(items.begin(), items.end(),
        [&](const item& i) { 
            return i.req.addr == p.addr && 
                i.action == action &&
                i.msg_id == msg_id && 
                !i.satisfied;  
        });

    return it != items.end();
}

/// networking

template <typename Fwd>
network<Fwd>::network(bool local_, u16 p, h_callback handler) :
    local(local_),
    port(p),
    socket(ioc, udp::endpoint(udp::v4(), p)),
    message_handler(handler) {
    fwd_.initialize(false); /// @todo consider ipv6?
}

template <typename Fwd>
network<Fwd>::~network() {
    if(release_thread.joinable()) release_thread.join();
    if(ioc_thread.joinable()) ioc_thread.join();
}

template <typename Fwd>
void network<Fwd>::run() {
    release_thread = std::thread([&, this]() {
        while(!local) {
            if(!fwd_.forward_port("dht", u_UDP, port)) {
                spdlog::error("upnp: failed to re-lease port mapping");
            }

            std::this_thread::sleep_for(seconds(constants::upnp_release_interval));
        }
    });

    recv();
    ioc_thread = std::thread([&, this]() { ioc.run(); });
}

template <typename Fwd>
void network<Fwd>::recv() {
    socket.async_wait(udp::socket::wait_read,
        [this](boost::system::error_code ec) {
            if(ec) goto bad;

            {
                udp::socket::bytes_readable readable(true);
                socket.io_control(readable, ec);

                if(!ec) {
                    std::string buf;
                    std::size_t len = readable.get();

                    // messages larger than the maximum allowed size will be discarded
                    if(len > proto::max_data_size || len == 0)
                        goto bad;

                    buf.resize(len);

                    socket.receive_from(boost::asio::buffer(buf), endpoint, 0, ec);

                    if(!ec) {
                        handle(std::move(buf), endpoint);
                    }
                }
            }

            bad: recv();
        });
}

template <typename Fwd>
void network<Fwd>::handle(std::string buf, udp::endpoint ep) {
    try {
        proto::message msg = util::deserialize<proto::message>(buf);

        net_peer p{ util::dec58(msg.i), net_addr("udp", ep.address().to_string(), ep.port()) };

        // if there's already a response pending, drop this one
        // except if it's an identify request
        if(msg.m == proto::type::query && 
            queue.pending(p, msg.a, msg.q) && 
            msg.a != proto::actions::identify &&
            msg.a != proto::actions::get_addresses) {
            return;
        }
           
        message_handler(std::move(p), std::move(msg));
    } catch (std::exception& e) { spdlog::debug("exception caught: {}", e.what()); }
}

// for real case
EINST(network, upnp);

///// FOR TESTS

// for test case
EINST(network, test::mock_forwarder);

namespace test {

// mock_network

mock_network::mock_network(bool, u16, h_callback) : network(true, 0, [](net_peer, proto::message){}) { }

mock_network::~mock_network() { }

// mock_rt_net_resp

template <>
void mock_rt_net_resp::send(bool, const net_addr& addr, int, int, hash_t, u64, msgpack::type::nil_t, msg_queue::q_callback ok, msg_queue::f_callback) {
    ok(net_peer(0, addr), "");
}

template <>
void mock_rt_net_resp::send(bool, std::vector<net_addr> addrs, int, int, hash_t, u64, msgpack::type::nil_t, msg_queue::q_callback ok, msg_queue::f_callback) {
    ok(net_peer(0, addrs.front()), "");
}

// mock_rt_net_unresp

template <>
void mock_rt_net_unresp::send(bool, const net_addr& addr, int, int, hash_t, u64, msgpack::type::nil_t, msg_queue::q_callback, msg_queue::f_callback bad) {
    bad(net_peer(0, addr));
}

template <>
void mock_rt_net_unresp::send(bool, std::vector<net_addr> addrs, int, int, hash_t, u64, msgpack::type::nil_t, msg_queue::q_callback, msg_queue::f_callback bad) {
    bad(net_peer(0, addrs.front()));
}

// mock_rt_net_maybe

template <>
void mock_rt_net_maybe::send(bool, const net_addr& addr, int, int, hash_t, u64, msgpack::type::nil_t, msg_queue::q_callback ok, msg_queue::f_callback bad) {
    if(std::rand() & 1)
        ok(net_peer(0, addr), "");
    else
        bad(net_peer(0, addr));
}

template <>
void mock_rt_net_maybe::send(bool, std::vector<net_addr> addrs, int, int, hash_t, u64, msgpack::type::nil_t, msg_queue::q_callback ok, msg_queue::f_callback bad) {
    if(std::rand() & 1)
        ok(net_peer(0, addrs.front()), "");
    else
        bad(net_peer(0, addrs.front()));
}

}

}
}