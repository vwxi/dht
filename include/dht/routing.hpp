#ifndef _ROUTING_HPP
#define _ROUTING_HPP

#include "util.hpp"
#include "proto.hpp"
#include "node.hpp"
#include "bucket.hpp"

namespace dht {

#define TRAVERSE \
    tree* ptr = root; \
    int i = 0; \
    traverse(req->id, &ptr, i); \
    if(!ptr) return; \
    if(!ptr->data) return; \
    auto it = std::find_if(ptr->data->begin(), ptr->data->end(), \
        [&](std::shared_ptr<node> p) { return p->id == req->id; }); 

class routing_table {
public:
    struct tree {
        struct tree* left;
        struct tree* right;
        std::shared_ptr<bucket<proto::K>> data;

        tree() {
            left = right = nullptr;
            data = std::make_shared<bucket<proto::K>>(); 
        }

        ~tree() {
            data.reset();
            delete left;
            delete right;
        }
    };

    routing_table(hash_t id_) : id(id_) {
        root = new tree;
        root->data = std::make_shared<bucket<proto::K>>();
    };

    ~routing_table() {
        delete root;
    }

    void traverse(hash_t id, tree** ptr, int& i) {
        if(!ptr) return;
        if(!*ptr) return;

        while((*ptr)->data == nullptr) {
            if(id[proto::I - i++]) {
                if(!(*ptr)->right) { *ptr = NULL; return; }
                *ptr = (*ptr)->right;
            } else {
                if(!(*ptr)->left) { *ptr = NULL; return; }
                *ptr = (*ptr)->left;
            }
        }
    }
    
    void split(tree* t, int i) {
        if(!t) return;
        if(!t->data) return;

        t->left = new tree;
        if(!t->left) return;

        t->right = new tree;
        if(!t->right) return;

        for(auto& it : (*t->data)) {
            if(it->id[proto::I - i]) {
                t->right->data->push_back(it);
            } else { 
                t->left->data->push_back(it);
            }
        }

        t->data->clear();
        t->data.reset();
    }

    void update(std::shared_ptr<node> req) {
        TRAVERSE;

        hash_t prefix(((~hash_t(0) << (proto::I - i)) & ~hash_t(0)));
            
        if(ptr->data->size() < ptr->data->max_size) {
            spdlog::warn("bucket isn't full, update node {}", util::htos(req->id));
            ptr->data->update(req, true);
        } else {
            if((req->id & prefix) == (id & prefix)) {
                if(it == ptr->data->end()) {
                    spdlog::warn("bucket is within prefix, split");
                    split(ptr, i);
                } else {
                    spdlog::warn("bucket is nearby and full");
                    ptr->data->update(req, true);
                }
            } else {
                spdlog::warn("bucket is far and full");
                ptr->data->update(req, false);
            }
        }
    }

    void update_pending(std::shared_ptr<node> req) {
        TRAVERSE;

        if(it != ptr->data->end()) {
            if(req->missed_pings++ < proto::M) {
                req->missed_pings--;
                ptr->data->splice(ptr->data->end(), *(ptr->data), it);
                spdlog::info("pending node {} updated", util::htos(req->id));
            } else {
                ptr->data->erase(it);
                spdlog::info("erasing pending node {}", util::htos(req->id));
            }
        }
    }

    void delete_pending(std::shared_ptr<node> req) {
        TRAVERSE;

        if(it != ptr->data->end()) {
            spdlog::info("delete_pending: it = {}", util::htos((*it)->id));
            ptr->data->erase(it);
        }
    }

    hash_t id;
    
private:
    tree* root;
};

}

#endif