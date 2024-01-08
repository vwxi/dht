#include "dht.h"

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%P] [%H:%M:%S] [%^%l%$] %v");
    using namespace tulip::dht;

    // ctor starts networking
    switch(std::atoi(argv[1])) {
    case 1: // bare node
        {
            node n(16161);
            n.run();
        }
        break;
    case 2: // join and do nothing
        {
            node n(std::atoi(argv[2]));
            n.run();
            n.join(peer("udp", argv[3], std::atoi(argv[4])), n.basic_nothing, n.basic_nothing);
        }
        break;
    case 3: // join and store a value
        {
            node n(std::atoi(argv[2]));
            n.run();
            n.join(peer("udp", argv[3], std::atoi(argv[4])), 
                [&](peer p_) {
                    n.put("hello", "hihi");
                }, n.basic_nothing);
        }
        break;
    case 4: // join and fetch value(s)
        {
            node n(std::atoi(argv[2]));
            n.run();
            n.join(peer("udp", argv[3], std::atoi(argv[4])), 
                [&](peer p_) {
                    n.get("hello", [&](std::vector<kv> values) {
                        for(auto i : values)
                            spdlog::info("{} -> data: \"{}\", origin: {}, timestamp: {}", 
                                i.type == proto::store_type::provider_record ? "provider" : "data",
                                i.value, i.origin(), i.timestamp);
                    });
                }, n.basic_nothing);
        }
        break;
    case 5: // join and start providing
        {
            node n(std::atoi(argv[2]));
            n.run();
            n.join(peer("udp", argv[3], std::atoi(argv[4])), 
                [&](peer p_) {
                    n.provide("hello", peer("tcp", "127.0.0.1", std::atoi(argv[2]), n.get_id()));
                }, n.basic_nothing);
        }
        break;
    case 6: // join and fetch provider(s)
        {
            node n(std::atoi(argv[2]));
            n.run();
            n.join(peer("udp", argv[3], std::atoi(argv[4])), 
                [&](peer p_) {
                    n.get_providers("hello", [&](std::vector<peer> providers) {
                        for(auto i : providers)
                            spdlog::info("\tprovider -> {}", i());
                    });
                }, n.basic_nothing);
        }
        break;
    case 7: // get peer record
        {
            node n(std::atoi(argv[2]));
            peer p("udp", argv[3], std::atoi(argv[4]));
            n.run();
            n.join(p, 
                [&](peer p_) {
                    n.get_addresses(p, n.get_id(), 
                        [&](peer, const std::vector<peer_record>& records) {
                            spdlog::info("\taddresses associated with id {}:", util::htos(n.get_id()));
                            for(auto r : records)
                                spdlog::info("\t\t{}", r.address.to_string());
                        }, n.basic_nothing);
                }, n.basic_nothing);
        }
        break;
    }

    return 0;
}