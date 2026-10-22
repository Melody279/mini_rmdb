// Day 2 B+ 树单元测试：range_query / erase / validate_tree
// 覆盖计划文档第二天验收点：
//   - B+ 树支持点查询和范围查询
//   - 随机一致性：与 std::map 对拍 insert / get / erase / range_query
//   - 全量结构校验通过（插入后、删除后）

#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

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

// ---------- range_query ----------

static void test_range_query_basic() {
    BPlusTree tree;
    std::vector<int> keys = gen_sorted(1000);
    for (int k : keys) {
        tree.insert(k, rid_of(k));
    }

    CHECK(tree.range_query(100, 199).size() == 100);
    CHECK(tree.range_query(0, 999).size() == 1000);
    CHECK(tree.range_query(1000, 2000).empty());   // 上界之外
    CHECK(tree.range_query(50, 40).empty());       // 空范围（lower > upper）
    CHECK(tree.range_query(42, 42).size() == 1);   // 单点范围

    // 升序校验
    auto r = tree.range_query(100, 199);
    for (std::size_t i = 0; i < r.size(); ++i) {
        CHECK(r[i] == rid_of(100 + static_cast<int>(i)));
    }
}

static void test_range_query_boundary() {
    BPlusTree tree;
    for (int k = 0; k < 1000; ++k) {
        tree.insert(k, rid_of(k));
    }
    CHECK(tree.range_query(0, 0).size() == 1);
    CHECK(tree.range_query(999, 999).size() == 1);
    // 边界值横跨叶子分裂边界（MAX_KEYS=4，25 个叶子）
    CHECK(tree.range_query(3, 4).size() == 2);
    CHECK(tree.range_query(4, 7).size() == 4);
    CHECK(tree.range_query(15, 20).size() == 6);
}

static void test_range_query_empty_tree() {
    BPlusTree tree;
    CHECK(tree.range_query(0, 100).empty());
}

static void test_range_query_vs_map() {
    // 随机 key 集上的范围查询对拍
    std::mt19937 rng(2026);
    std::uniform_int_distribution<int> key_dist(0, 9999);
    std::map<int, Rid> reference;
    BPlusTree tree;
    for (int i = 0; i < 3000; ++i) {
        int k = key_dist(rng);
        if (reference.emplace(k, rid_of(k)).second) {
            CHECK(tree.insert(k, rid_of(k)));
        }
    }
    for (int t = 0; t < 200; ++t) {
        int lo = key_dist(rng);
        int hi = lo + static_cast<int>(key_dist(rng) % 500);
        auto expected = reference.lower_bound(lo);
        std::size_t expect_count = 0;
        for (auto it = expected; it != reference.end() && it->first <= hi; ++it) {
            ++expect_count;
        }
        std::vector<Rid> got = tree.range_query(lo, hi);
        CHECK(got.size() == expect_count);
    }
    CHECK(tree_ok(tree));
}

// ---------- erase ----------

static void test_erase_basic() {
    BPlusTree tree;
    for (int i = 0; i < 10; ++i) {
        tree.insert(i, rid_of(i));
    }
    CHECK(tree.erase(5));
    CHECK(!tree.get(5).has_value());
    CHECK(tree.size() == 9);
    CHECK(!tree.erase(5));       // 重复删除返回 false
    CHECK(!tree.erase(9999));    // 不存在 key
    CHECK(tree.get(4).has_value());
    CHECK(tree.get(6).has_value());
    CHECK(tree_ok(tree));
}

static void test_erase_all() {
    BPlusTree tree;
    for (int i = 0; i < 100; ++i) {
        tree.insert(i, rid_of(i));
    }
    for (int i = 0; i < 100; ++i) {
        CHECK(tree.erase(i));
    }
    CHECK(tree.size() == 0);
    CHECK(tree.height() == 0);
    CHECK(!tree.get(0).has_value());
    CHECK(tree.range_query(0, 999).empty());
    CHECK(tree_ok(tree));
}

static void test_erase_reinsert() {
    BPlusTree tree;
    for (int i = 0; i < 50; ++i) {
        tree.insert(i, rid_of(i));
    }
    CHECK(tree.erase(30));
    CHECK(tree.insert(30, Rid{9, 9}));   // 删除后可重新插入
    auto r = tree.get(30);
    CHECK(r.has_value());
    CHECK(r.has_value() && r.value() == Rid(9, 9));
    CHECK(tree_ok(tree));
}

