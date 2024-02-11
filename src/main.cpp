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
            n.run("pub2", "priv2");
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
    case 3: // join & resolve own ip
        {
            node n(true, std::atoi(argv[2]));
            n.run("pub", "priv");
            n.join(net_addr("udp", argv[3], std::atoi(argv[4])), 
                [&](net_contact peer) {
                    n.resolve(enc("3NYJ54uJys9xPA6DntLD3uaSQVKQW92gE8WNci7vLsKg"),
                        [&](net_contact p) {
                            spdlog::critical("resolved addresses...");
                            for(auto a : p.addresses)
                                spdlog::critical("\t{}", a.to_string());
                        },
                        [](net_contact) {
                            spdlog::critical("couldn't resolve addresses");
                        });
                }, 
                [&](net_contact peer) {
                    spdlog::info("join bad");
                });
        }
        break;
    }

    return 0;
}