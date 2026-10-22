// Day3 增补测试：负数 key 全覆盖 + 随机操作对拍
// 背景：原数据生成器只产生 [0, n) 的 key，负数路径从未被验证。
// 本测试：
//   - B+ 树：混入负数的随机 insert/get/erase/range 与 std::map 对拍，定期全量结构校验
//   - 边界：删 -1/0/1 后不返回旧值，负区间范围查询
//   - 自适应哈希：负数 key 的缓存晋升 / 失效一致性

#include <cstdio>
#include <map>
#include <random>
#include <string>
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

static bool tree_ok(const BPlusTree& tree) {
    std::string err;
    const bool ok = tree.validate_tree(err);
    if (!ok) {
        std::printf("  [validate_tree] %s\n", err.c_str());
    }
    return ok;
}

// ---------- 1. B+ 树：负数 key 随机混合操作对拍 ----------

static void test_bplus_negative_mixed_vs_map() {
    // 10000 个不重复 key，范围 [-5000, 5000)：一半负数
    std::vector<int> keys = gen_random_unique_range(10000, -5000, 42);
    std::mt19937 rng(99);

    BPlusTree tree;
    std::map<int, Rid> reference;

    // 阶段 A：插入前 7000 个（与 map 同步）
    for (int i = 0; i < 7000; ++i) {
        const int k = keys[static_cast<std::size_t>(i)];
        tree.insert(k, rid_of(k));
        reference[k] = rid_of(k);
    }
    CHECK(tree_ok(tree));

    // 阶段 B：2000 轮随机操作（get / erase / range），全程对拍
    for (int step = 0; step < 2000; ++step) {
        const int op = static_cast<int>(rng() % 100);
        if (op < 40) {  // get：随机 key（可能不存在，对拍空/非空）
            const int k = keys[rng() % keys.size()];
            auto res = tree.get(k);
            auto it = reference.find(k);
            CHECK(res.has_value() == (it != reference.end()));
            if (res.has_value() && it != reference.end()) {
                CHECK(res.value() == it->second);
            }
        } else if (op < 70) {  // erase
            const int k = keys[rng() % keys.size()];
            const bool got = tree.erase(k);
            CHECK(got == (reference.erase(k) > 0));
        } else {  // range：负区间也覆盖（lower 可负、upper 可负）
            int lo = -5000 + static_cast<int>(rng() % 9000);
            int hi = lo + static_cast<int>(rng() % 300);
            auto got = tree.range_query(lo, hi);
            std::vector<Rid> expect;
            for (auto it = reference.lower_bound(lo); it != reference.end() && it->first <= hi; ++it) {
                expect.push_back(it->second);
            }
            CHECK(got.size() == expect.size());
            if (got.size() == expect.size()) {
                for (std::size_t i = 0; i < got.size(); ++i) {
                    CHECK(got[i] == expect[i]);
                }
            }
        }
        if (step % 500 == 499) {
            CHECK(tree_ok(tree));
            CHECK(tree.validate_leaf_chain());
        }
    }

    // 阶段 C：全量比对（reference 里每个 key 都能查到，且值一致）
    for (const auto& kv : reference) {
        auto res = tree.get(kv.first);
        CHECK(res.has_value());
        if (res.has_value()) {
            CHECK(res.value() == kv.second);
        }
    }
    CHECK(tree_ok(tree));

    // 阶段 D：随机查 500 个肯定不存在的 key，确认不会误报
    for (int i = 0; i < 500; ++i) {
        const int k = -9000 + static_cast<int>(rng() % 5000);  // [-9000,-4001) 从未插入
        if (reference.find(k) == reference.end()) {
            CHECK(!tree.get(k).has_value());
        }
    }
}

// ---------- 2. B+ 树：负数边界（删 -1/0/1、负区间） ----------

