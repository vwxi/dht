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
        n.store(peer("127.0.0.1", 16161), "key", "hello",
            [](peer) { spdlog::info("yeahhhh"); },
            [](peer) { spdlog::info("nahhhhh"); });
    }

    return 0;
}