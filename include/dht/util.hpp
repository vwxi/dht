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
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/shared_lock_guard.hpp>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/random.hpp>

#include "msgpack.hpp"
#include "spdlog/spdlog.h"
#include "cryptopp/rsa.h"
#include "cryptopp/sha.h"
#include "cryptopp/pssr.h"
#include "cryptopp/osrng.h"
#include "cryptopp/hex.h"
#include "cryptopp/files.h"
#include "miniupnpc/miniupnpc.h"
#include "miniupnpc/upnpcommands.h"
#include "miniupnpc/upnperrors.h"

#define LOCK(m) std::lock_guard<std::mutex> l(m);
#define R_LOCK(m) boost::shared_lock<boost::shared_mutex> read_lock(m);
#define W_LOCK(m) boost::upgrade_lock<boost::shared_mutex> write_lock(m);
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
const int quorum = 3; // quorum for alternative lookup procedure (lp_lookup)
const int token_length = 32; // length of secret tokens

}

namespace constants {

const int upnp_release_interval = 14400; // number of seconds between each upnp port re-leasing

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

typedef std::independent_bits_engine<
    std::default_random_engine, CHAR_BIT, unsigned char> token_reng_t;

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

static hash_t gen_randomness(hash_reng_t& reng) {
    return reng();
}

static std::string gen_token(token_reng_t& reng) {
    std::string data;
    data.resize(proto::token_length);
    std::generate(data.begin(), data.end(), std::ref(reng));
    return data;
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
    ss << "0x"; // hacky but required for hex->int

    CryptoPP::HexEncoder he(new CryptoPP::FileSink(ss));

    std::string digest;
    CryptoPP::SHA256 h;

    h.Update((const CryptoPP::byte*)s.data(), s.size());
    digest.resize(h.DigestSize() + 2); // hacky
    h.Final((CryptoPP::byte*)&digest[2]);

    (void)CryptoPP::StringSource(digest, true, new CryptoPP::Redirector(he));

    return hash_t(ss.str());
}

// most of the base58 code is ripped from https://bitcoin.stackexchange.com/a/96359
// i removed the unnecessary abstractions so we can just get string in string out

static const char b58map[] = {
  '1', '2', '3', '4', '5', '6', '7', '8',
  '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
  'H', 'J', 'K', 'L', 'M', 'N', 'P', 'Q',
  'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y',
  'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
  'h', 'i', 'j', 'k', 'm', 'n', 'o', 'p',
  'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
  'y', 'z' };

static const u8 alphamap[] = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0xff, 0x11, 0x12, 0x13, 0x14, 0x15, 0xff,
  0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0xff, 0x2c, 0x2d, 0x2e,
  0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0xff, 0xff, 0xff, 0xff, 0xff };

// encode string into base58. used for signatures
static std::string b58encode_s(const std::string& data) {
    std::vector<u8> digits((data.size() * 138 / 100) + 1);
    size_t digitslen = 1;
    std::string result;

    for(size_t i = 0; i < data.size(); i++) {
        u32 carry = static_cast<u32>(data[i]);
        for(size_t j = 0; j < digitslen; j++) {
            carry = carry + static_cast<uint32_t>(digits[j] << 8);
            digits[j] = static_cast<u8>(carry % 58);
            carry /= 58;
        }
        
        for(; carry; carry /= 58)
            digits[digitslen++] = static_cast<u8>(carry % 58);
    }
  
    for(size_t i = 0; i < (data.size() - 1) && !data[i]; i++)
        result.push_back(b58map[0]);

    for(size_t i = 0; i < digitslen; i++)
        result.push_back(b58map[digits[digitslen - 1 - i]]);

    return result;
}

// decode base58 into string. used for signatures
static std::string b58decode_s(const std::string& data) {
    std::vector<u8> result((data.size() * 138 / 100) + 1);
    
    size_t resultlen = 1;
    for(size_t i = 0; i < data.size(); i++) {
        uint32_t carry = static_cast<uint32_t>(alphamap[data[i] & 0x7f]);
    
        for(size_t j = 0; j < resultlen; j++, carry >>= 8) {
            carry += static_cast<uint32_t>(result[j] * 58);
            result[j] = static_cast<uint8_t>(carry);
        }
    
        for (; carry; carry >>= 8)
            result[resultlen++] = static_cast<uint8_t>(carry);
    }

    result.resize(resultlen);
    for(size_t i = 0; i < (data.size() - 1) && data[i] == b58map[0]; i++)
        result.push_back(0);

    std::reverse(result.begin(), result.end());

    std::string res(result.begin(), result.end());
    return res;
}

// encode hash into base58 (https://learnmeabitcoin.com/technical/base58)
static std::string b58encode_h(hash_t h) {
    std::deque<char> result;
    while(h > 0) {
        int remainder = (h % 58).convert_to<int>();
        result.push_front(b58map[remainder]);
        h /= 58;
    }

    std::string res(result.begin(), result.end());
    return res;
}

// decode base58 into hash (https://learnmeabitcoin.com/technical/base58)
static hash_t b58decode_h(std::string s) {
    hash_t result = 0;
    
    for(std::size_t i = 0; i != s.size(); i++) {
        const char* p = std::strchr(b58map, s[i]);
        if(p == NULL)
            throw std::runtime_error("invalid base58 character");

        result = result * 58 + (p - b58map);
    }

    return result;
}

}

}
}

#endif