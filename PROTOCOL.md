# DHT protocol

this document will detail the information send between nodes on the network.

## size definitions

for the sake of brevity,  

```
typedef unsigned long long int u64;
typedef unsigned long int u32;
typedef unsigned short u16;
typedef unsigned char u8;
```

## software constants

these are constants within the software that you will have to change if you wish to make any modifications to the protocol:  
as defined in `include/dht/util.hpp` and `include/dht/proto.h`:

- magic length in bytes (`magic_length`) (default: 4)
- magic bytes (`consts.magic`) (default: `b0 0b 1e 55`)
- hash width in u32s (`u32_hash_width`) (default: 5)
- number of peer entries allowed in one bucket (`K`) (default: 4) (CHANGE!)
- hash width in bits (`bit_hash_width`) (default: 160)
- number of missed pings allowed (`missed_pings_allowed`) (default: 3) (CHANGE!)
- number of missed messages allowed (`missed_messages_allowed`) (default: 3) (CHANGE!)
- number of seconds until timeout (`net_timeout`) (default: 10) (CHANGE!)
- number of candidate peers allowed in replacement cache (`repl_cache_size`) (default: 3)
- max data size in bytes (`max_data_size`) (default: 65535) (CHANGE?)
- `a` value from kademlia paper (`alpha`) (default: 3)

## infrastructure

every peer has two sockets open for messaging, a UDP and a TCP socket.   
UDP messages are meant for messages described below, and the TCP socket should only serve to receive and send larger packets of data.  

these sockets should run on different threads, as to not cause blocking.

## messages

one message per client should be handled at any time. a client should not have more than one message actively being processed.  

### message context

are we requesting, responding or are we doing something else? these are represented as `u8`:

- `0x00`: messages that request information (request)
- `0x01`: messages that respond to requests for information (response)
- `0x02`: message that acknowledges receiving data (ack)

### message actions

messages require an action to be associated with them. these are represented as `u8`:

- `0x00`: check if recipient is still online (ping)
- `0x01`: request the `K` closest peers to an ID (find_node)
- `0x02`: find a value in the network hash table based on the key (find_value)
- `0x03`: store a value in the recipient's local hash table (store)
- `0x04`: acknowledge receiving data (ack)

### message response codes

messages must respond with a response code to determine success or failure. these are represented as `u8`:

- `0x00`: success (ok)
- `0x01`: generic internal error (bad_internal)

## message formats

the reply-back port is the TCP port.  
the messaging port is the UDP port.  

### UDP message format

| `struct msg`                 |
|------------------------------|
| magic (`u8` x `magic_length`)|
| id (`u32_hash_width`-`u32`s) |
| msg id (`u32_hash_width`-`u32`s)|
| action (`u8` x 1)            |
| context (`u8` x 1)           |
| response (`u8` x 1)          |
| messaging port (`u16` x 1)   |
| reply-back port (`u16` x 1)  |
| payload size (`u64` x 1)     |

***NOTE:*** if payload size is non-zero, it is implied that the recipient will be receiving more data (exactly `payload size` bytes) over the TCP socket.

***NOTE:*** if the requester sends a request while the requester has a pending operation, the responder will refuse the request.

### TCP message format

| `struct rp_msg`              |
|------------------------------|
| magic (`u8` x `magic_length`)|
| id (`u32_hash_width`-`u32`s) |
| msg id (`u32_hash_width`-`u32`s)|
| messaging port (`u32` x 1)   |
| reply-back port (`u32` x 1)  |
| payload size (`u64` x 1)     |

after receiving an `rp_msg`, the TCP socket should be ready to receive `payload size` bytes.  
to acknowledge that the data was received, send a `msg` with the `context` set to `0x02` (ack).

## message sequences

***NOTE:*** message id's should be used for whole message sequences. however, these should be UNIQUE! do not reuse message ids. you have 2^`bit_hash_width` different IDs to use...use them!  

***NOTE:*** any non data transfer related messages should have payload size set to 0.

***NOTE:*** all messages after the first message must be *responses*

### legend

```
LEGEND:
    ----> means requester
    <---- means responder
```

### ping

```
UDP         TCP    Action
----->             requester sends ping msg
<-----             responder replies with ping msg response
```

### store

```
UDP         TCP    Action
----->             requester sends store msg with size
<-----             responder replies with store msg ok response (no data)
         ----->    requester sends rp_msg detailing payload size
         ----->    requester sends serialized store_data
<-----             responder replies with msg acknowledging (action: ack, context: response)
```

### find_node

```
UDP         TCP    Action
----->             requester sends find_node msg 
<-----             responder replies with find_node msg ok response (no data)
         ----->    requester sends rp_msg detailing payload size
         ----->    requester sends serialized node to find (u32_hash_width u32s)
<-----             responder replies with a msg detailing payload size (response)
----->             requester replies with details msg ok response (no data)
         <-----    responder sends rp_msg detailing payload size and other information
         <-----    responder sends actual payload (serialized bucket)
----->             requester replies with a msg acknowledging (action: ack, context: response)
```