#include "bucket.h"
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
        }
        break;
    case 2: // ping
        {
            node n(std::atoi(argv[2]));
            n.ping(peer(std::string(argv[3]), std::atoi(argv[4])), [](peer) {}, [](peer){});
        }
        break;
    case 3: // iterative find node
        {
            node n(std::atoi(argv[2]));
            n.ping(peer(argv[3], std::atoi(argv[4])), 
                [&](peer) {
                    bucket b = n.iter_find_node(0xb00b1e5);
                    spdlog::info("iter_find_node ->");
                    for(auto i : b)
                        spdlog::info("\t{}", i());
                }, 
                [](peer){});
        }
        break;
    case 4: // iterative store
        {
            node n(std::atoi(argv[2]));
            n.ping(peer("127.0.0.1", 16161), 
                [&](peer) {
                    n.iter_store("heyyyyyyyyyyyyyyyyyyyyyyy", "hey there buster");
                }, 
                [](peer){});
        }
        break;
    case 5: // iterative find value
        {
            node n(std::atoi(argv[2]));
            n.ping(peer(argv[3], std::atoi(argv[4])), 
                [&](peer) {
                    fv_value v = n.iter_find_value("heyyyyyyyyyyyyyyyyyyyyyyy");
                    spdlog::info("iter_find_value ->");
                    if(v.type() == typeid(bucket)) {
                        spdlog::info("\tno value found, bucket instead:");
                        for(auto i : boost::get<bucket>(v))
                            spdlog::info("\t\t{}", i());
                    } else {
                        kv vl = boost::get<kv>(v);
                        spdlog::info("\tvalue found, value is: {}, timestamp is {}, origin is {}", vl.value, vl.timestamp, vl.origin());
                    }
                }, 
                [](peer){});
        }
        break;
    case 6: // join
        {
            node n(std::atoi(argv[2]));
            n.join(peer(argv[3], std::atoi(argv[4])),
                [&](peer) { spdlog::info("join was a success."); },
                [&](peer) { spdlog::info("join was a failure."); });
        }
        break;
    }

    return 0;
}