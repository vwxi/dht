#include "dht.h"

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%P] [%H:%M:%S] [%^%l%$] %v");
    using namespace lotus::dht;

    // ctor starts networking
    switch(std::atoi(argv[1])) {
    case 1: // bare node
        {
            node n(true, 16161);
            n.run();
        }
        break;
    case 2: // join
        {
            node n(true, std::atoi(argv[2]));
            n.run();
            n.join(net_addr("udp", argv[3], std::atoi(argv[4])), 
                [&](net_contact peer) {
                    spdlog::info("join ok");
                    spdlog::info("join addresses:");
                    for(auto a : peer.addresses)
                        spdlog::info("\t{}", a.to_string());
                }, 
                [&](net_contact peer) {
                    spdlog::info("join bad");
                });
        }
        break;
    }

    return 0;
}