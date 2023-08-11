#include "dht.h"

namespace tulip {
namespace dht {

node::node(u16 port) :
    net(port,
        std::bind(&node::handle_ping, this, _1, _2),
        std::bind(&node::handle_store, this, _1, _2),
        std::bind(&node::handle_find_node, this, _1, _2),
        std::bind(&node::handle_find_value, this, _1, _2)),
    table(id, net),
    reng(rd()) {
    std::srand(std::time(NULL));
    id = table.id = util::gen_id(reng);
    net.run();
}

/// handlers

void node::handle_ping(peer p, proto::message msg) {
    spdlog::info("weeeee!!!! {}", util::htos(p.id));
    
    if(msg.m == proto::type::query) {
        net.send(
            p, proto::type::response, proto::actions::ping, 
            id, msg.q, msgpack::type::nil_t(),
            net.queue.q_nothing, net.queue.f_nothing);

        table.update(p);
    } else if(msg.m == proto::type::response) {
        net.queue.satisfy(p, msg.q, std::string{});
    }
}

void node::handle_store(peer p, proto::message msg) {
    /// @todo handle_store
}

void node::handle_find_node(peer p, proto::message msg) {
    /// @todo handle_find_node
}

void node::handle_find_value(peer p, proto::message msg) {
    /// @todo handle_find_value
}

/// async actions

void node::ping(peer p, basic_callback ok, basic_callback bad) {
    net.send(p, proto::type::query, proto::actions::ping,
        id, util::msg_id(), msgpack::type::nil_t(),
        [this, ok](peer p_, std::string) { ok(p_); },
        [this, bad](peer p_) { bad(p_); });
}

void node::store(peer p, std::string key, std::string value, basic_callback ok, basic_callback bad) {
    
}

}
}