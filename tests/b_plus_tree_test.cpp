// 第一天 B+ 树单元测试（零依赖：自写轻量断言，不需要 GoogleTest）
// 覆盖计划文档第一天的全部验收点：
//   1. 插入 1000 条不崩溃、全部可查
//   2. 查询不存在 key 返回空
//   3. 重复 key 行为明确（唯一键，返回 false）
//   4. 触发至少一次叶子节点分裂（MAX_KEYS=4，插入 5 个即触发）
//   5. 触发至少一次内部节点分裂（插入 100 个，树高 >= 3）
//
// 运行方式：
//   g++ -std=c++1z -O2 -Wall -Wextra -I include src/b_plus_tree.cpp tests/b_plus_tree_test.cpp -o build/bplus_test.exe
//   build\bplus_test.exe

#include <cstdio>
#include <map>
#include <random>
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

// 把 vector<int> 全部插入，返回失败（重复）的数量
static int insert_all(BPlusTree& tree, const std::vector<int>& keys) {
    int dup = 0;
    for (int k : keys) {
        if (!tree.insert(k, rid_of(k))) {
            ++dup;
        }
    }
    return dup;
}

// 校验所有 key 均能查到且 Rid 正确
static void check_all_gettable(const BPlusTree& tree, const std::vector<int>& keys) {
    for (int k : keys) {
        auto r = tree.get(k);
        CHECK(r.has_value());
        if (r.has_value()) {
            CHECK(r.value() == rid_of(k));
        }
    }
}

static void test_empty_tree() {
    BPlusTree tree;
    CHECK(tree.size() == 0);
    CHECK(tree.height() == 0);
    CHECK(!tree.get(0).has_value());
    CHECK(!tree.get(999999).has_value());
    CHECK(tree.validate_leaf_chain());
}

static void test_single_insert() {
    BPlusTree tree;
    CHECK(tree.insert(42, Rid{3, 7}));
    auto r = tree.get(42);
    CHECK(r.has_value());
    CHECK(r.has_value() && r.value() == Rid(3, 7));
    CHECK(tree.size() == 1);
    CHECK(tree.height() == 1);
}

static void test_sorted_insert_1000() {
    BPlusTree tree;
    std::vector<int> keys = gen_sorted(1000);
    CHECK(insert_all(tree, keys) == 0);
    CHECK(tree.size() == 1000);
    check_all_gettable(tree, keys);
}

static void test_reversed_insert_1000() {
    BPlusTree tree;
    std::vector<int> keys = gen_reversed(1000);
    CHECK(insert_all(tree, keys) == 0);
    CHECK(tree.size() == 1000);
    check_all_gettable(tree, keys);
}

static void test_random_insert_10000() {
    BPlusTree tree;
    std::vector<int> keys = gen_random_unique(10000, 42);
    CHECK(insert_all(tree, keys) == 0);
    CHECK(tree.size() == 10000);
    check_all_gettable(tree, keys);
    CHECK(tree.validate_leaf_chain());
}

static void test_duplicate_key() {
    BPlusTree tree;
    CHECK(tree.insert(5, Rid{0, 0}));
    CHECK(!tree.insert(5, Rid{9, 9}));  // 唯一键：重复插入被拒绝
    auto r = tree.get(5);
    CHECK(r.has_value());
    CHECK(r.has_value() && r.value() == Rid(0, 0));  // 原值保持不变
    CHECK(tree.size() == 1);
}

static void test_missing_key() {
    BPlusTree tree;
    std::vector<int> keys = gen_sorted(1000);
    insert_all(tree, keys);
    CHECK(!tree.get(1000).has_value());    // 紧邻上界之外
    CHECK(!tree.get(5000).has_value());    // 范围外
    CHECK(!tree.get(-1).has_value());      // 负 key（未插入）
}

static void test_leaf_split() {
    BPlusTree tree;
    // MAX_KEYS=4：插入第 5 个 key 必然触发一次叶子分裂
    std::vector<int> keys = gen_sorted(10);
    insert_all(tree, keys);
    CHECK(tree.height() >= 2);
    CHECK(tree.validate_leaf_chain());
    check_all_gettable(tree, keys);
}

static void test_internal_split() {
    BPlusTree tree;
    // MAX_KEYS=4：100 条数据至少需要 25 个叶子，
    // 高度为 2 时根节点最多 4 个 key / 5 个孩子，装不下，必然触发内部节点分裂
    std::vector<int> keys = gen_random_unique(100, 42);
    insert_all(tree, keys);
    CHECK(tree.height() >= 3);
    CHECK(tree.validate_leaf_chain());
    check_all_gettable(tree, keys);
}

static void test_mixed_random_ops() {
    // 随机插入 + 随机查询混合，对比一个 std::map 参考答案
    BPlusTree tree;
    std::map<int, Rid> reference;
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> key_dist(0, 999);
    std::uniform_int_distribution<int> op_dist(0, 9);

    for (int step = 0; step < 5000; ++step) {
        int k = key_dist(rng);
        if (op_dist(rng) < 6) {  // 60% 插入
            bool ok = tree.insert(k, rid_of(k));
            bool ref_ok = reference.emplace(k, rid_of(k)).second;
            CHECK(ok == ref_ok);
        } else {                 // 40% 查询
            auto r = tree.get(k);
            auto it = reference.find(k);
            CHECK(r.has_value() == (it != reference.end()));
            if (it != reference.end()) {
                CHECK(r.has_value() && r.value() == it->second);
            }
        }
    }
    CHECK(tree.size() == reference.size());
    CHECK(tree.validate_leaf_chain());
}

int main() {
    test_empty_tree();
    test_single_insert();
    test_sorted_insert_1000();
    test_reversed_insert_1000();
    test_random_insert_10000();
    test_duplicate_key();
    test_missing_key();
    test_leaf_split();
    test_internal_split();
    test_mixed_random_ops();

    std::printf("\n========================================\n");
    std::printf("B+ Tree Day1 Test: %d passed, %d failed\n", g_pass, g_fail);
    std::printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
