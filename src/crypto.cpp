#include "crypto.h"
#include "dht.h"

namespace tulip {
namespace pki {

crypto::crypto() { }

std::string crypto::pub_key() {
    std::string pk;

    key_pair.pub_key.Save(StringSink(pk).Ref());

    return pk;
}

void crypto::generate_keypair() {
    InvertibleRSAFunction params;
    params.GenerateRandomWithKeySize(rng, dht::proto::key_size);

    key_pair.priv_key = RSA::PrivateKey(params);
    key_pair.pub_key = RSA::PublicKey(params);
}

void crypto::import_keypair(keypair kp) {
    key_pair.priv_key = kp.priv_key;
    key_pair.pub_key = kp.pub_key;
}

void crypto::import_file(std::string pub_filename, std::string priv_filename) {
    key_pair.pub_key.Load(FileSource(pub_filename.c_str(), true, NULL, true).Ref());
    key_pair.priv_key.Load(FileSource(priv_filename.c_str(), true, NULL, true).Ref());
}

void crypto::export_keypair(keypair& kp) {
    kp.priv_key = key_pair.priv_key;
    kp.pub_key = key_pair.pub_key;
}

void crypto::export_file(std::string pub_filename, std::string priv_filename) {
    key_pair.pub_key.Save(FileSink(pub_filename.c_str(), true).Ref());
    key_pair.priv_key.Save(FileSink(priv_filename.c_str(), true).Ref());
}

std::string crypto::sign(std::string message) {
    std::string signature;
    RSASS<PSS, SHA256>::Signer signer(key_pair.priv_key);

    StringSource s1(message, true, new SignerFilter(rng, signer, new StringSink(signature)));

    return signature;
}

bool crypto::verify(std::string message, std::string signature) {
    try {
        RSASS<PSS, SHA256>::Verifier verifier(key_pair.pub_key);

        StringSource s1(message+signature, true, 
            new SignatureVerificationFilter(
                verifier, NULL, SignatureVerificationFilter::THROW_EXCEPTION
            )
        );

        return true;
    } catch (std::exception& e) {
        return false;
    }
}

void crypto::ks_put(dht::hash_t h, std::string s) {
    LOCK(ks_mutex);
    RSA::PublicKey pk;

    try {
        pk.Load(StringSource(s, true).Ref());
        ks[h] = std::move(pk);
    } catch (std::exception& e) { }
}

boost::optional<RSA::PublicKey> crypto::ks_get(dht::hash_t h) {
    LOCK(ks_mutex);
    auto it = ks.find(h);
    return (it != ks.end()) ? boost::optional<RSA::PublicKey>(it->second) : boost::none;
}

bool crypto::validate(dht::kv vl) {
    // try local keystore to get key first
    auto local = ks_get(vl.key);

    if(local.has_value())
        return verify(vl.sig_blob(), vl.signature);

    // we'll call pub_key when we get back a value from lookups?
    return false;
}

}
}