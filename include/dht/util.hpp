#ifndef _UTIL_HPP
#define _UTIL_HPP

#include "files.h"
#include "filters.h"
#include "hex.h"
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
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/shared_lock_guard.hpp>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/random.hpp>

#include <msgpack.hpp>

#include "spdlog/spdlog.h"

#include <cryptopp/rsa.h>
#include <cryptopp/sha.h>

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
using boost::asio::deadline_timer;
using namespace std::chrono;
using namespace std::placeholders;
using rand_eng = std::uniform_int_distribution<u32>;

namespace dht {

namespace proto {

const int magic_length = 4; // magic length in bytes
const int bucket_size = 20; // number of entries in k-buckets (k=20)
const int bit_hash_width = 256; // hash width in bits
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
const int disjoint_paths = 3; // number of disjoint paths to take for lookups
const int key_size = 2048; // size of public/private keys in bytes

}

typedef boost::multiprecision::number<
    boost::multiprecision::cpp_int_backend<
        proto::bit_hash_width, 
        proto::bit_hash_width, 
        boost::multiprecision::unsigned_magnitude, 
        boost::multiprecision::unchecked, 
        void>, 
    boost::multiprecision::et_off> hash_t;

typedef boost::random::independent_bits_engine<
    boost::mt19937, 
    proto::bit_hash_width, 
    hash_t> hash_reng_t;

namespace util { // utilities

static u64 time_now() {
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
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
    std::stringstream ss;
    ss << std::hex;
    ss << "0x";
    ss << h;
    return ss.str();
}

static u64 msg_id() {
    u64 r = rand();
    r = (r << 30) | rand();
    r = (r << 30) | rand();
    return r;
}

static hash_t gen_id(hash_reng_t& reng) {
    return reng();
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

static hash_t hash(std::string s) {
    std::stringstream ss;
    CryptoPP::HexEncoder he(new CryptoPP::FileSink(ss));

    std::string digest;
    CryptoPP::SHA256 h;

    h.Update((const CryptoPP::byte*)s.data(), s.size());
    digest.resize(h.DigestSize() + 2); // hacky
    h.Final((CryptoPP::byte*)&digest[2]);

    (void)CryptoPP::StringSource(digest, true, new CryptoPP::Redirector(he));

    spdlog::info("hh: {}", ss.str());
    return hash_t(0);
}

}

}
}

#endif