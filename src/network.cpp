#include "network.h"
#include "bucket.h"
#include "proto.h"
#include "routing.h"
#include "util.hpp"

namespace tulip {
namespace dht {

/// message queue

void msg_queue::await(peer p, u64 msg_id, q_callback ok, f_callback bad) {
    LOCK(mutex);

    items.emplace_back(p, msg_id, false);
    auto pit = items.end();

    std::thread(&msg_queue::wait, this, --pit, ok, bad).detach();
}

void msg_queue::wait(std::list<item>::iterator it, q_callback ok, f_callback bad) {
    try {
        if(it == items.end()) {
            bad(peer());
            return; // bad iterator
        }

        std::future<std::string> fut = it->promise.get_future();
        std::future_status fs = fut.wait_for(seconds(proto::net_timeout));
        
        // since every action is one query-response we don't need to feed callback the message ID
        peer req;

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

void msg_queue::satisfy(peer p, u64 msg_id, std::string data) {
    LOCK(mutex);

    auto it = std::find_if(items.begin(), items.end(),
        [&](const item& i) { return i.req == p && i.msg_id == msg_id && !i.satisfied; });

    if(it == items.end()) {
        return;
    }

    it->satisfied = true;
    it->req = p;
    it->promise.set_value(data);
}

bool msg_queue::pending(peer p, u64 msg_id) {
    LOCK(mutex);

    auto it = std::find_if(items.begin(), items.end(),
        [&](const item& i) { return i.req == p && i.msg_id == msg_id && !i.satisfied; });

    return it != items.end();
}

/// networking

network::network(
    u16 p,
    m_callback handle_ping_,
    m_callback handle_store_,
    m_callback handle_find_node_,
    m_callback handle_find_value_) :
    port(p),
    socket(ioc, udp::endpoint(udp::v4(), p)),
    handle_ping(handle_ping_),
    handle_store(handle_store_),
    handle_find_node(handle_find_node_),
    handle_find_value(handle_find_value_) { }

network::~network() {
    ioc_thread.join();
}

void network::run() {
    recv();
    ioc_thread = std::thread([&, this]() { ioc.run(); });
}

void network::recv() {
    socket.async_wait(udp::socket::wait_read,
        [this](boost::system::error_code ec) {
            if(ec) goto bad;

            {
                udp::socket::bytes_readable readable(true);
                socket.io_control(readable, ec);

                if(!ec) {
                    std::string buf;
                    auto len = readable.get();

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

void network::handle(std::string buf, udp::endpoint ep) {
    msgpack::object_handle result;

    msgpack::unpack(result, buf.c_str(), buf.size());
    msgpack::object obj(result.get());

    proto::message msg = obj.as<proto::message>();

    try {
        peer p(ep.address().to_string(), ep.port(), hash_t(util::to_bin(msg.i)));
        
        // if there's already a response pending, drop this one
        if(msg.m == proto::type::query && queue.pending(p, msg.q))
            return;

        switch(msg.a) {
        case proto::actions::ping: handle_ping(std::move(p), std::move(msg)); break;
        case proto::actions::store: handle_store(std::move(p), std::move(msg)); break;
        case proto::actions::find_node: handle_find_node(std::move(p), std::move(msg)); break;
        case proto::actions::find_value: handle_find_value(std::move(p), std::move(msg)); break;
        }
    } catch (std::exception& e) { spdlog::error("handle exception: {}", e.what()); }
}


}
}