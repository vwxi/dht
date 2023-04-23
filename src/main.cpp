#include "dht.h"

int main() {
    try {
        dht::node n(16666);
    } catch (std::exception& e) {
        std::cerr << "EXCEPTION CAUGHT: " << e.what() << std::endl;
    }
    return 0;
}