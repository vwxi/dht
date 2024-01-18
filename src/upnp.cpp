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
    for(port_mapping m : mappings) {
        std::string p(std::to_string(m.port));
        int r = UPNP_DeletePortMapping(
            urls.controlURL, data.first.servicetype, 
            p.c_str(), m.protocol.c_str(), 
            NULL);
        spdlog::debug("upnp: deleting port mapping for port {}, returned: {}", m.port, r);
    }

    FreeUPNPUrls(&urls);
    freeUPNPDevlist(devlist);
    devlist = NULL;
}

bool upnp::forward_port(std::string description, t_protocol proto, u16 port) {
    int r;
    
    // prune all expired leases
    u64 current_time = util::time_now();
    mappings.erase(std::remove_if(mappings.begin(), mappings.end(),
        [&](const port_mapping& m) {
            return current_time - m.when > constants::upnp_release_interval;
        }), mappings.end());

    if(std::find_if(mappings.begin(), mappings.end(), [&](const port_mapping& m) {
        return m.port == port;
    }) != mappings.end()) {
        return true; // already mapped
    }

    std::string ext_port = std::to_string(port),
        lease = std::to_string(constants::upnp_release_interval);

    std::string proto_s;
    switch(proto) {
    case u_UDP: proto_s = "UDP"; break;
    case u_TCP: proto_s = "TCP"; break;
    }

    if((r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, 
        ext_port.c_str(), ext_port.c_str(), 
        get_local_ip_address().c_str(), description.c_str(), proto_s.c_str(), 
        NULL, lease.c_str())) != UPNPCOMMAND_SUCCESS) {
        spdlog::error("upnp: AddPortMapping error: {}", strupnperror(r));
        return false;
    }

    char int_client[40];
    char int_port[6];
    if((r = UPNP_GetSpecificPortMappingEntry(
        urls.controlURL, data.first.servicetype, ext_port.c_str(), proto_s.c_str(),
        NULL, int_client, int_port, &description[0],
        NULL, &lease[0])) != UPNPCOMMAND_SUCCESS) {
        spdlog::error("upnp: GetSpecificPortMappingEntry error: {}", strupnperror(r));
        return false;
    }

    mappings.push_back({
        .protocol = proto_s,
        .port = port,
        .when = util::time_now()
    });

    spdlog::debug("upnp: adding port mapping for port {}", port);

    return true;
}

}
}