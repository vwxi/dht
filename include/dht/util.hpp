#ifndef _UTIL_HPP
#define _UTIL_HPP

#include <iostream>
#include <iomanip>
#include <utility>
#include <list>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <chrono>
#include <bitset>
#include <functional>
#include <random>
#include <future>
#include <thread>
#include <mutex>

#include <boost/asio.hpp>
#include <boost/uuid/detail/sha1.hpp>

#include "spdlog/spdlog.h"

#include "proto.hpp"

namespace dht {

typedef unsigned long long int u64;
typedef unsigned long int u32;
typedef unsigned short u16;
typedef unsigned char u8;

using boost::asio::ip::udp;
using boost::uuids::detail::sha1;
using boost::asio::deadline_timer;
using namespace std::chrono;
using namespace std::placeholders;
using rand_eng = std::uniform_int_distribution<unsigned int>;

typedef std::bitset<proto::I> hash_t;

namespace util { // utilities

inline void hash_combine(std::size_t& seed) { }

template <typename T, typename... Rest>
inline void hash_combine(std::size_t& seed, const T& v, Rest... rest) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
    hash_combine(seed, rest...);
}

template<typename ... Args>
static std::string string_format( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1;
    if( size_s <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 );
}

static std::string htos(hash_t h) {
    std::vector<unsigned char> bytes((h.size() + 7) / 8);
    for (size_t i = 0; i < h.size(); ++i) {
        if (h.test(i)) {
            bytes[i / 8] |= 1 << (i % 8);
        }
    }

    std::stringstream ss;
    ss << std::hex;
    std::cout << std::hex;

    for (auto b : bytes) {
        ss << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }

    return ss.str();
}

static hash_t htob(sha1::digest_type h) {
    hash_t b(0), t(0);
    const u64 sh = sizeof(unsigned int) << 3;
    
    b |= hash_t(h[4]); b <<= sh;
    b |= hash_t(h[3]); b <<= sh;
    b |= hash_t(h[2]); b <<= sh;
    b |= hash_t(h[1]); b <<= sh;
    b |= hash_t(h[0]);

    return b;
}

static void btoh(hash_t b, sha1::digest_type& h) {
    const u64 sh = sizeof(unsigned int) << 3;
    hash_t s((1 << sizeof(unsigned int)) - 1);

    h[4] = (b & s).to_ullong(); b >>= sh;
    h[3] = (b & s).to_ullong(); b >>= sh;
    h[2] = (b & s).to_ullong(); b >>= sh;
    h[1] = (b & s).to_ullong(); b >>= sh;
    h[0] = (b & s).to_ullong(); b >>= sh;
}

static hash_t gen_id(std::string s, u16 p) {
    std::string full = string_format("%s:%u", s.c_str(), p);
    boost::uuids::detail::sha1 sha;
    sha1::digest_type id;
    sha.process_bytes(full.data(), full.size());
    sha.get_digest(id);
    return htob(id);
}

static void nonce(sha1::digest_type& h) {
    std::random_device rd;
    std::default_random_engine re(rd());
    std::uniform_int_distribution<unsigned int> uid;

    std::generate(std::begin(h), std::end(h), [&]() { return uid(re); });
}

}

}

#endif