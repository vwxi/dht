#include "bucket.h"
#include "dht.h"

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%P] [%H:%M:%S] [%^%l%$] %v");
    using namespace tulip::dht;

    // ctor starts networking
    switch(std::atoi(argv[1])) {
    case 1: 
        {
            node n(16161);
        }
        break;
    case 2: 
        {
            node n(std::atoi(argv[2]));
            n.ping(peer(std::string(argv[3]), std::atoi(argv[4])), [](peer) {}, [](peer){});
        }
        break;
    case 3:
        {
            node n(std::atoi(argv[2]));
            n.ping(peer("127.0.0.1", 16161), 
                [&](peer) {
                    bucket b = n.iter_find_node(0xb00b1e5);
                    spdlog::info("iter_find_node ->");
                    for(auto i : b)
                        spdlog::info("\t{}:{} id {}", i.addr, i.port, util::htos(i.id));
                }, 
                [](peer){});
        }
        break;
    }

    return 0;
}