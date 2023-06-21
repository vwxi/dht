#ifndef _BUCKET_H
#define _BUCKET_H

#include "util.hpp"

namespace tulip {
namespace dht {

class routing_table;
class peer;
class node;

class bucket : 
    public std::list<peer>,
    public std::enable_shared_from_this<std::list<peer>> {
public:
    friend boost::serialization::access;
    
    bucket();
    ~bucket();

    template <class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & max_size;
        ar & boost::serialization::base_object<std::list<peer>>(*this);
    }
    
    void update(routing_table&, peer, bool);

    u64 last_seen;
    std::size_t max_size;
};

}
}

#endif