#ifndef _PROTO_H
#define _PROTO_H

#include "util.hpp"

namespace tulip {
namespace dht {

namespace proto { // protocol

const int schema_version = 0;

enum actions {
    ping = 0,
    store = 1,
    find_node = 2,
    find_value = 3
};

enum type {
    query = 0,
    response = 1
};

struct find_query_data {
    std::string t;
    MSGPACK_DEFINE_MAP(t);
};

// store

struct store_query_data {
    std::string k;
    std::string v;
    MSGPACK_DEFINE_MAP(k, v);
};

struct store_response_data {
    int ok;
    MSGPACK_DEFINE_MAP(ok);
};

// find_node

struct bucket_peer {
    std::string a;
    int p;
    std::string i;
    MSGPACK_DEFINE_MAP(a, p, i);
};

struct find_node_resp_data {
    std::vector<bucket_peer> b;
    MSGPACK_DEFINE_MAP(b);
};

// find_value

struct find_value_resp_data {
    boost::optional<std::string> v;
    boost::optional<std::vector<bucket_peer>> b;
    MSGPACK_DEFINE_MAP(v, b);
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

}

}
}

#endif