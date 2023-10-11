#ifndef _PROTO_H
#define _PROTO_H

#include "util.hpp"

namespace tulip {
namespace dht {

struct peer {
    hash_t id;
    std::string addr;
    u16 port;
    int staleness;
    boost::optional<std::string> pub_key;

    peer() = default;
    peer(hash_t id_) : id(id_) { }
    peer(std::string a, u16 p, hash_t id_) : addr(a), port(p), staleness(0), id(id_) { }
    peer(std::string a, u16 p) : addr(a), port(p), staleness(0), id(0) { }
    udp::endpoint endpoint() const { return udp::endpoint{boost::asio::ip::address::from_string(addr), port}; }
    bool operator==(const peer& rhs) const { return !addr.compare(rhs.addr) && port == rhs.port; }
    std::string operator()() { return fmt::format("{}:{}:{}", addr, port, util::b58encode_h(id)); }
};

namespace proto { // protocol

const int schema_version = 0;

enum actions {
    ping = 0,
    store = 1,
    find_node = 2,
    find_value = 3,
    pub_key = 4
};

enum type {
    query = 0,
    response = 1
};

enum status {
    ok = 0,
    bad = 1
};

struct peer_object {
    std::string a;
    int p;
    std::string i;
    MSGPACK_DEFINE_MAP(a, p, i);
    peer_object() { }
    peer_object(std::string a_, int p_, std::string i_) : a(a_), p(p_), i(i_) { }
    peer_object(peer p_) : a(p_.addr), p(p_.port), i(util::b58encode_h(p_.id)) { }
    peer to_peer() const { return peer(a, p, util::b58decode_h(i)); }
};

struct stored_data {
    std::string v;
    peer_object o;
    u64 t;
    std::string s;
    MSGPACK_DEFINE_MAP(v, o, t, s);
};

struct find_query_data {
    std::string t;
    MSGPACK_DEFINE_MAP(t);
};

// store

struct store_query_data {
    std::string k;
    std::string v;
    boost::optional<peer_object> o;
    u64 t;
    std::string s;
    MSGPACK_DEFINE_MAP(k, v, o, t, s);
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

struct pub_key_resp_data {
    std::string k;
    MSGPACK_DEFINE_MAP(k);
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
    std::string v;
    std::string i;
    u64 t;
    MSGPACK_DEFINE_MAP(k, v, i, t);
};

}

}
}

#endif