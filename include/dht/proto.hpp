#ifndef _PROTO_HPP
#define _PROTO_HPP

#include "util.hpp"

namespace dht {

namespace proto { // protocol

typedef unsigned long long int u64;
typedef unsigned long int u32;
typedef unsigned short u16;
typedef unsigned char u8;
using boost::uuids::detail::sha1;

const std::size_t K = 4;   // k entries in k-buckets
const std::size_t I = 160; // hash bit width
const int M = 3; // number of missed pings allowed
const int T = 5; // number of seconds to wait between each pending queue check

enum actions {
    ping = 0,
    find_node = 1,
    find_value = 2,
    store = 3
};

enum responses {
    ok = 0
};

struct {
    u8 magic[4];
} const consts = {
    .magic = {0xb0, 0x0b, 0x1e, 0x55}
};

struct msg {
    u8 magic[4];

    sha1::digest_type nonce;

    u8 action;
    u8 reply;
    u8 response;
    u64 sz;
};

}

}

#endif