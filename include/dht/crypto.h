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
    ~crypto() = default;

    // import
    bool import_keypair(keypair);

    // export
    void export_keypair(keypair&);

    // sign

    // verify

private:
    keypair key_pair;
    AutoSeededRandomPool rng;
};

}
}

#endif