#include "dht.h"

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::info);
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
                            spdlog::info("value -> data: \"{}\", origin: {}, timestamp: {}", 
                                i.value, i.origin(), i.timestamp);
                    });
                }, n.basic_nothing);
        }
        break;
    }

    return 0;
}