#ifndef _PROTO_H
#define _PROTO_H

#include "util.hpp"

namespace tulip {
namespace dht {

struct net_addr {
    typedef boost::variant<tcp::endpoint, udp::endpoint> endp;

    enum {
        t_udp,
        t_tcp
    } transport_type;

    std::string addr;
    u16 port;

    net_addr(std::string t, std::string a, u16 p) : addr(a), port(p) {
        if(t == "udp") transport_type = t_udp;
        else if(t == "tcp") transport_type = t_tcp;
    }

    endp endpoint() const {
        switch(transport_type) {
        case t_udp: return udp::endpoint{boost::asio::ip::address::from_string(addr), port};
        case t_tcp: return tcp::endpoint{boost::asio::ip::address::from_string(addr), port};
        }
    }

    bool operator==(const net_addr& rhs) const {
        return transport_type == rhs.transport_type && addr == rhs.addr && port == rhs.port; 
    }

    std::string transport() const {
        switch(transport_type) {
        case t_udp: return "udp";
        case t_tcp: return "tcp";
        default: return "unknown";
        }
    }

    std::string to_string() const {
        return fmt::format("{}:{}:{}", transport(), addr, port);
    }
};

struct peer_record {
    net_addr address;
    std::string signature;
    
    peer_record(std::string t, std::string a, u16 p, std::string s) :
        address(t, a, p), signature(s) { }

    bool operator==(const peer_record& r) {
        return address == r.address && signature == r.signature;
    }
};

struct peer {
    typedef boost::variant<tcp::endpoint, udp::endpoint> endp;

    /// @todo empty for now.
    std::vector<peer_record> addresses;

    std::string transport;
    hash_t id;
    std::string addr;
    u16 port;
    int staleness;
    boost::optional<std::string> pub_key;

    peer() = default;
    peer(hash_t id_) : id(id_) { }
    peer(std::string t, std::string a, u16 p, hash_t id_) : 
        transport(t), addr(a), port(p), staleness(0), id(id_) { 
        
    }
    
    peer(std::string t, std::string a, u16 p) : 
        transport(t), addr(a), port(p), staleness(0), id(0) { 

    }
    
    // hacky
    peer(hash_t id_, peer_record r) : 
        transport(r.address.transport()), id(id_), 
        addr(r.address.addr), port(r.address.port), staleness(0) { 
        addresses.push_back(r);
    }
    
    tcp::endpoint t_endpoint() const { return tcp::endpoint{boost::asio::ip::address::from_string(addr), port}; }
    udp::endpoint u_endpoint() const { return udp::endpoint{boost::asio::ip::address::from_string(addr), port}; }
    endp endpoint() const { return !transport.compare("udp") ? endp(u_endpoint()) : (!transport.compare("tcp") ? endp(t_endpoint()) : endp()); } 
    bool operator==(const peer& rhs) const { return !transport.compare(rhs.transport) && !addr.compare(rhs.addr) && port == rhs.port; }
    std::string operator()() { return fmt::format("{}:{}:{}:{}", transport, addr, port, util::b58encode_h(id)); }
};

namespace proto { // protocol

const int schema_version = 0;

enum actions {
    ping = 0,
    store = 1,
    find_node = 2,
    find_value = 3,
    pub_key = 4,
    get_addresses = 5
};

enum type {
    query = 0,
    response = 1
};

enum status {
    ok = 0,
    bad = 1
};

enum store_type {
    data = 0,
    provider_record = 1
};

struct peer_object {
    std::string t;
    std::string a;
    int p;
    std::string i;
    MSGPACK_DEFINE_MAP(t, a, p, i);
    peer_object() { }
    peer_object(std::string t_, std::string a_, int p_, std::string i_) : t(t_), a(a_), p(p_), i(i_) { }
    peer_object(peer p_) : t(p_.transport), a(p_.addr), p(p_.port), i(util::b58encode_h(p_.id)) { }
    peer to_peer() const { return peer(t, a, p, util::b58decode_h(i)); }
};

struct stored_data {
    int d;
    std::string v;
    peer_object o;
    u64 t;
    std::string s;
    MSGPACK_DEFINE_MAP(d, v, o, t, s);
};

struct find_query_data {
    std::string t;
    MSGPACK_DEFINE_MAP(t);
};

// store

struct store_query_data {
    std::string k;
    int d;
    std::string v;
    boost::optional<peer_object> o;
    u64 t;
    std::string s;
    MSGPACK_DEFINE_MAP(k, d, v, o, t, s);
};

struct store_resp_data {
    u32 c;
    int s;
    MSGPACK_DEFINE_MAP(c, s);
};

// find_node

struct find_node_resp_data {
    std::vector<peer_object> b;
    std::string s;
    MSGPACK_DEFINE_MAP(b, s);
};

// find_value

struct find_value_resp_data {
    boost::optional<stored_data> v;
    boost::optional<find_node_resp_data> b;
    MSGPACK_DEFINE_MAP(v, b);
};

// pub_key

struct pub_key_query_data {
    std::string s;
    MSGPACK_DEFINE_MAP(s);
};

struct pub_key_resp_data {
    std::string k;
    std::string s;
    MSGPACK_DEFINE_MAP(k, s);
};

// get_addresses

struct get_addresses_peer_record {
    std::string t;
    std::string a;
    int p;
    std::string s;
    MSGPACK_DEFINE_MAP(t, a, p, s);
};

struct get_addresses_query_data {
    std::string i;
    MSGPACK_DEFINE_MAP(i);
};

struct get_addresses_resp_data {
    std::string i;
    std::vector<get_addresses_peer_record> p;
    MSGPACK_DEFINE_MAP(i, p);
};

struct message {
    int s;
    int m;
    int a;
    std::string i;
    u64 q;
    msgpack::object d;
    MSGPACK_DEFINE_MAP(s, m, a, i, q, d);
};

// sig blob

struct sig_blob {
    std::string k;
    int d;
    std::string v;
    std::string i;
    u64 t;
    MSGPACK_DEFINE_MAP(k, d, v, i, t);
};

}

}
}

#endif