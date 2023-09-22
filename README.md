# dht

## what?

a generic dht solution, to be used in larger projects involving peer-to-peer networking.  
modeled after kademlia DHT (https://pdos.csail.mit.edu/~petar/papers/maymounkov-kademlia-lncs.pdf)  
this implementation doesn't have solutions for real world problems such as NAT and sybil attacks


## use?

```cpp
#include "dht.h"

int main() {
    using namespace tulip::dht;
    node n(std::atoi(argv[2]));
    
    n.join(peer(argv[3], std::atoi(argv[4])),
        [&](peer) { spdlog::info("join was a success."); },
        [&](peer) { spdlog::info("join was a failure."); });
    
    fv_value v = n.iter_find_value("test value");
    if(v.type() == typeid(bucket)) {
        spdlog::info("\tno value found, bucket instead:");
        for(auto i : boost::get<bucket>(v))
            spdlog::info("\t\t{}", i());
    } else {
        kv vl = boost::get<kv>(v);
        spdlog::info("\tvalue found, value is: {}, timestamp is {}, origin is {}", 
            vl.value, vl.timestamp, vl.origin());
    }

    return 0;
}
```

## requirements

- [boost](http://boost.org)
- [gabime/spdlog](http://github.com/gabime/spdlog)
- [msgpack/msgpack-c](http://github.com/msgpack/msgpack-c)
- [cryptopp-cmake](https://github.com/abdes/cryptopp-cmake)

## list of stuff to do

- document everything
- interface
- tests