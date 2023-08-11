#include "bucket.h"
#include "dht.h"

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::debug);
    using namespace tulip::dht;

    // ctor starts networking
    if(std::atoi(argv[1]) == 1) {
        node n(16161);
    } else {
        node n(17171);
        n.find_node(peer("127.0.0.1", 16161), hash_t(0xb00b1e5),
            [](peer, bucket bkt) { 
                for(auto b : bkt) spdlog::info("node {}:{} id {}", b.addr, b.port, util::htos(b.id)); 
                },
            [](peer) { spdlog::info("nahhhhh"); });
    }

    return 0;
}