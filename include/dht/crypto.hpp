#ifndef _CRYPTO_H
#define _CRYPTO_H

#include "util.hpp"

namespace lotus {
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
    std::string pub_key(RSA::PublicKey);

    // generate keypair
    void generate_keypair();

    // import
    void import_keypair(keypair);
    void import_file(const std::string&, const std::string&);

    // export
    void export_keypair(keypair&);
    void export_file(const std::string&, const std::string&);

    // sign
    std::string sign(const std::string&);

    // verify
    bool verify(RSA::PublicKey, const std::string&, const std::string&);
    bool verify(const std::string&, const std::string&);
    bool verify(dht::hash_t, const std::string&, const std::string&);

    // keystore
    void ks_put(dht::hash_t, const std::string&);
    boost::optional<RSA::PublicKey> ks_get(dht::hash_t);
    void ks_del(dht::hash_t);
    bool ks_has(dht::hash_t);

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