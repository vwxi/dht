#ifndef _UPNP_H
#define _UPNP_H

#include "util.hpp"

namespace lotus {
namespace dht {

struct port_mapping {
    std::string protocol;
    u16 port;
    u64 when;
};

enum t_protocol {
    u_UDP,
    u_TCP
};

/// @brief a wrapper for miniupnp. using upnp igd to forward ports

class upnp {
public:
    ~upnp();
    
    void initialize(bool);
    bool forward_port(std::string, t_protocol, u16);
    std::string get_external_ip_address();
    std::string get_local_ip_address();

protected:
    bool initialized = false;

    UPNPDev* devlist;
    UPNPUrls urls;
    IGDdatas data;
    std::string local_ip;

    std::vector<port_mapping> mappings;
};

namespace test {

// fake upnp resolver to always resolve to localhost
class mock_forwarder : public upnp { 
public:
    ~mock_forwarder() { }

    void initialize(bool) { local_ip = "127.0.0.1"; }
    std::string get_external_ip_address() { return local_ip; }
    bool forward_port(std::string, t_protocol, u16) { return true; }
};

}

}
}

#endif