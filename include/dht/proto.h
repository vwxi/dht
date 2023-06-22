#ifndef _PROTO_H
#define _PROTO_H

#include "util.hpp"

namespace tulip {
namespace dht {

namespace proto { // protocol

const u16 Mport = 16666; // messaging port (UDP)
const u16 Rport = 16667; // reply port (TCP)

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

    unsigned int id[NL];
    unsigned int msg_id[NL];

    u8 action;
    u8 reply;
    u8 response;

    u16 msg_port;
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

    unsigned int id[NL];
    unsigned int msg_id[NL];

    u8 ack;

    u16 msg_port;
    u16 reply_port;
    
    u64 sz;
};

}

}
}

#endif