#ifndef _CRYPTO_H
#define _CRYPTO_H

#include "osrng.h"
#include "util.hpp"

namespace tulip {
namespace pki {

using namespace CryptoPP;

struct keypair {
    RSA::PublicKey pub_key;
    RSA::PrivateKey priv_key;
};

/// @brief crypto wrapper object
class crypto {
public:
    crypto();

    // generate keypair
    void generate_keypair();

    // import
    void import_keypair(keypair);
    void import_file(std::string, std::string);

    // export
    void export_keypair(keypair&);
    void export_file(std::string, std::string);

    // sign
    std::string sign(std::string);

    // verify
    bool verify(std::string, std::string);

private:
    keypair key_pair;
    AutoSeededRandomPool rng;
};

}
}

#endif