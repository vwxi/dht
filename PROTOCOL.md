# DHT protocol

this document will detail the information send between nodes on the network.

## software constants

these are constants within the software that you will have to change if you wish to make any modifications to the protocol:  
as defined in `include/dht/util.hpp` and `include/dht/proto.h`:

- hash width in u32s (`u32_hash_width`) (default: 5)
- number of peer entries allowed in one bucket (`bucket_size`) (default: 20)
- hash width in bits (`bit_hash_width`) (default: 256)
- number of missed pings allowed (`missed_pings_allowed`) (default: 3)
- number of missed messages allowed (`missed_messages_allowed`) (default: 3)
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
- quorum for alternative lookups (`quorum`) (default: 3)
- length of secret tokens (`token_length`) (default: 32)
- maximum number of addresses allowed for a single ID (`table_entry_addr_limit`) (default: 10)

## messages

one message per client should be handled at any time. a client should not have more than one message actively being processed.  

### message skeleton

- messages are encoded using messagepack but this document will describe messages in JSON as they are identical.  
- messages missing any of these elements will be discarded  
- messages larger than the maximum allowed size will be discarded
- sending a query before sending back a response will cause the query to be discarded
  - ***EXCEPTION:*** for public key exchanges from messages like `identify` 


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
{ "t": <transport>, "a": <address>, "p": <port>, "i": <enc-string> }
```

transport is a string, either "udp" or "tcp", denoting transport protocol to be used

#### schema version

as of the writing of this document, schema version will always be `0x00` until changes are made to the protocol

#### message type

there are two types of messages:

- queries (`0x00`)
- responses (`0x01`)

#### action

these are the actions:

- ping (`0x00`)
- store (`0x01`)
- find_node (`0x02`)
- find_value (`0x03`)
- identify (`0x04`)
- get_addresses (`0x05`)

#### store types

you can store different types of data, which are all handled the same over protocol but  
can be handled differently:

- data (`0x00`)
  - this is just raw data, to be interpreted by clients
- provider record (`0x01`)
  - this is like a pointer to data, pointing to a peer that will provide this data.
    the data itself is simply a serialized peer object.
- peer record (`0x02`)
  - this is a signed record attributing a peer ID to an IP address
  - they follow the following format:
    ```
    { "t": <transport>, "a": <address>, "p": <port>, "s": <signature> }
    ```
    - where transport is a string (`udp` or `tcp`) denoting which transport protocol to use and the signature is raw binary data signing the following format: `transport:ip:port`


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
        "d": <store type>
        "v": <binary data>
        "o": <origin>
        "t": <timestamp>,
        "s": <signature>
}
```

for recipient,
```
"d": {
        "c": <checksum>,
        "s": <status>
}
```

where:
- the key (enc-string)
- store type (see above)
- binary data (string)
- origin (peer object or nil)
- timestamp (64-bit integer timestamp)
- signature (binary data)
- checksum (32-bit integer)
- status (integer, zero = ok, nonzero = error)

if the origin is nil, then the sender is the origin of the key-value pair

the signature is raw binary data which details a signing of an encoded map object   
with the following syntax using the origin's private key:

```
{
        "k": <key>,
        "d": <store type>
        "v": <binary data>,
        "i": <origin's ID ('i' object)>,
        "t": <timestamp>
}
```

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
                "d": 0,
                "v": a3 e5 1d 0f 9e ... 6e 77 3a 0e 9f,
                "o": { "t": "udp", "a": "127.0.0.1", "p": 10001, "i": "1vxEC" }
                "t": 15019835313561,
                "s": ff ff ff ff 00 ... 0e 1a f4 3f dd
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
                {"t": <transport>, "a": <IP address>, "p": <UDP port>, "i": <ID> }, 
                {"t": <transport>, "a": <IP address>, "p": <UDP port>, "i": <ID> },
                {"t": <transport>, "a": <IP address>, "p": <UDP port>, "i": <ID> }
        ],
        "s": <signature>
}
```

where:
- IP addresses are strings, ports are integers and IDs are enc-strings
- the signature signs the encoded `b` object containing the elements

if there are no nearby nodes, the bucket may be empty

***TECHNICAL NOTE:*** if the signature is invalid, remove the peer's public key from the local keystore

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
                        {"t": "udp", "a": "24.30.210.11", "p": 16616, "i": "12JZzN" }, 
                        {"t": "udp", "a": "1.1.51.103", "p": 10510, "i": "5NbYrm" }
                ],
                "s": ff ff ff ff 00 ... 0e 1a f4 3f dd
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
              "t": <timestamp>,
              "s": <signature>
        } OR nil,
        "b": <nearest nodes> OR nil
}
```

where:
- v (nil if value not found):
  - stored value (string)
  - origin (peer object)
  - timestamp (64-bit integer)
  - signature (raw data signature identical to that in the `store` message details)
- nearest nodes (bucket object if value is not found, nil if value found), represented as such:
```
"b": {
        "b": [
                <list of nodes>
        ],
        "s" <signature>
}
```

as seen above in the `find_node` response.

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
                        "o": {"t": "udp", "a": "127.0.0.1", "p": 16006, "i": "gaofe"},
                        "t": 196182340981,
                        "s": ff ff ff ff 00 ... 0e 1a f4 3f dd
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
                "b": {
                        "b": [
                                {"t": "udp", "a": "24.30.210.11", "p": 16616, "i": "12JZzN" }, 
                                {"t": "udp", "a": "1.1.51.103", "p": 10510, "i": "5NbYrm" }       
                        ],
                        "s": ff ff ff ff 00 ... 0e 1a f4 3f dd
                },
        }
```

### identify (`0x04`)

this message is very simple. peer queries for public key in BER(?) key format (used in crypto++).  this function will also validate a peer's IP address.

if a key does not exist in the keystore for the node ID, then an identify query should be sent to the sending node as to acquire their public key. if there were complications in the process or the key was invalid, the node's request should be ignored entirely.

message and key-value signatures.

#### action-specific data

sender uses the following format:

```
"d": {
        "s": <secret token>
}
```

recipient uses the following format:

```
"d": {
        "k": <public key>,
        "s": <signature>
}
```

where:
- secret token is a string of `token_length` random characters
- public key is binary data 
- signature is the signature for the following string format: `secret token:IP address:port`

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

### get_addresses (`0x05`)

this message attempts to obtain valid peer records for a given peer ID from a node. responses must be peer records with valid signatures.  
this message does not update the routing table.

#### action-specific data

for sender,

```
"d": {
        "i": <peer ID>
}
```

for recipient,

```
"d": {
        "i": <peer ID>,
        "p": [<peer records>]
}
```

where:
- peer ID is an encoded string 
- `p` is an array of peer records which were already defined 

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