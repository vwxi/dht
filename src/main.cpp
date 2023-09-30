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
    case 2: // ping
        {
            node n(std::atoi(argv[2]));
            n.run();
            n.ping(peer(std::string(argv[3]), std::atoi(argv[4])), n.basic_nothing, n.basic_nothing);
        }
        break;
    case 3: // iterative find node
        {
            node n(std::atoi(argv[2]));
            n.run();
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
            n.run();
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
            n.run();
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
                        spdlog::info("\tsig: {}", util::b58encode_s(vl.signature));
                    }
                }, 
                n.basic_nothing);
        }
        break;
    case 6: // disjoint path
        {
            node n(std::atoi(argv[2]));
            n.run();
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
                            spdlog::info("\tVALID? {}", n.crypto.validate(v) ? "yeah" : "nah");
                        }
                    }
                },
                [&](peer) { spdlog::info("join was a failure."); });
        }
        break;
    case 7: // join
        {
            node n(std::atoi(argv[2]));
            n.run();
            n.join(peer(argv[3], std::atoi(argv[4])),
                [&](peer) { spdlog::info("join was a success."); },
                [&](peer) { spdlog::info("join was a failure."); });
        }
        break;
    case 8: // pub_key
        {
            node n(std::atoi(argv[2]));
            n.run();
            n.pub_key(peer(argv[3], std::atoi(argv[4])),
                [&](peer p, std::string key) {
                    using namespace CryptoPP;
                    RSA::PublicKey pk;

                    pk.Load(StringSource(key, true).Ref());

                    spdlog::info("key: {}", util::b58encode_h(util::hash(key)));
                },
                n.basic_nothing);
        }
        break;
    case 9: // crypto test
        {
            tulip::pki::crypto c1, c2;
            std::string data = "hello", s1, s2;

            c1.generate_keypair();
            c2.generate_keypair();

            s1 = c1.sign(data);
            s2 = c2.sign(data);

            spdlog::info("verify(c1, s1) = {}", c1.verify(data, s1));
            spdlog::info("verify(c2, s2) = {}", c2.verify(data, s2));
            spdlog::info("verify(c2, s1) = {}", c2.verify(data, s1));
            spdlog::info("verify(c1, s2) = {}", c1.verify(data, s2));
        }
        break;
    }

    return 0;
}