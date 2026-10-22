// Day 2 自适应哈希索引单元测试
// 覆盖计划文档第二天验收点：
//   - 未达到阈值时查询走 B+ 树
//   - 达到阈值后 key 进入哈希缓存
//   - 热点 key 命中缓存、冷门 key 回退 B+ 树
//   - 插入 / 删除后缓存失效，不返回旧值
//   - 范围查询不使用哈希缓存
//   - 优化索引与 B+ 树结果完全一致（10000 次查询对拍）

#include <cstdio>
#include <map>
#include <random>
#include <vector>

#include "adaptive_hash_index.h"
#include "b_plus_tree.h"
#include "data_generator.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (cond) {                                                       \
            ++g_pass;                                                     \
        } else {                                                          \
            ++g_fail;                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

static Rid rid_of(int i) { return Rid{i / 10, i % 10}; }

static void test_below_threshold_uses_bplus() {
    AdaptiveHashIndex idx;
    idx.insert(1, rid_of(1));
    auto r = idx.get(1);
    CHECK(r.has_value());
    CHECK(idx.bplus_fallback_count() == 1);  // 未命中缓存，回退 B+ 树
    CHECK(idx.hash_hit_count() == 0);
    CHECK(idx.cache_size() == 0);            // 1 次 < 阈值 3，未晋升
}

static void test_promote_after_threshold() {
    AdaptiveHashIndex idx;
    idx.insert(1, rid_of(1));

    for (int i = 0; i < 3; ++i) {
        CHECK(idx.get(1).has_value());       // 前 3 次都回退 B+ 树
    }
    CHECK(idx.cache_size() == 1);            // 第 3 次查询后晋升
    CHECK(idx.bplus_fallback_count() == 3);
    CHECK(idx.hash_hit_count() == 0);

    CHECK(idx.get(1).has_value());           // 第 4 次：命中缓存
    CHECK(idx.hash_hit_count() == 1);
    CHECK(idx.bplus_fallback_count() == 3);  // 回退次数不再增加
}

static void test_hot_key_hits_cold_key_falls_back() {
    AdaptiveHashIndex idx;
    // 热点 key：0..199；冷门 key：200..9999
    for (int k = 0; k < 10000; ++k) {
        idx.insert(k, rid_of(k));
    }

    // 冷门 key 各查 1 次：全部回退，不晋升
    for (int k = 200; k < 500; ++k) {
        idx.get(k);
    }
    CHECK(idx.cache_size() == 0);
    CHECK(idx.hash_hit_count() == 0);

    // 热点 key 查 5 次：晋升后命中（只查 k=0 一个 key）
    for (int i = 0; i < 5; ++i) {
        CHECK(idx.get(0).has_value());
    }
    CHECK(idx.cache_size() == 1);
    CHECK(idx.hash_hit_count() == 2);  // 第 4、5 次命中
}

static void test_insert_invalidates_cache() {
    AdaptiveHashIndex idx;
    idx.insert(7, Rid{0, 1});
    for (int i = 0; i < 3; ++i) {
        idx.get(7);   // 晋升
    }
    CHECK(idx.cache_size() == 1);

    // 同一 key 再插入：唯一键被拒绝，数据未变，缓存仍有效
    CHECK(!idx.insert(7, Rid{9, 9}));
    auto r = idx.get(7);
    CHECK(r.has_value() && r.value() == Rid(0, 1));
    CHECK(idx.hash_hit_count() == 1);  // 缓存未被失效，直接命中

    // 新插入一个 key 后缓存应失效（数据变化）
    idx.insert(8, Rid{0, 8});
    auto r8 = idx.get(8);
    CHECK(r8.has_value());
    CHECK(r8.has_value() && r8.value() == Rid(0, 8));
    CHECK(idx.bplus_fallback_count() > 0);  // 走回退，说明缓存未提前命中
}

static void test_erase_invalidates_cache() {
    AdaptiveHashIndex idx;
    idx.insert(7, rid_of(7));
    for (int i = 0; i < 3; ++i) {
        idx.get(7);   // 晋升
    }
    CHECK(idx.cache_size() == 1);

    CHECK(idx.erase(7));
    CHECK(idx.cache_size() == 0);
    // 删除后不会返回旧值
    CHECK(!idx.get(7).has_value());
    CHECK(idx.size() == 0);

    // 删除不存在 key 返回 false
    CHECK(!idx.erase(7));
}

static void test_range_query_uses_bplus() {
    AdaptiveHashIndex idx;
    for (int k = 0; k < 1000; ++k) {
        idx.insert(k, rid_of(k));
    }
    // 先制造热点，让部分 key 进缓存
    for (int k = 0; k < 100; ++k) {
        for (int i = 0; i < 5; ++i) {
            idx.get(k);
        }
    }
    std::size_t hit_before = idx.hash_hit_count();

    // 范围查询完全委托 B+ 树，不消耗缓存命中计数
    std::vector<Rid> r = idx.range_query(100, 199);
    CHECK(r.size() == 100);
    CHECK(idx.hash_hit_count() == hit_before);
    for (std::size_t i = 0; i < r.size(); ++i) {
        CHECK(r[i] == rid_of(100 + static_cast<int>(i)));
    }
}

static void test_consistency_10000_queries() {
    // 插入 10000 条，构造 80/20 热点工作负载，与 B+ 树逐条对拍
    BPlusTree tree;
    AdaptiveHashIndex idx;
    std::vector<int> keys = gen_random_unique(10000, 42);
    for (int k : keys) {
        tree.insert(k, rid_of(k));
        idx.insert(k, rid_of(k));
    }

    // 80% 查询落在前 20% key，20% 落在其余 key
    std::mt19937 rng(99);
    std::uniform_int_distribution<int> hot_dist(0, 1999);
    std::uniform_int_distribution<int> cold_dist(2000, 9999);
    std::uniform_int_distribution<int> pick(0, 9);

    for (int i = 0; i < 10000; ++i) {
        int k = (pick(rng) < 8) ? hot_dist(rng) : cold_dist(rng);
        auto r_tree = tree.get(k);
        auto r_idx = idx.get(k);
        CHECK(r_tree.has_value() == r_idx.has_value());
        if (r_tree.has_value()) {
            CHECK(r_idx.has_value() && r_idx.value() == r_tree.value());
        }
    }
    // 热点缓存确实被利用：既有命中也有回退
    CHECK(idx.hash_hit_count() > 0);
    CHECK(idx.bplus_fallback_count() > 0);
}

static void test_consistency_with_erase() {
    // 随机 insert / get / erase 与 std::map 对拍（覆盖删除后缓存一致性）
    std::mt19937 rng(20261021);
    std::uniform_int_distribution<int> key_dist(0, 4999);
    std::uniform_int_distribution<int> op_dist(0, 9);
    std::map<int, Rid> reference;
    AdaptiveHashIndex idx;

    for (int step = 0; step < 8000; ++step) {
        int k = key_dist(rng);
        int op = op_dist(rng);
        if (op < 4) {
            bool ref_ok = reference.emplace(k, rid_of(k)).second;
            CHECK(idx.insert(k, rid_of(k)) == ref_ok);
        } else if (op < 8) {
            auto r = idx.get(k);
            auto it = reference.find(k);
            CHECK(r.has_value() == (it != reference.end()));
            if (it != reference.end()) {
                CHECK(r.has_value() && r.value() == it->second);
            }
        } else {
            bool ref_ok = reference.erase(k) > 0;
            CHECK(idx.erase(k) == ref_ok);
            CHECK(!idx.get(k).has_value());  // 删除后不返回旧值
        }
    }
    CHECK(idx.size() == reference.size());
}

int main() {
    test_below_threshold_uses_bplus();
    test_promote_after_threshold();
    test_hot_key_hits_cold_key_falls_back();
    test_insert_invalidates_cache();
    test_erase_invalidates_cache();
    test_range_query_uses_bplus();
    test_consistency_10000_queries();
    test_consistency_with_erase();

    std::printf("\n========================================\n");
    std::printf("AdaptiveHash Test: %d passed, %d failed\n", g_pass, g_fail);
    std::printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
