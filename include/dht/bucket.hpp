#ifndef _BUCKET_HPP
#define _BUCKET_HPP

#include "util.hpp"
#include "node.hpp"

namespace dht {

template <std::size_t K>
class bucket : 
    public std::list<std::shared_ptr<node>>,
    public std::enable_shared_from_this<std::list<std::shared_ptr<node>>> {
public:
    bucket() : max_size(K), last_seen(0) { };

    ~bucket() {
        for(auto& n : *this) {
            n.reset();
        }
    }

    void update(std::shared_ptr<node> req, bool nearby) {
        auto rit = std::find_if(begin(), end(), 
            [req](std::shared_ptr<node> p) { return p->id == req->id; });

        if(!nearby) {
            std::shared_ptr<node> beg = *begin();
            spdlog::info("checking if node {} ({}:{}) is alive", util::htos(beg->id), beg->addr, beg->port);
            beg->send_alive();
            goto end;
        }

        if(rit != end()) {
            std::shared_ptr<node> _p = *rit;            
            splice(end(), *this, rit);

            spdlog::info("exists already, moved node {} to tail. size: {}", util::htos(_p->id), size());
            goto end;
        }

        if(size() < max_size) {
            push_back(req);
            spdlog::info("pushed back, size: {}", size());
            goto end;
        }

end:
        last_seen = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    }

    std::shared_ptr<node> get(std::shared_ptr<node> req, hash_t id) {
        auto it = std::find_if(begin(), end(), 
            [id](std::shared_ptr<node> p) { return p->id == id; });
        
        update(req);
        
        if(it == end()) return nullptr;
        return (*it);
    }

    u64 last_seen;
    std::size_t max_size; 
};

}

#endif