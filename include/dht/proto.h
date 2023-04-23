#ifndef _PROTO_H
#define _PROTO_H

#include "util.hpp"

namespace dht {

namespace proto { // protocol

typedef unsigned long long int u64;
typedef unsigned long int u32;
typedef unsigned short u16;
typedef unsigned char u8;
using boost::uuids::detail::sha1;

const int ML = 4; // magic length

const int K = 4;   // k entries in k-buckets (SHOULD NOT BE OVER 20)
const int I = 160; // hash bit width
const int M = 3; // number of missed pings allowed
const int G = 3; // number of missed messages allowed
const int T = 10; // number of seconds until timeout

enum actions {
    ping = 0,
    find_node = 1,
    find_value = 2,
    store = 3
};

enum responses {
    ok = 0,
    bad_internal = 1
};

struct {
    u8 magic[ML];
} const consts = {
    .magic = {0xb0, 0x0b, 0x1e, 0x55}
};

struct msg {
    u8 magic[ML];

    sha1::digest_type nonce;

    u8 action;
    u8 reply;
    u8 response;
    u64 sz;
} const ping_request = {
    .action = proto::actions::ping,
    .reply = 0,
    .response = proto::responses::ok,
    .sz = 0
};

}

}

#endif