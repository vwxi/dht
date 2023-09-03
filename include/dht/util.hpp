#ifndef _UTIL_HPP
#define _UTIL_HPP

#include <iostream>
#include <iomanip>
#include <stdexcept>
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
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <cassert>
#include <tuple>
#include <deque>

#undef NDEBUG
#define BOOST_BIND_NO_PLACEHOLDERS

#include <boost/asio.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/shared_lock_guard.hpp>

#include <msgpack.hpp>

#include "spdlog/spdlog.h"

#define LOCK(m) std::lock_guard<std::mutex> l(m);
#define R_LOCK(m) boost::shared_lock<boost::shared_mutex> l(m);
#define W_LOCK(m) boost::upgrade_lock<boost::shared_mutex> l(m);
#define TIME_NOW() duration_cast<seconds>(system_clock::now().time_since_epoch()).count()

namespace tulip {

typedef std::uint64_t u64; 
typedef std::uint32_t u32;
typedef std::uint16_t u16;
typedef std::uint8_t u8;

using boost::asio::ip::udp;
using boost::asio::ip::tcp;
using boost::uuids::detail::sha1;
using boost::asio::deadline_timer;
using namespace std::chrono;
using namespace std::placeholders;
using rand_eng = std::uniform_int_distribution<u32>;

template<std::size_t N>
bool operator<(const std::bitset<N>& x, const std::bitset<N>& y)
{
    for (int i = N-1; i >= 0; i--) {
        if (x[i] ^ y[i]) return y[i];
    }
    return false;
}

namespace dht {

namespace proto {

const int magic_length = 4; // magic length in bytes
const int hash_width = 5; // hash width in unsigned ints
const int bucket_size = 4;   // number of entries in k-buckets (SHOULD NOT BE OVER 20)
const int bit_hash_width = 32; // hash width in bits
const int missed_pings_allowed = 3; // number of missed pings allowed
const int missed_messages_allowed = 3; // number of missed messages allowed
const int net_timeout = 10; // number of seconds until timeout
const int repl_cache_size = 3; // number of peers allowed in bucket replacement cache at one time
const u64 max_data_size = 65535; // max data size in bytes
const int alpha = 3; // alpha from kademlia paper
const int refresh_time = 3600; // number of seconds until a bucket needs refreshing
const int republish_time = 86400; // number of seconds until a key-value pair expires
const int refresh_interval = 600; // when to refresh buckets older than refresh_time, in seconds
const int republish_interval = 86400; // when to republish data older than an republish_time, in seconds
}

typedef std::bitset<proto::bit_hash_width> hash_t;

typedef u32 id_t[proto::hash_width];

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

    for (auto b = bytes.rbegin(); b != bytes.rend(); ++b) {
        ss << std::setw(2) << std::setfill('0') << static_cast<int>(*b);
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

static void msg_id(std::default_random_engine& reng, id_t& h) {
    std::uniform_int_distribution<unsigned int> uid;

    std::generate(std::begin(h), std::end(h), [&]() { return uid(reng); });
}

static u64 msg_id() {
    u64 r = rand();
    r = (r << 30) | rand();
    r = (r << 30) | rand();
    return r;
}

static hash_t gen_id(std::default_random_engine& reng) {
    id_t id;
    msg_id(reng, id);
    return htob(id);
}

static std::string to_bin(std::string hex) {
    const std::unordered_map<char, std::string> t = {
        { '0', "0000" }, { '1', "0001" }, { '2', "0010" }, { '3', "0011" }, 
        { '4', "0100" }, { '5', "0101" }, { '6', "0110" }, { '7', "0111" }, 
        { '8', "1000" }, { '9', "1001" }, { 'A', "1010" }, { 'B', "1011" }, 
        { 'C', "1100" }, { 'D', "1101" }, { 'E', "1110" }, { 'F', "1111" }
    };

    std::string r;
    for(auto i : hex)
        try {
            r += t.at(std::toupper(i));
        } catch (std::exception&) { throw std::invalid_argument("to_bin: bad hex string"); }

    return r;
}

// http://www.hackersdelight.org/hdcodetxt/crc.c.txt
static unsigned int crc32b(unsigned char *message) {
   int i, j;
   unsigned int byte, crc, mask;

   i = 0;
   crc = 0xFFFFFFFF;
   while (message[i] != 0) {
      byte = message[i];            // Get next byte.
      crc = crc ^ byte;
      for (j = 7; j >= 0; j--) {    // Do eight times.
         mask = -(crc & 1);
         crc = (crc >> 1) ^ (0xEDB88320 & mask);
      }
      i = i + 1;
   }
   return ~crc;
}

static hash_t sha1(std::string s) {
    boost::uuids::detail::sha1 sha1;
    id_t h;
    sha1.process_bytes(s.c_str(), s.size());
    sha1.get_digest(h);
    return util::htob(h);
}

template <typename M, typename F, typename... Args>
static auto lock_fn(M& mut, F&& fn, Args&&... args) {
    LOCK(mut);
    return std::forward<F>(fn)(std::forward<Args>(args)...);
}

}

}
}

#endif