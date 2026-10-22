// Day3 增补测试：B+ 树删除完整下界维护（借位 / 合并 / 根压缩）
// 覆盖：
//   - 随机删 80%：与 std::map 对拍 + 定期全量结构校验（含下界检查）+ 树高不增
//   - 删除到空树：根压缩 / 树清空路径
//   - 删后重插：验证删除修复过的树仍能正常插入分裂
//   - 顺序插入深树 + 删到只剩几个 key：树高必须压回 1（单叶子）

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

// ---------- 1. 随机删 80% + 对拍 ----------

static void test_erase_random_vs_map_massive() {
    std::vector<int> keys = gen_random_unique_range(10000, -5000, 42);
    std::mt19937 rng(2026);

    BPlusTree tree;
    std::map<int, Rid> reference;
    for (int k : keys) {
        tree.insert(k, rid_of(k));
        reference[k] = rid_of(k);
    }
    const int h_before = tree.height();
    CHECK(tree_ok(tree));

    // 随机删除 8000 个不重复 key（洗牌后顺序删，保证恰好删 80%），与 map 对拍
    std::vector<int> del_keys = keys;
    std::shuffle(del_keys.begin(), del_keys.end(), rng);
    for (int step = 0; step < 8000; ++step) {
        const int k = del_keys[static_cast<std::size_t>(step)];
        const bool got = tree.erase(k);
        CHECK(got == (reference.erase(k) > 0));
        if (step % 1000 == 999) {
            CHECK(tree_ok(tree));
            CHECK(tree.validate_leaf_chain());
        }
    }

    // 剩余 2000 个 key 全量对拍 get
    CHECK(reference.size() == 2000);
    CHECK(tree.size() == reference.size());
    for (const auto& kv : reference) {
        auto res = tree.get(kv.first);
        CHECK(res.has_value());
        if (res.has_value()) {
            CHECK(res.value() == kv.second);
        }
    }
    // 被删掉的 key 查不到
    for (int i = 0; i < 500; ++i) {
        const int k = keys[rng() % keys.size()];
        if (reference.find(k) == reference.end()) {
            CHECK(!tree.get(k).has_value());
        }
    }
    // 随机范围查询对拍
    for (int i = 0; i < 200; ++i) {
        const int lo = -5000 + static_cast<int>(rng() % 8000);
        const int hi = lo + static_cast<int>(rng() % 500);
        auto got = tree.range_query(lo, hi);
        std::vector<Rid> expect;
        for (auto it = reference.lower_bound(lo); it != reference.end() && it->first <= hi; ++it) {
            expect.push_back(it->second);
        }
        CHECK(got.size() == expect.size());
        if (got.size() == expect.size()) {
            for (std::size_t i2 = 0; i2 < got.size(); ++i2) {
                CHECK(got[i2] == expect[i2]);
            }
        }
    }
    CHECK(tree_ok(tree));
    CHECK(tree.height() <= h_before);  // 删除不能增加树高
}

// ---------- 2. 删除到空树（根压缩 + 清空） ----------

static void test_erase_to_empty() {
    std::vector<int> keys = gen_random_unique_range(5000, -2500, 3);
    std::mt19937 rng(77);

    BPlusTree tree;
    std::map<int, Rid> reference;
    for (int k : keys) {
        tree.insert(k, rid_of(k));
        reference[k] = rid_of(k);
    }
    CHECK(tree_ok(tree));

    // 一直删到空（顺序随机打乱删除）
    std::shuffle(keys.begin(), keys.end(), rng);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const int k = keys[i];
        const bool got = tree.erase(k);
        CHECK(got == (reference.erase(k) > 0));
        if (i % 500 == 499) {
            CHECK(tree_ok(tree));
            CHECK(tree.validate_leaf_chain());
        }
    }
    CHECK(tree.size() == 0);
    CHECK(reference.empty());
    CHECK(tree.height() == 0);        // 空树高度 0
    CHECK(tree_ok(tree));             // 空树 validate 通过
    CHECK(!tree.get(0).has_value());  // 任意查询为空
    CHECK(tree.range_query(-1000, 1000).empty());
}

// ---------- 3. 删后重插（树保持可插入 / 可分裂） ----------

