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
./dht
```

## requirements

- [boost](http://boost.org)
- [gabime/spdlog](http://github.com/gabime/spdlog)

## list of stuff to do

- refactor (done)
- ping (done)
- replacement cache (done)
- find_node
- testing interface
- find_value
- store