static void test_bplus_negative_boundary() {
    BPlusTree tree;
    // 手动构造含负数的关键边界集合
    std::vector<int> keys = {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5};
    for (int k : keys) {
        tree.insert(k, rid_of(k));
    }
    CHECK(tree_ok(tree));

    // 全部能查到
    for (int k : keys) {
        auto res = tree.get(k);
        CHECK(res.has_value());
        if (res.has_value()) {
            CHECK(res.value() == rid_of(k));
        }
    }
    // 负数不在集合中
    CHECK(!tree.get(6).has_value());
    CHECK(!tree.get(-6).has_value());

    // 删 -1 / 0 / 1，之后不返回旧值
    CHECK(tree.erase(-1));
    CHECK(tree.erase(0));
    CHECK(tree.erase(1));
    CHECK(!tree.get(-1).has_value());
    CHECK(!tree.get(0).has_value());
    CHECK(!tree.get(1).has_value());
    CHECK(tree.get(-2).has_value());  // 邻接 key 不受影响
    CHECK(tree.get(2).has_value());
    CHECK(tree_ok(tree));
    CHECK(tree.validate_leaf_chain());

    // 负区间范围查询（含删除后的空洞）
    std::map<int, Rid> reference;
    for (int k : keys) {
        if (k != -1 && k != 0 && k != 1) {
            reference[k] = rid_of(k);
        }
    }
    auto got = tree.range_query(-3, 3);
    std::vector<Rid> expect;
    for (auto it = reference.lower_bound(-3); it != reference.end() && it->first <= 3; ++it) {
        expect.push_back(it->second);
    }
    CHECK(got.size() == expect.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        CHECK(got[i] == expect[i]);
    }
    CHECK(tree.range_query(-100, -6).empty());  // 全部在范围外
    CHECK(tree.range_query(-5, -5).size() == 1);  // 负数单点
}

// ---------- 3. 自适应哈希：负数 key 对拍 + 缓存一致性 ----------

static void test_ah_negative_mixed_vs_map() {
    std::vector<int> keys = gen_random_unique_range(8000, -4000, 7);
    std::mt19937 rng(123);

    AdaptiveHashIndex ah;
    std::map<int, Rid> reference;

    for (int i = 0; i < 6000; ++i) {
        const int k = keys[static_cast<std::size_t>(i)];
        ah.insert(k, rid_of(k));
        reference[k] = rid_of(k);
    }

    // 先跑一段热点查询（同一批负数 key 查多次），触发缓存晋升
    for (int i = 0; i < 3000; ++i) {
        const int k = keys[rng() % 3000];  // 集中前 3000 个（含大量负数）
        auto res = ah.get(k);
        auto it = reference.find(k);
        CHECK(res.has_value() == (it != reference.end()));
        if (res.has_value() && it != reference.end()) {
            CHECK(res.value() == it->second);
        }
    }
    CHECK(ah.hash_hit_count() > 0);  // 负数 key 也能正常晋升命中

    // 混合 erase/get：删除后缓存必须失效（不返回旧值）
    for (int step = 0; step < 2000; ++step) {
        const int k = keys[rng() % keys.size()];
        if (step % 2 == 0) {
            const bool got = ah.erase(k);
            CHECK(got == (reference.erase(k) > 0));
        } else {
            auto res = ah.get(k);
            auto it = reference.find(k);
            CHECK(res.has_value() == (it != reference.end()));
            if (res.has_value() && it != reference.end()) {
                CHECK(res.value() == it->second);
            }
        }
    }

    // 全量比对（含负数 key 值一致性）
    for (const auto& kv : reference) {
        auto res = ah.get(kv.first);
        CHECK(res.has_value());
        if (res.has_value()) {
            CHECK(res.value() == kv.second);
        }
    }
}

// ---------- main ----------

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Negative Key Test:\n");
    test_bplus_negative_mixed_vs_map();
    test_bplus_negative_boundary();
    test_ah_negative_mixed_vs_map();
    std::printf("  %d passed, %d failed\n", g_pass, g_fail);
    std::printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
