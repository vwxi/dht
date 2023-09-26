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
            n.ping(peer(std::string(argv[3]), std::atoi(argv[4])), n.basic_nothing, n.basic_nothing);
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
                n.basic_nothing);
        }
        break;
    case 4: // iterative store
        {
            node n(std::atoi(argv[2]));
            n.ping(peer("127.0.0.1", 16161), 
                [&](peer) {
                    n.iter_store("hihi", "hey there buster");
                }, 
                n.basic_nothing);
        }
        break;
    case 5: // iterative find value
        {
            node n(std::atoi(argv[2]));
            n.ping(peer(argv[3], std::atoi(argv[4])), 
                [&](peer) {
                    node::fv_value v = n.iter_find_value("hihi");
                    spdlog::info("iter_find_value ->");
                    if(v.type() == typeid(bucket)) {
                        spdlog::info("\tno value found, bucket instead:");
                        for(auto i : boost::get<bucket>(v))
                            spdlog::info("\t\t{}", i());
                    } else {
                        kv vl = boost::get<kv>(v);
                        spdlog::info("\tvalue found, value is: {}, timestamp is {}, origin is {}", 
                            vl.value, vl.timestamp, vl.origin());
                    }
                }, 
                n.basic_nothing);
        }
        break;
    case 6: // disjoint path
        {
            node n(std::atoi(argv[2]));
            n.join(peer(argv[3], std::atoi(argv[4])),
                [&](peer) { 
                    std::list<node::fv_value> paths = n.disjoint_lookup(true, util::hash("hihi"));
                    for(const auto& p : paths) {
                        spdlog::info("disjoint path ->");
                        if(p.type() == typeid(bucket)) {
                            for(auto b : boost::get<bucket>(p))
                                spdlog::info("\tbucket peer {}", b());
                        } else if(p.type() == typeid(kv)) {
                            kv v = boost::get<kv>(p);
                            spdlog::info("\tkv {} val {} ts {} origin {}", 
                                util::b58encode_h(v.key), v.value, v.timestamp, v.origin());
                        }
                    }
                },
                [&](peer) { spdlog::info("join was a failure."); });
        }
        break;
    case 7: // join
        {
            node n(std::atoi(argv[2]));
            n.join(peer(argv[3], std::atoi(argv[4])),
                [&](peer) { spdlog::info("join was a success."); },
                [&](peer) { spdlog::info("join was a failure."); });
        }
        break;
    case 8: // testing base58
        {
            spdlog::info("base58('hello') = {}", util::b58encode_s("hello"));
            spdlog::info("base58d('Cn8eVZg') = {}", util::b58decode_s("Cn8eVZg"));
            spdlog::info("base58h(0xb00b1e5) = {}", util::b58encode_h(0xb00b1e5));
            spdlog::info("base58hd('HK6dz') = {}", util::b58encode_h(util::b58decode_h("HK6dz")));
        }
        break;
    case 9: // testing crypto wrapper
        {
            tulip::pki::crypto c1;
            c1.generate_keypair();

            std::string data = "hello", sig1, sig2;
            
            sig1 = c1.sign(data);

            tulip::pki::crypto c2;
            c2.generate_keypair();

            sig2 = c2.sign(data);

            spdlog::info("verify(c1, sig1) = {}", c1.verify(data, sig1));
            spdlog::info("verify(c2, sig1) = {}", c2.verify(data, sig2));
            spdlog::info("verify(c1, sig2) = {}", c1.verify(data, sig2));
            spdlog::info("verify(c2, sig1) = {}", c2.verify(data, sig1));
        }
        break;
    }

    return 0;
}