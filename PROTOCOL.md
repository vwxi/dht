# DHT protocol

this document will detail the information send between nodes on the network.

## software constants

these are constants within the software that you will have to change if you wish to make any modifications to the protocol:  
as defined in `include/dht/util.hpp` and `include/dht/proto.h`:

- hash width in u32s (`u32_hash_width`) (default: 5)
- number of peer entries allowed in one bucket (`bucket_size`) (default: 20)
- hash width in bits (`bit_hash_width`) (default: 256)
- number of missed pings allowed (`missed_pings_allowed`) (default: 3) (CHANGE!)
- number of missed messages allowed (`missed_messages_allowed`) (default: 3) (CHANGE!)
- number of seconds until timeout (`net_timeout`) (default: 10) (CHANGE!)
- number of candidate peers allowed in replacement cache (`repl_cache_size`) (default: 3)
- max data size in bytes (`max_data_size`) (default: 65535)
- `a` value from kademlia paper (`alpha`) (default: 3)
- number of seconds until a bucket needs refreshing (`refresh_time`) (default: 3600)
- number of seconds until a key-value pair expires (`republish_time`) (default: 86400)
- number of seconds between each routing table refresh (`refresh_interval`) (default: 600)
- number of seconds between key-value republications (`refresh_interval`) (default: 86400)
- number of disjoint paths to take for lookups (`disjoint_paths`) (default: 3)
- size of public/private keys in bytes (`key_size`) (default: 2048)

## messages

one message per client should be handled at any time. a client should not have more than one message actively being processed.  

### message skeleton

- messages are encoded using messagepack but this document will describe messages in JSON as they are identical.  
- messages missing any of these elements will be discarded  
- messages larger than the maximum allowed size will be discarded
- sending a query before sending back a response will cause the query to be discarded


```
{
        "s": <schema version>,
        "m": <message type>,
        "a": <action>,
        "i": <enc-string>,
        "q": <message ID>,
        "d": {
                <action-specific data>
        }
}
```

peer objects are formatted like so:

```
{ "a": <address>, "p": <port>, "i": <enc-string> }
```

#### schema version

as of the writing of this document, schema version will always be `0x00` until changes are made to the protocol

#### message type

there are two types of messages:

- queries (`0x00`)
- responses (`0x01`)

#### action

there are four actions:

- ping (`0x00`)
- store (`0x01`)
- find_node (`0x02`)
- find_value (`0x03`)
- pub_key (`0x04`)

#### enc-string

enc-strings are base58-encoded strings

this protocol uses this alphabet:

```cpp
static const char b58map[] = {
  '1', '2', '3', '4', '5', '6', '7', '8',
  '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
  'H', 'J', 'K', 'L', 'M', 'N', 'P', 'Q',
  'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y',
  'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
  'h', 'i', 'j', 'k', 'm', 'n', 'o', 'p',
  'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
  'y', 'z' };
```

examples (base58 -> hex):

- `HK6dz` -> `0xb00b1e5`
- `4vygUjcYGbG` -> `0x177ff13e0a8ef567`

#### message ID

message IDs are random 64-bit integer identifiers to associate RPCs with their respective sequences  
responses must always use the message ID of their respective queries

## actions

the action-specific data and sequences for the actions are the following: 

### ping (`0x00`)

this message is meant for pinging nodes to see if they are online

#### action-specific data

there is no extra data to be sent from both parties

#### sequence

#### 1. sender sends initial query:
```
{
        "s": 0,
        "m": 0,
        "a": 0,
        "i": "HK6dz",
        "q": 103581305802345,
        "d": nil
}
```

#### 2. recipient sends response:
```
{
        "s": 0,
        "m": 1,
        "a": 0,
        "i": "bqgzy",
        "q": 103581305802345,
        "d": nil
}
```

### store (`0x01`)

this message is meant for storing key-value pairs on a specific node's hash table

#### action-specific data

for sender,
```
"d": {
        "k": <key>,
        "v": <binary data>
        "o": <origin>
}
```

for recipient,
```
"d": {
        "t": <timestamp>
        "c": <checksum>,
        "s": <status>
}
```

where:
- the key (enc-string)
- binary data (string)
- origin (peer object or nil)
- timestamp (64-bit integer timestamp)
- checksum (32-bit integer)
- status (integer, zero = ok, nonzero = error)

if the origin is nil, then the sender is the origin of the key-value pair

#### sequence

#### 1. sender sends initial query
```
{
        "s": 0,
        "m": 0,
        "a": 1,
        "i": "HK6dz",
        "q": 103581305802345,
        "d": {
                "k": "5PYPwi",
                "v": a3 e5 1d 0f 9e ... 6e 77 3a 0e 9f,
                "o": { "a": "127.0.0.1", "p": 10001, "i": "1vxEC" }
        }
}
```

#### 2. recipient sends response
```
{
        "s": 0,
        "m": 0,
        "a": 1,
        "i": "bqgzy",
        "q": 103581305802345,
        "d": {
                "t": 15019835313561,
                "c": 10010501359,
                "s": 0
        }
}
```

