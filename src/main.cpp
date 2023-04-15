#include "dht.hpp"

int main() {
    try {
        boost::asio::io_context ioc;
        dht::peer p(ioc, 16666);
        ioc.run();
    } catch (std::exception& e) {
        std::cerr << "EXCEPTION CAUGHT: " << e.what() << std::endl;
    }
    return 0;
}