#include "bucket.h"
#include "dht.h"

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::debug);
    using namespace tulip::dht;

    // node n(std::atoi(argv[1]), std::atoi(argv[2]));

    // if(argc > 3) {
    //     dht::peer t(std::string(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]));

    //     int i = std::atoi(argv[6]);

    //     if(i == 1) {
    //         n.ping(t,
    //             [&](dht::peer) {
    //                 dht::bucket b = n.lookup(dht::hash_t(0xb00b1e5));
    //                 spdlog::critical("contents of lookup bucket: ");
    //                 for(auto c : b)
    //                     spdlog::critical("\tcandidate: {}:{}:{} id {}", c.addr, c.port, c.reply_port, dht::util::htos(c.id));
    //             },
    //             [&](dht::peer p) { spdlog::info("not ok"); });
    //     } else {
    //         std::thread([&n, t]() {
    //         //n.find_node(t,
    //         //    n.own_id(),
    //         //    [&](tulip::dht::peer p, tulip::dht::bucket bkt) {
    //         //        spdlog::info("nearby peers to peer {}:", tulip::dht::util::htos(n.own_id()));
    //         //        for(tulip::dht::peer peer : bkt) {
    //         //            spdlog::info("\t{}:{}:{} (id: {})", peer.addr, peer.port, peer.reply_port, 
    //         //                tulip::dht::util::htos(peer.id));
    //         //        }
    //         //    },
    //         //    [&](tulip::dht::peer p) {
    //         //        spdlog::warn("bad");
    //         //    });

    //         n.ping(t,
    //             [&](dht::peer p) { spdlog::info("ok"); },
    //             [&](dht::peer p) { spdlog::info("not ok"); });
    //         }).detach();
    //     }
    // }

    // ctor starts networking
    if(std::atoi(argv[1]) == 1) {
        node n(16161);
    } else {
        node n(17171);
        n.ping(peer("127.0.0.1", 16161), 
            [](peer) { spdlog::info("yeahhhh"); },
            [](peer) { spdlog::info("nahhhhh"); });
    }

    return 0;
}