static void test_erase_then_reinsert() {
    std::vector<int> keys1 = gen_random_unique_range(5000, 0, 11);
    std::vector<int> keys2 = gen_random_unique_range(5000, 10000, 22);  // 另一批不重叠
    std::mt19937 rng(5);

    BPlusTree tree;
    std::map<int, Rid> reference;
    for (int k : keys1) {
        tree.insert(k, rid_of(k));
        reference[k] = rid_of(k);
    }
    // 随机删 3000
    for (int i = 0; i < 3000; ++i) {
        const int k = keys1[rng() % keys1.size()];
        const bool got = tree.erase(k);
        CHECK(got == (reference.erase(k) > 0));
    }
    CHECK(tree_ok(tree));
    // 再插入 5000 个新 key（触发分裂），与 map 对拍
    for (int k : keys2) {
        tree.insert(k, rid_of(k));
        reference[k] = rid_of(k);
    }
    CHECK(tree_ok(tree));
    CHECK(tree.size() == reference.size());
    for (const auto& kv : reference) {
        auto res = tree.get(kv.first);
        CHECK(res.has_value());
        if (res.has_value()) {
            CHECK(res.value() == kv.second);
        }
    }
    // 再删一半，验证删除与插入交替
    std::vector<int> all;
    for (const auto& kv : reference) {
        all.push_back(kv.first);
    }
    std::shuffle(all.begin(), all.end(), rng);
    for (std::size_t i = 0; i < all.size() / 2; ++i) {
        const int k = all[i];
        const bool got = tree.erase(k);
        CHECK(got == (reference.erase(k) > 0));
    }
    CHECK(tree_ok(tree));
    for (const auto& kv : reference) {
        auto res = tree.get(kv.first);
        CHECK(res.has_value());
        if (res.has_value()) {
            CHECK(res.value() == kv.second);
        }
    }
}

// ---------- 4. 深树删除后树高压回 1 ----------

static void test_erase_height_shrink() {
    // 顺序插入 2000 个 key：MAX_KEYS=4 时分裂出多级内部节点（高 ~6）
    std::vector<int> keys = gen_sorted(2000);
    BPlusTree tree;
    std::map<int, Rid> reference;
    for (int k : keys) {
        tree.insert(k, rid_of(k));
        reference[k] = rid_of(k);
    }
    const int h_before = tree.height();
    CHECK(h_before >= 3);  // 确保确实是深树
    CHECK(tree_ok(tree));

    // 删掉除前 3 个之外的所有 key（剩 3 个 key 必然压回单叶）
    for (int k = 3; k < 2000; ++k) {
        CHECK(tree.erase(k));
        reference.erase(k);
        if (k % 500 == 499) {
            CHECK(tree_ok(tree));
        }
    }
    CHECK(tree.height() == 1);  // 只剩 1 个叶子：根压缩生效
    CHECK(tree.size() == 3);
    for (int k = 0; k < 3; ++k) {
        auto res = tree.get(k);
        CHECK(res.has_value());
        if (res.has_value()) {
            CHECK(res.value() == rid_of(k));
        }
    }
    CHECK(!tree.get(1000).has_value());
    CHECK(tree.range_query(0, 2).size() == 3);
    CHECK(tree_ok(tree));
    CHECK(tree.height() <= h_before);
}

// ---------- 5. 不同阶数（max_keys 参数化）下的正确性 ----------

static void test_fanout_correctness() {
    const int orders[] = {6, 8, 16, 32};
    for (int o : orders) {
        std::vector<int> keys = gen_random_unique_range(5000, -2500, 9);
        std::mt19937 rng(static_cast<unsigned>(o));
        BPlusTree tree(o);
        std::map<int, Rid> reference;
        for (int k : keys) {
            tree.insert(k, rid_of(k));
            reference[k] = rid_of(k);
        }
        CHECK(tree_ok(tree));
        // 随机删 3000，与 map 对拍
        std::shuffle(keys.begin(), keys.end(), rng);
        for (int i = 0; i < 3000; ++i) {
            const int k = keys[static_cast<std::size_t>(i)];
            const bool got = tree.erase(k);
            CHECK(got == (reference.erase(k) > 0));
        }
        CHECK(tree_ok(tree));
        // 全量对拍
        CHECK(tree.size() == reference.size());
        for (const auto& kv : reference) {
            auto res = tree.get(kv.first);
            CHECK(res.has_value());
            if (res.has_value()) {
                CHECK(res.value() == kv.second);
            }
        }
        // 范围对拍
        for (int i = 0; i < 100; ++i) {
            const int lo = -2500 + static_cast<int>(rng() % 4000);
            const int hi = lo + static_cast<int>(rng() % 200);
            auto got = tree.range_query(lo, hi);
            std::vector<Rid> expect;
            for (auto it = reference.lower_bound(lo); it != reference.end() && it->first <= hi; ++it) {
                expect.push_back(it->second);
            }
            CHECK(got.size() == expect.size());
        }
        CHECK(tree_ok(tree));
    }
}

// ---------- main ----------

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Erase Rebalance Test:\n");
    test_erase_random_vs_map_massive();
    test_erase_to_empty();
    test_erase_then_reinsert();
    test_erase_height_shrink();
    test_fanout_correctness();
    std::printf("  %d passed, %d failed\n", g_pass, g_fail);
    std::printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