static void test_erase_random_vs_map() {
    // 随机插入 + 随机删除，与 std::map 对拍
    std::mt19937 rng(777);
    std::uniform_int_distribution<int> key_dist(0, 9999);
    std::map<int, Rid> reference;
    BPlusTree tree;

    for (int i = 0; i < 5000; ++i) {
        int k = key_dist(rng);
        if (reference.emplace(k, rid_of(k)).second) {
            tree.insert(k, rid_of(k));
        }
    }
    for (int i = 0; i < 3000; ++i) {
        int k = key_dist(rng);
        bool ref_ok = reference.erase(k) > 0;
        CHECK(tree.erase(k) == ref_ok);
    }
    CHECK(tree.size() == reference.size());
    // 抽查对拍
    for (int i = 0; i < 2000; ++i) {
        int k = key_dist(rng);
        auto it = reference.find(k);
        auto r = tree.get(k);
        CHECK(r.has_value() == (it != reference.end()));
        if (it != reference.end()) {
            CHECK(r.has_value() && r.value() == it->second);
        }
    }
    CHECK(tree_ok(tree));
}

// ---------- validate_tree ----------

static void test_validate_after_insert() {
    BPlusTree tree;
    std::vector<int> keys = gen_random_unique(10000, 42);
    for (int k : keys) {
        tree.insert(k, rid_of(k));
    }
    CHECK(tree_ok(tree));
    CHECK(tree.height() >= 4);  // 10000 条、MAX_KEYS=4 的树高验证（>2 即可，>=4 供参考）
}

static void test_validate_after_mixed_ops() {
    // 插入 + 删除混合后结构仍合法（实验版删除允许欠载，但划分有效性保持）
    std::mt19937 rng(31415);
    std::uniform_int_distribution<int> key_dist(0, 9999);
    std::uniform_int_distribution<int> op_dist(0, 9);
    BPlusTree tree;
    std::map<int, Rid> reference;

    for (int step = 0; step < 8000; ++step) {
        int k = key_dist(rng);
        if (op_dist(rng) < 7) {
            if (reference.emplace(k, rid_of(k)).second) {
                tree.insert(k, rid_of(k));
            }
        } else {
            if (reference.erase(k) > 0) {
                tree.erase(k);
            }
        }
    }
    CHECK(tree.size() == reference.size());
    CHECK(tree_ok(tree));
    for (int i = 0; i < 500; ++i) {
        int k = key_dist(rng);
        auto r = tree.get(k);
        auto it = reference.find(k);
        CHECK(r.has_value() == (it != reference.end()));
    }
}

// ---------- 综合：insert/get/erase/range_query 全对拍 ----------

static void test_mixed_ops_with_range() {
    std::mt19937 rng(4242);
    std::uniform_int_distribution<int> key_dist(0, 4999);
    std::uniform_int_distribution<int> op_dist(0, 15);
    std::map<int, Rid> reference;
    BPlusTree tree;

    for (int step = 0; step < 6000; ++step) {
        int k = key_dist(rng);
        int op = op_dist(rng);
        if (op < 6) {  // insert
            bool ref_ok = reference.emplace(k, rid_of(k)).second;
            CHECK(tree.insert(k, rid_of(k)) == ref_ok);
        } else if (op < 11) {  // get
            auto r = tree.get(k);
            auto it = reference.find(k);
            CHECK(r.has_value() == (it != reference.end()));
        } else if (op < 14) {  // erase
            bool ref_ok = reference.erase(k) > 0;
            CHECK(tree.erase(k) == ref_ok);
        } else {  // range
            int hi = k + 50;
            auto it = reference.lower_bound(k);
            std::size_t expect_count = 0;
            for (auto p = it; p != reference.end() && p->first <= hi; ++p) {
                ++expect_count;
            }
            CHECK(tree.range_query(k, hi).size() == expect_count);
        }
    }
    CHECK(tree.size() == reference.size());
    CHECK(tree_ok(tree));
}

int main() {
    test_range_query_basic();
    test_range_query_boundary();
    test_range_query_empty_tree();
    test_range_query_vs_map();

    test_erase_basic();
    test_erase_all();
    test_erase_reinsert();
    test_erase_random_vs_map();

    test_validate_after_insert();
    test_validate_after_mixed_ops();

    test_mixed_ops_with_range();

    std::printf("\n========================================\n");
    std::printf("B+ Tree Day2 Test: %d passed, %d failed\n", g_pass, g_fail);
    std::printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
