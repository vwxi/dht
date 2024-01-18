#ifndef _UPNP_H
#define _UPNP_H

#include "util.hpp"

namespace tulip {
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
    upnp(bool);
    ~upnp();

    bool forward_port(std::string, t_protocol, u16);
    std::string get_external_ip_address();
    std::string get_local_ip_address();

private:
    UPNPDev* devlist;
    UPNPUrls urls;
    IGDdatas data;
    std::string local_ip;

    std::vector<port_mapping> mappings;
};

}
}

#endif