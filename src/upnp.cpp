#include "upnp.h"

namespace tulip {
namespace dht {

// looking at miniupnpc/src/upnpc.c
upnp::upnp(bool ipv6) {
    int error = 0;
    char lan_addr[64] = "unset";

    if(!(devlist = upnpDiscover(
        2000, NULL, NULL, 
        UPNP_LOCAL_PORT_ANY, ipv6, 2, &error))) {
        throw std::runtime_error(fmt::format("upnp discover error, {}", error));
    }
    
    if(UPNP_GetValidIGD(devlist, &urls, &data, lan_addr, sizeof(lan_addr)) != 1) {
        throw std::runtime_error("upnp could not find igd");
    }

    local_ip = std::string(lan_addr);
}

std::string upnp::get_external_ip_address() {
    char ip[40] = { 0 };
    if(UPNP_GetExternalIPAddress(
        urls.controlURL, 
        data.first.servicetype, 
        ip) == UPNPCOMMAND_SUCCESS) {
        return std::string(ip);
    } else {
        return std::string();
    }
}

std::string upnp::get_local_ip_address() {
    return local_ip;
}

upnp::~upnp() {
    FreeUPNPUrls(&urls);
    freeUPNPDevlist(devlist);
    devlist = NULL;
}

}
}