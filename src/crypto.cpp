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
    RSASS<PSSR, SHA256>::Signer signer(key_pair.priv_key);

    StringSource s1(message, true, new SignerFilter(rng, signer, new StringSink(signature)));

    return signature;
}

bool crypto::verify(RSA::PublicKey pk, std::string message, std::string signature) {
    try {
        RSASS<PSSR, SHA256>::Verifier verifier(pk);

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

bool crypto::verify(std::string message, std::string signature) {
    return verify(key_pair.pub_key, message, signature);
}

bool crypto::verify(dht::hash_t id, std::string message, std::string signature) {
    auto k = ks_get(id);

    if(!k.has_value())
        return false;

    bool v = verify(k.value(), message, signature);

    // remove from local keystore
    if(!v)
        ks_del(id);

    return v;
}

boost::optional<RSA::PublicKey> crypto::ks_get(dht::hash_t h) {
    LOCK(ks_mutex);
    return (ks.find(h) != ks.end()) ? 
        boost::optional<RSA::PublicKey>(ks[h]) : boost::none;
}

void crypto::ks_del(dht::hash_t h) {
    LOCK(ks_mutex);
    ks.erase(h);
}

void crypto::ks_put(dht::hash_t h, std::string s) {
    if(s.empty() || ks_get(h).has_value())
        return;

    LOCK(ks_mutex);
    RSA::PublicKey pk;

    try {
        pk.Load(StringSource(s, true).Ref());
        ks[h] = std::move(pk);
    } catch (std::exception& e) { }
}

bool crypto::ks_has(dht::hash_t h) {
    LOCK(ks_mutex);
    return ks.find(h) != ks.end();
}

bool crypto::validate(dht::kv vl) {
    return verify(vl.origin.id, vl.sig_blob(), vl.signature);
}

}
}