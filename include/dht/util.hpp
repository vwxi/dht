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
#define EINST(b, ...) template class b<__VA_ARGS__>;

namespace lotus {

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
const int bit_hash_width = 32; // hash width in bits
const int missed_pings_allowed = 3; // number of missed pings allowed
const int missed_messages_allowed = 3; // number of missed messages allowed
const int net_timeout = 10; // number of seconds until timeout
const int repl_cache_size = 3; // number of peers allowed in bucket replacement cache at one time
const u64 max_data_size = 65535; // max data size in bytes
const int alpha = 3; // alpha from kademlia paper
const int refresh_time = 3600; // number of seconds until a bucket needs refreshing
const int republish_time = 100; // number of seconds until a key-value pair expires
const int refresh_interval = 600; // when to refresh buckets older than refresh_time, in seconds
const int republish_interval = 10; // when to republish data older than an republish_time, in seconds
const int disjoint_paths = 3; // number of disjoint paths to take for lookups
const int key_size = 2048; // size of public/private keys in bytes
const int quorum = 3; // quorum for alternative lookup procedure (lp_lookup)
const int token_length = 32; // length of secret tokens
const int table_entry_addr_limit = 10; // max number of addrs allowed for one table entry
const std::string message_protocol = "udp";
const std::string transport_protocol = "tcp";
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

struct net_addr {
    typedef boost::variant<tcp::endpoint, udp::endpoint> endp;

    enum {
        t_msg,
        t_txp
    } transport_type;

    std::string addr;
    u16 port;

    net_addr() : transport_type(t_msg), addr(), port(0) {  }
    net_addr(const std::string& t, const std::string& a, u16 p) : addr(a), port(p) {
        if(t == proto::message_protocol) transport_type = t_msg;
        else if(t == proto::transport_protocol) transport_type = t_txp;
    }

    udp::endpoint udp_endpoint() const {
        return udp::endpoint{boost::asio::ip::address::from_string(addr), port};
    }

    tcp::endpoint tcp_endpoint() const {
        return tcp::endpoint{boost::asio::ip::address::from_string(addr), port};
    }

    bool operator==(const net_addr& rhs) const {
        return transport_type == rhs.transport_type && addr == rhs.addr && port == rhs.port; 
    }

    std::string transport() const {
        switch(transport_type) {
        case t_msg: return proto::message_protocol;
        case t_txp: return proto::transport_protocol;
        default: return "unknown";
        }
    }

    std::string to_string() const {
        return fmt::format("{}:{}:{}", transport(), addr, port);
    }
};

// for outgoing messages or internal work
struct routing_table_entry {
    typedef std::pair<net_addr, int> mi_addr;

    hash_t id;
    std::vector<mi_addr> addresses;

    routing_table_entry() : id(0), addresses{} { }
    routing_table_entry(hash_t i, const net_addr& a) :
        id(i), addresses{ { a, 0 } } { }
};

// object used for individual networking operations.
// basically for incoming messages
struct net_peer {
    hash_t id;
    net_addr addr;

    net_peer(hash_t id_, const net_addr& addr_) : id(id_), addr(addr_) { }
    bool operator==(const net_peer& rhs) { return id == rhs.id && addr == rhs.addr; }
    bool operator!=(const net_peer& rhs) { return !(*this == rhs); }
} static empty_net_peer{ 0, net_addr("", "", 0) };

// for when we've resolved a net_peer from routing_table
struct net_contact {
    hash_t id;
    std::vector<net_addr> addresses;

    net_contact() : id(0), addresses() { }
    net_contact(hash_t id_, const std::vector<net_addr>& addrs) : id(id_), addresses(addrs) { }
    explicit net_contact(const net_addr& a) : id(0), addresses{ a } { }
    explicit net_contact(const net_peer& p) : id(p.id), addresses{ p.addr } { }
    explicit net_contact(const routing_table_entry& rte) : id(rte.id) {
        std::transform(rte.addresses.begin(), rte.addresses.end(), std::back_inserter(addresses),
            [](const routing_table_entry::mi_addr& a) { return a.first; });
    }
    bool operator==(const net_contact& c) { return c.id == id; }
};

namespace util { // utilities

static u64 time_now() {
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
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
static unsigned int crc32b(const unsigned char *message) {
   int i, j;
   unsigned int crc, mask;

   i = 0;
   crc = 0xFFFFFFFF;
   while (message[i] != 0) {
      unsigned int byte = message[i];            // Get next byte.
      crc = crc ^ byte;
      for (j = 7; j >= 0; j--) {    // Do eight times.
         mask = -(crc & 1);
         crc = (crc >> 1) ^ (0xEDB88320 & mask);
      }
      i = i + 1;
   }
   return ~crc;
}

static hash_t hash(const std::string& s) {
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

// encode hash into base58 (https://learnmeabitcoin.com/technical/base58)
static std::string enc58(hash_t h) {
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
static hash_t dec58(std::string s) {
    hash_t result = 0;
    
    for(std::size_t i = 0; i != s.size(); i++) {
        const char* p = std::strchr(b58map, s[i]);
        if(p == NULL)
            throw std::runtime_error("invalid base58 character");

        result = result * 58 + (p - b58map);
    }

    return result;
}

static std::string string_to_hex(const std::string& input) {
    static const char hex_digits[] = "0123456789ABCDEF";

    std::string output;
    output.reserve(input.length() * 2);
    
    for (unsigned char c : input) {
        output.push_back(hex_digits[c >> 4]);
        output.push_back(hex_digits[c & 15]);
    }

    return output;
}

template <typename T>
static T deserialize(const std::string& s) {
    msgpack::object_handle oh;
    msgpack::unpack(oh, s.data(), s.size());
    msgpack::object obj = oh.get();
    return obj.as<T>();
}

template <typename T>
static std::string serialize(T d) {
    std::stringstream ss;
    msgpack::pack(ss, d);
    return ss.str();
}

}

}
}

#endif