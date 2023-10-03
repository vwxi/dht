# dht

## what?

a generic dht solution, to be used in larger projects involving peer-to-peer networking.  
modeled after kademlia DHT (https://pdos.csail.mit.edu/~petar/papers/maymounkov-kademlia-lncs.pdf)  
this implementation doesn't have solutions for real world problems such as NAT and sybil attacks


## use?

```cpp
#include "dht.h"

int main(int argc, char** argv) {
    using namespace tulip::dht;

    node n(std::atoi(argv[2]));
    n.run();
    n.join(peer(argv[3], std::atoi(argv[4])),
        [&](peer) { 
            std::list<node::fv_value> paths = n.disjoint_lookup(true, util::hash("hihi"));
            for(const auto& p : paths) {
                spdlog::info("disjoint path ->");
                if(p.type() == typeid(bucket)) {
                    for(auto b : boost::get<bucket>(p))
                        spdlog::info("\tbucket peer {}", b());
                } else if(p.type() == typeid(kv)) {
                    kv v = boost::get<kv>(p);
                    spdlog::info("\tkv {} val {} ts {} origin {}", 
                        util::b58encode_h(v.key), v.value, v.timestamp, v.origin());
                }
            }
        },
        [&](peer) { spdlog::info("join was a failure."); });

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