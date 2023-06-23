#ifndef _UTIL_HPP
#define _UTIL_HPP

#include <iostream>
#include <iomanip>
#include <utility>
#include <list>
#include <set>
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

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include <boost/serialization/list.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/bitset.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/version.hpp>

#include "spdlog/spdlog.h"

namespace tulip {

typedef unsigned long long int u64;
typedef unsigned long int u32;
typedef unsigned short u16;
typedef unsigned char u8;

using boost::asio::ip::udp;
using boost::asio::ip::tcp;
using boost::uuids::detail::sha1;
using boost::asio::deadline_timer;
using namespace std::chrono;
using namespace std::placeholders;
using rand_eng = std::uniform_int_distribution<u32>;

namespace dht {

namespace proto {

const int ML = 4; // magic length in bytes
const int NL = 5; // hash width in unsigned ints
const int K = 4;   // number of entries in k-buckets (SHOULD NOT BE OVER 20)
const int I = 160; // hash width in bits
const int M = 3; // number of missed pings allowed
const int G = 3; // number of missed messages allowed
const int T = 10; // number of seconds until timeout
const int C = 3; // number of peers allowed in bucket replacement cache at one time
const u64 MS = 65535; // max data size in bytes
const int A = 3; // `a` from kademlia paper

}

typedef std::bitset<proto::I> hash_t;

template<std::size_t N>
bool operator<(const std::bitset<N>& x, const std::bitset<N>& y)
{
    for (int i = N-1; i >= 0; i--) {
        if (x[i] ^ y[i]) return y[i];
    }
    return false;
}

typedef u32 id_t[proto::NL];

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

static hash_t htob(id_t h) {
    hash_t b(0), t(0);
    const u64 sh = sizeof(unsigned int) << 3;
    
    b |= hash_t(h[4]); b <<= sh;
    b |= hash_t(h[3]); b <<= sh;
    b |= hash_t(h[2]); b <<= sh;
    b |= hash_t(h[1]); b <<= sh;
    b |= hash_t(h[0]);

    return b;
}

static void btoh(hash_t b, id_t& h) {
    const u64 sh = 32;
    hash_t s(0xffffffff);

    h[0] = (b & s).to_ulong(); b >>= sh;
    h[1] = (b & s).to_ulong(); b >>= sh;
    h[2] = (b & s).to_ulong(); b >>= sh;
    h[3] = (b & s).to_ulong(); b >>= sh;
    h[4] = (b & s).to_ulong(); b >>= sh;
}

static void msg_id(id_t& h) {
    std::random_device rd;
    std::default_random_engine re(rd());
    std::uniform_int_distribution<unsigned int> uid;

    std::generate(std::begin(h), std::end(h), [&]() { return uid(re); });
}

static hash_t gen_id() {
    id_t id;
    msg_id(id);
    return htob(id);
}

}

}
}

#endif