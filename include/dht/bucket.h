#ifndef _BUCKET_H
#define _BUCKET_H

#include "util.hpp"

namespace dht {

class routing_table;
class peer;
class node;

class bucket : 
    public std::list<peer>,
    public std::enable_shared_from_this<std::list<peer>> {
public:
    friend boost::serialization::access;

    bucket(routing_table&);
    ~bucket();

    template <class Archive>
    void serialize(Archive&, const unsigned int);
    
    void update(peer, bool);

    u64 last_seen;
    std::size_t max_size;

private:
    routing_table& table;
};

}

#endif