### find_node (`0x02`)

this message returns a list of `K` closest nodes to a given ID 

#### action-specific data

for sender,
```
"d": {
        "t": <target ID>
}
```

where the target ID is a enc-string  

for recipient, "buckets" are serialized into arrays where each element describes a peer, like so:

```
"d": {
        "b": [
                {"a": <IP address>, "p": <UDP port>, "i": <ID> }, 
                {"a": <IP address>, "p": <UDP port>, "i": <ID> },
                {"a": <IP address>, "p": <UDP port>, "i": <ID> }
        ]
}
```

where IP addresses are strings, ports are integers and IDs are enc-strings

if there are no nearby nodes, the bucket may be empty

#### sequence

#### 1. sender sends initial query

```
        "s": 0,
        "m": 0,
        "a": 2,
        "i": "HK6dz",
        "q": 103581305802345,
        "d": {
                "t": "f5PBC"
        }
```

#### 2. recipient sends response

```
{
        "s": 0,
        "m": 1,
        "a": 2,
        "i": "bqgzy",
        "q": 103581305802345,
        "d": {
                "b": [
                        {"a": "24.30.210.11", "p": 16616, "i": "12JZzN" }, 
                        {"a": "1.1.51.103", "p": 10510, "i": "5NbYrm" }
                ]
        }
}
```

### find_value (`0x03`)

this message is similar to `find_node`, it returns the `K` closest nodes to a given ID. however, if the recipient has the target ID in its hash table, it will instead return the stored value

#### action-specific data

for sender,

```
"d": {
        "t": <target ID>
}
```

where the target ID is a enc-string

for recipient,

```
"d": {
        "v": {
              "v": <stored value>,
              "o": <origin>,
              "t": <timestamp>  
        } OR nil,
        "b": <nearest nodes> OR nil
}
```

where:
- v (nil if value not found):
  - stored value (string)
  - origin (peer object)
  - timestamp (64-bit integer)
- nearest nodes (array of nodes if value not found, nil if value found)

`find_value` **cannot** return **both** a stored value and a node array, such messages should be rejected

#### sequence

#### 1. sender sends initial query

```
        "s": 0,
        "m": 0,
        "a": 3,
        "i": "HK6dz",
        "q": 103581305802345,
        "d": {
                "t": "f5PBC"
        }
```

#### 2a. recipient has target ID in hash table, sends response with value stored at key

```
        "s": 0,
        "m": 1,
        "a": 3,
        "i": "bqgzy",
        "q": 103581305802345,
        "d": {
                "v": {
                        "v": 1e e5 6a 2e 90 ... a0 e4 b7 61 d8,
                        "o": {"a": "127.0.0.1", "p": 16006, "i": "gaofe"},
                        "t": 196182340981
                },
                "b": nil
        }
```

#### 2b. recipient does not have target ID in hash table, sends response with closest nodes to target ID

```
        "s": 0,
        "m": 1,
        "a": 3,
        "i": "bqgzy",
        "q": 103581305802345,
        "d": {
                "v": nil,
                "b": [
                        {"a": "24.30.210.11", "p": 16616, "i": "12JZzN" }, 
                        {"a": "1.1.51.103", "p": 10510, "i": "5NbYrm" }       
                ]
        }
```

### pub_key (`0x04`)

this message is very simple. peer queries for public key in X.509(?) key format (used in crypto++)

#### action-specific data

there is no action-specific data for sender  
recipient uses the following format:

```
"d": {
        "k": <public key>
}
```

where:
- public key is binary data 

#### sequence

#### 1. sender sends initial query

```
        "s": 0,
        "m": 0,
        "a": 4,
        "i": "HK6dz",
        "q": 103581305802345,
        "d": nil
```

#### 2. recipient sends response with public key

```
        "s": 0,
        "m": 1,
        "a": 4,
        "i": "bqgzy",
        "q": 103581305802345,
        "d": {
                "k": ff ff ff ff ... 3e 56 7a 10
        }
```

## operations

joins, refreshes and republishes are all described in [xlattice/kademlia](https://xlattice.sourceforge.net/components/protocol/kademlia/specs.html)  

### disjoint path lookup

from the S/kademlia paper,

> We extended this algorithm to use d disjoint paths and thus increase the lookup success ratio in a network with adversarial nodes. The initiator starts a lookup by taking the k closest nodes to the destination key from his local routing table and distributes them into d independent lookup buckets. From there on the node continues with d parallel lookups similar to the traditional Kademlia lookup. The lookups are independent, except the important fact, that each node is only used once during the whole lookup process to ensure that the resulting paths are really disjoint.

---

the idea is N unique lookup paths, no interleaving nodes. the paper is pretty vague in regards to specifics but

have a list of "claimed" peers
1. start N lookups in parallel
2. for every successfully contacted peer, add to claimed list. to ensure disjointness, this peer cannot be contacted again
3. return N lookup results