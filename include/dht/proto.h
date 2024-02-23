#ifndef _PROTO_H
#define _PROTO_H

#include "util.hpp"

namespace lotus {
namespace dht {

namespace proto { // protocol

const int schema_version = 0;

enum actions {
    ping = 0,
    store = 1,
    find_node = 2,
    find_value = 3,
    identify = 4,
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
    peer_object(net_peer p_) : t(p_.addr.transport()), a(p_.addr.addr), p(p_.addr.port), i(util::enc58(p_.id)) { }
    net_peer to_peer() const { return net_peer{ util::dec58(i), net_addr(t, a, p) }; }
};

struct provider_record {
    std::string i;
    u64 e;
    std::string s;
    MSGPACK_DEFINE_MAP(i, e, s);
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

// identify

struct identify_query_data {
    std::string s;
    MSGPACK_DEFINE_MAP(s);
};

struct identify_resp_data {
    std::string k;
    std::string s;
    MSGPACK_DEFINE_MAP(k, s);
};

// get_addresses

struct address_object {
    std::string t;
    std::string a;
    std::string p;
    address_object() {  }
    address_object(net_addr ad) : t(ad.transport()), a(ad.addr), p(std::to_string(ad.port)) { } 
    MSGPACK_DEFINE_MAP(t, a, p);
};

struct get_addresses_query_data {
    std::string i;
    MSGPACK_DEFINE_MAP(i);
};

struct get_addresses_resp_data {
    std::string i;
    std::vector<address_object> p;
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