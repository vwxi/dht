#include "bucket.h"
#include "dht.h"

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::debug);
    using namespace tulip::dht;

    // ctor starts networking
    if(std::atoi(argv[1]) == 1) {
        node n(16161);
    } else if(std::atoi(argv[1]) == 2) {
        node n(17171);
        n.store(peer("127.0.0.1", 16161), "hello", "hey", 
            [&](peer p) {
                n.find_value(peer("127.0.0.1", 16161), util::sha1("hello"),
                    [&](peer, boost::variant<std::string, bucket> res) {
                        if(res.type() == typeid(std::string)) {
                            spdlog::info("found value -> {}", boost::get<std::string>(res));
                        } else if(res.type() == typeid(bucket)) {
                            bucket bkt = boost::get<bucket>(res);

                            spdlog::info("no value found, bucket ->");
                            for(auto i : bkt)
                                spdlog::info("\tnode {}:{} id {}", i.addr, i.port, util::htos(i.id));
                        }
                    },
                    [](peer) {
                        spdlog::error("find_value bad");
                    });
                },
            [&](peer) {
                spdlog::error("store bad");
            });
    } else if(std::atoi(argv[1]) == 3) {
        node n(18181);
        n.find_value(peer("127.0.0.1", 16161), util::sha1("qq"),
            [&](peer, boost::variant<std::string, bucket> res) {
                if(res.type() == typeid(std::string)) {
                    spdlog::info("found value -> {}", boost::get<std::string>(res));
                } else if(res.type() == typeid(bucket)) {
                    bucket bkt = boost::get<bucket>(res);

                    spdlog::info("no value found, bucket ->");
                    for(auto i : bkt)
                        spdlog::info("\tnode {}:{} id {}", i.addr, i.port, util::htos(i.id));
                }
            },
            [](peer) {
                spdlog::error("find_value bad");
            });
    }

    return 0;
}