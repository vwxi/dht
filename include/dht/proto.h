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

const int ML = 4; // magic length in bytes
const int NL = 5; // hash width in unsigned ints
const int K = 4;   // number of entries in k-buckets (SHOULD NOT BE OVER 20)
const int I = 160; // hash width in bits
const int M = 3; // number of missed pings allowed
const int G = 3; // number of missed messages allowed
const int T = 3; // number of seconds until timeout
const int C = 3; // number of peers allowed in bucket replacement cache at one time
const u64 MS = 65535; // max data size in bytes

enum actions {
    ping = 0,
    find_node = 1,
    find_value = 2,
    store = 3,
    ack = 4
};

enum context {
    request = 0,
    response = 1
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

    unsigned int msg_id[NL];

    u8 action;
    u8 reply;
    u8 response;
    u16 reply_port;

    u64 sz;
} const ping_request = {
    .action = proto::actions::ping,
    .reply = 0,
    .response = proto::responses::ok,
    .sz = 0
};

struct rp_msg {
    u8 magic[ML];

    unsigned int msg_id[NL];

    u8 ack;
    u16 msg_port;

    u64 sz;
};

}

}

#endif