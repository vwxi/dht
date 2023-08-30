# DHT protocol

this document will detail the information send between nodes on the network.

## software constants

these are constants within the software that you will have to change if you wish to make any modifications to the protocol:  
as defined in `include/dht/util.hpp` and `include/dht/proto.h`:

- hash width in u32s (`u32_hash_width`) (default: 5)
- number of peer entries allowed in one bucket (`K`) (default: 4) (CHANGE!)
- hash width in bits (`bit_hash_width`) (default: 160)
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
        "i": <serialized ID>,
        "q": <message ID>,
        "d": {
                <action-specific data>
        }
}
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

#### serialized ID

the serialized ID will be a hex-string, for example:

- `01234567abcdef`
- `1b1b30aeb0df0ed0f0c0ba03548135`

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
        "i": "0b00b1e5",
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
        "i": "177ff13e",
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
}
```

for recipient,
```
"d": {
        "c": <checksum>,
        "s": <status>
}
```

where the key is a plaintext string, the binary data is a string containing the value, the checksum is a 32-bit checksum (crc-32) of the stored value and the status is an integer detailing whether or not the store was successful (zero = ok, nonzero = error) 

#### sequence

#### 1. sender sends initial query
```
{
        "s": 0,
        "m": 0,
        "a": 1,
        "i": "0b00b1e5",
        "q": 103581305802345,
        "d": {
                "k": "boobies",
                "v": a3 e5 1d 0f 9e ... 6e 77 3a 0e 9f
        }
}
```

#### 2. recipient sends response
```
{
        "s": 0,
        "m": 0,
        "a": 1,
        "i": "177ff13e",
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

where the target ID is a serialized ID  

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

where IP addresses are strings, ports are integers and IDs are serialized IDs

if there are no nearby nodes, the bucket may be empty

#### sequence

#### 1. sender sends initial query

```
        "s": 0,
        "m": 0,
        "a": 2,
        "i": "0b00b1e5",
        "q": 103581305802345,
        "d": {
                "t": "19aebc67"
        }
```

#### 2. recipient sends response

```
{
        "s": 0,
        "m": 1,
        "a": 2,
        "i": "177ff13e",
        "q": 103581305802345,
        "d": {
                "b": [
                        {"a": "24.30.210.11", "p": 16616, "i": "00e0fb37" }, 
                        {"a": "1.1.51.103", "p": 10510, "i": "ab0de4c2" }
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

where the target ID is a serialized ID

for recipient,

```
"d": {
        "v": <stored value>,
        "b": <nearest nodes>
}
```

where the stored value is either a string or nil depending on whether or not it was found in the recipient's hash table and the nearest nodes either being an array of nodes or nil depending on the same factors as the stored value  

`find_value` **cannot** return **both** a stored value and a node array, such messages should be rejected

#### sequence

#### 1. sender sends initial query

```
        "s": 0,
        "m": 0,
        "a": 3,
        "i": "0b00b1e5",
        "q": 103581305802345,
        "d": {
                "t": "19aebc67"
        }
```

#### 2a. recipient has target ID in hash table, sends response with value stored at key

```
        "s": 0,
        "m": 1,
        "a": 3,
        "i": "177ff13e",
        "q": 103581305802345,
        "d": {
                "v": 1e e5 6a 2e 90 ... a0 e4 b7 61 d8,
                "b": nil
        }
```

#### 2b. recipient does not have target ID in hash table, sends response with closest nodes to target ID

```
        "s": 0,
        "m": 1,
        "a": 3,
        "i": "177ff13e",
        "q": 103581305802345,
        "d": {
                "v": nil,
                "b": [
                        {"a": "24.30.210.11", "p": 16616, "i": "00e0fb37" }, 
                        {"a": "1.1.51.103", "p": 10510, "i": "ab0de4c2" }       
                ]
        }
```