# dht

## what?

a generic dht solution, to be used in larger projects involving peer-to-peer networking.  
modeled after kademlia DHT (https://pdos.csail.mit.edu/~petar/papers/maymounkov-kademlia-lncs.pdf)

## build instructions

```
mkdir build
cd build
cmake ..
cmake --build .
./dht [udp port] [tcp port]
```

## requirements

- [boost](http://boost.org)
- [gabime/spdlog](http://github.com/gabime/spdlog)
- [msgpack/msgpack-c](http://github.com/msgpack/msgpack-c)

## list of stuff to do

- store
- find_value
- refresh
- document everything
- tests