#ifndef _CRYPTO_H
#define _CRYPTO_H

#include "util.hpp"

namespace tulip {
namespace dht {
    struct kv;
}

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

    // public access to pub key
    std::string pub_key();

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
    bool verify(RSA::PublicKey, std::string, std::string);
    bool verify(std::string, std::string);

    // keystore
    void ks_put(dht::hash_t, std::string);
    boost::optional<RSA::PublicKey> ks_get(dht::hash_t);

    // validate
    bool validate(dht::kv);
    
private:
    keypair key_pair;
    AutoSeededRandomPool rng;

    std::mutex ks_mutex;
    std::unordered_map<dht::hash_t, RSA::PublicKey> ks;
};

}
}

#endif