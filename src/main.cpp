#include "dht.h"

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::debug);

    tulip::node n(std::atoi(argv[1]), std::atoi(argv[2]));

    if(argc > 3) {
        tulip::dht::peer t(std::string(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]));

        std::thread([&n, t]() {
            n.find_node(t,
                n.own_id(),
                [&](tulip::dht::peer p, tulip::dht::bucket bkt) {
                    spdlog::info("good");
                },
                [&](tulip::dht::peer p) {
                    spdlog::warn("bad");
                });
        }).detach();
    }

    return 0;
}