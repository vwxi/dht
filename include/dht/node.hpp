#ifndef _NODE_HPP
#define _NODE_HPP

#include "util.hpp"
#include "proto.hpp"

namespace dht {

class node : public std::enable_shared_from_this<node> {
public:
    node(boost::asio::io_context& ioc_,
        udp::socket& socket_,
        udp::endpoint endpoint_,
        std::vector<std::shared_ptr<node>>& pending_,
        std::mutex& pending_mutex_) : 
        ioc(ioc_),
        endpoint(endpoint_),
        addr(endpoint_.address().to_string()), 
        port(endpoint_.port()),
        socket(socket_),
        pending(pending_),
        pending_mutex(pending_mutex_),
        missed_pings(0) {
        id = util::gen_id(addr, port);
        spdlog::info("RECV: addr: {}, port: {}, id: {}", addr, port, util::htos(id));
    }

public:
    virtual void send_alive() {
        m_out = {
            .action = proto::actions::ping,
            .reply = 0,
            .response = 0,
            .sz = 0
        };

        std::memset(&m_in, 0, sizeof(proto::msg));

        std::memcpy(&m_out.magic, proto::consts.magic, 4);
        util::nonce(m_out.nonce);
        
        auto self(shared_from_this());
        socket.async_send_to(boost::asio::buffer(&m_out, sizeof(proto::msg)), endpoint, 
            [this, self](boost::system::error_code ec, std::size_t sz) {
                if(!ec) {
                    std::lock_guard<std::mutex> lock(pending_mutex);
                    auto it = std::find_if(pending.begin(), pending.end(),
                        [&](std::shared_ptr<node> p) { return p->id == id; });
                    if(it == pending.end())
                        pending.push_back(shared_from_this());
                    else
                        (*it)->missed_pings++;
                }
            });
    }

    hash_t id;
    std::string addr;
    u16 port;
    
    int missed_pings;

    std::vector<std::shared_ptr<node>>& pending;
    std::mutex& pending_mutex;

private:
    boost::asio::io_context& ioc;

    udp::socket& socket;
    udp::endpoint endpoint;

    proto::msg m_in;
    proto::msg m_out;
};

}

#endif