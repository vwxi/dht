# dht

## what?

a generic dht solution, to be used in larger projects involving peer-to-peer networking.  
modeled after kademlia DHT (https://pdos.csail.mit.edu/~petar/papers/maymounkov-kademlia-lncs.pdf)  
this implementation, in its current state, does not mitigate real-world problems such as NAT or sybil attacks, it is merely reference for future projects

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

## notes for user

- all functions labeled `@private` shouldn't be used in production

## list of stuff to do

- store
- find_value
- refresh
- testing interface