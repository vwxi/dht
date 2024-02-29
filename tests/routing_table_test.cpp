#include "catch2/catch_all.hpp"

#include "routing.hpp"
#include "network.hpp"

using namespace lotus;
using namespace lotus::dht;

static test::mock_rt_net_resp mock_resp_net(true, 0, [](net_peer, proto::message) {});
static test::mock_rt_net_unresp mock_unresp_net(true, 0, [](net_peer, proto::message) {});
static test::mock_rt_net_maybe mock_maybe_net(true, 0, [](net_peer, proto::message) {});

static std::random_device rd;
static hash_reng_t treng(rd());

typedef routing_table<test::mock_rt_net_resp, bucket<test::mock_rt_net_resp>> rt_resp;
typedef routing_table<test::mock_rt_net_unresp, bucket<test::mock_rt_net_unresp>> rt_unresp;
typedef routing_table<test::mock_rt_net_maybe, bucket<test::mock_rt_net_maybe>> rt_maybe;
typedef std::shared_ptr<rt_resp> rt_resp_ptr;
typedef std::shared_ptr<rt_unresp> rt_unresp_ptr;
typedef std::shared_ptr<rt_maybe> rt_maybe_ptr;

net_addr gen_addr() {
    return net_addr("udp", "127.0.0.1", std::rand() % USHRT_MAX);
}

// should split based on prefixes (hence prefix trie)
TEST_CASE("split", "split") {
    hash_t own_id = hash_t(1) << (proto::bit_hash_width - 1);

    rt_resp_ptr table = std::make_shared<rt_resp>(own_id, mock_resp_net);
    REQUIRE_NOTHROW(table->init());
    
    // fill main bucket to split
    for(int i = 0; i < proto::bucket_size; i++) {
        table->update(net_peer(hash_t(i), gen_addr()));
    }

#define TEST_TREE(r) \
    { \
        REQUIRE(!r->leaf); \
        REQUIRE(r->left != nullptr); \
        REQUIRE(r->right != nullptr); \
        bool left_ok = true, right_ok = true; \
        hash_t mask = hash_t(1) << (proto::bit_hash_width - (r->prefix.cutoff+1)); \
        for(const auto& rte : r->left->data) { \
            if((rte.id & mask)) { \
                left_ok = false; \
            } \
        } \
        for(const auto& rte : r->right->data) { \
            if(!(rte.id & mask)) { \
                right_ok = false; \
            } \
        } \
        REQUIRE(left_ok); \
        REQUIRE(right_ok); \
    }

    // introduce nearby peer
    table->update(net_peer((hash_t(3) << (proto::bit_hash_width - 2)), gen_addr()));

    TEST_TREE(table->root);

    // what should happen is there's only 1 local peer
    REQUIRE(table->root->left->data.size() == proto::bucket_size);
    REQUIRE(table->root->right->data.size() == 1);

    // fill local peers
    for(int i = 0; i < proto::bucket_size; i++) {
        table->update(net_peer((hash_t(1) << (proto::bit_hash_width-1)) | i, gen_addr()));
    }

    TEST_TREE(table->root->right);

    REQUIRE(table->root->right->left->data.size() == proto::bucket_size);
    REQUIRE(table->root->right->right->data.size() == 1);
#undef TEST_TREE
}

// should ignore new entries
TEST_CASE("faraway node with responsive behavior", "far_resp") {
    hash_t own_id = hash_t(1) << (proto::bit_hash_width - 1);

    rt_resp_ptr table = std::make_shared<rt_resp>(own_id, mock_resp_net);
    REQUIRE_NOTHROW(table->init());

    // fill main bucket to split
    for(int i = 0; i < proto::bucket_size; i++) {
        table->update(net_peer(own_id | i, gen_addr()));
    }

    // insert far node
    table->update(net_peer(hash_t(1), gen_addr()));

    for(int i = 2; i < proto::bucket_size+2; i++) {
        table->update(net_peer(hash_t(i), gen_addr()));
    }

    for(int i = 2; i < proto::bucket_size+2; i++) {
        table->update(net_peer(hash_t(70) | i, gen_addr()));
    }
    
    REQUIRE(table->root->left->data.size() == proto::bucket_size);
}

// should add from cache
TEST_CASE("faraway node with unresponsive behavior", "far_unresp") {
    hash_t own_id = hash_t(1) << (proto::bit_hash_width - 1);

    rt_unresp_ptr table = std::make_shared<rt_unresp>(own_id, mock_unresp_net);
    REQUIRE_NOTHROW(table->init());

    // fill main bucket to split
    for(int i = 0; i < proto::bucket_size; i++) {
        table->update(net_peer(own_id | i, gen_addr()));
    }

    // insert far nodes
    net_peer to_stale(hash_t(1), gen_addr());
    table->update(to_stale);

    for(int i = 2; i < proto::bucket_size+1; i++) {
        table->update(net_peer(hash_t(i), gen_addr()));
    }

    // fill replacement cache
    for(int i = 0; i < proto::repl_cache_size+1; i++) {
        table->update(net_peer(hash_t(70) | i, gen_addr()));
    }

    // should become max staleness
    for(int i = 0; i <= proto::missed_pings_allowed + 1; i++) {
        table->update(to_stale);
    }

    boost::optional<routing_table_entry> stale = table->find(to_stale.id),
        added = table->find(hash_t(70) | proto::repl_cache_size);

    REQUIRE(!stale.has_value());
    REQUIRE(added.has_value());
}

