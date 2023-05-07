#include "dht.h"

int main(int argc, char** argv) {
    if(argc != 3) {
        std::cerr << "usage: " << argv[0] << " [messaging port] [data xfer port]" << std::endl;
        return 0;
    }

    try {
        dht::node n(std::atoi(argv[1]), std::atoi(argv[2]));    
    } catch (std::exception& e) {
        std::cerr << "EXCEPTION CAUGHT: " << e.what() << std::endl;
    }
    return 0;
}