// Day4 增补测试：自适应哈希缓存容量上限 + LRU 淘汰
//
// 说明：这是"TODO 补齐"模式的验收测试——
//   当前 adaptive_hash_index.cpp 中 TODO(4.1)/(4.2)/(4.3) 尚未完成，
//   本文件部分用例会失败（预期）；完成 TODO 后应全绿。
//   运行：build\adaptive_hash_lru_test.exe

#include <cstdio>
#include <map>
#include <random>
#include <vector>

#include "adaptive_hash_index.h"
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

// 让一批 key 各自查询 3 次，触发晋升进入缓存
static void promote_keys(AdaptiveHashIndex& ah, const std::vector<int>& keys,
                         int times = 3) {
    for (int t = 0; t < times; ++t) {
        for (int k : keys) {
            ah.get(k);
        }
    }
}

// ---------- 1. 容量上限生效 ----------

static void test_capacity_limit() {
    const size_t CAP = 64;
    AdaptiveHashIndex ah(CAP);
    std::vector<int> keys = gen_random_unique(2000, 42);
    for (int k : keys) {
        ah.insert(k, rid_of(k));
    }
    // 全部 key 查询 4 次（>阈值），都会尝试晋升
    promote_keys(ah, keys, 4);
    // 缓存条数绝不能超过容量上限
    CHECK(ah.cache_size() <= CAP);
    CHECK(ah.lru_list_size() == ah.cache_size());  // 链表与 map 必须同步
    // 查询结果不受缓存限制影响
    for (int k : keys) {
        auto res = ah.get(k);
        CHECK(res.has_value());
        if (res.has_value()) {
            CHECK(res.value() == rid_of(k));
        }
    }
}

// ---------- 2. LRU 淘汰顺序 ----------

static void test_lru_eviction_order() {
    const size_t CAP = 3;
    AdaptiveHashIndex ah(CAP);
    std::vector<int> keys = gen_sorted(10);  // [0..9]
    for (int k : keys) {
        ah.insert(k, rid_of(k));
    }

    // 晋升顺序 A=0, B=1, C=2 → 缓存满（容量 3），链表 [2,1,0]（头最新）
    promote_keys(ah, {0, 1, 2}, 3);
    CHECK(ah.cache_size() == 3);

    // 再查 B=1 一次：B 变最新，链表 [1,2,0]
    ah.get(1);

    // 晋升 D=3：容量已满 → 应淘汰最久未用的 0
    promote_keys(ah, {3}, 3);
    CHECK(ah.cache_size() == 3);

    // 验证 0 已被淘汰：再查 0 应走 B+ 树回退（fallback 计数增加），而非缓存命中
    const size_t fb_before = ah.bplus_fallback_count();
    const size_t hit_before = ah.hash_hit_count();
    auto res0 = ah.get(0);
    CHECK(res0.has_value());  // 结果仍正确（B+ 树兜底）
    CHECK(ah.bplus_fallback_count() > fb_before);   // 走了回退
    CHECK(ah.hash_hit_count() == hit_before);       // 没有命中缓存

    // 缓存中的 1/2/3 命中率应保持（查 1 命中）
    const size_t hit_before2 = ah.hash_hit_count();
    auto res1 = ah.get(1);
    CHECK(res1.has_value());
    CHECK(res1.value() == rid_of(1));
    CHECK(ah.hash_hit_count() > hit_before2);
}

// ---------- 3. 失效路径同步维护 LRU ----------

static void test_erase_invalidates_lru() {
    const size_t CAP = 3;
    AdaptiveHashIndex ah(CAP);
    for (int k = 0; k < 10; ++k) {
        ah.insert(k, rid_of(k));
    }
    promote_keys(ah, {0, 1, 2}, 3);  // 缓存满
    CHECK(ah.cache_size() == 3);

    // 删除缓存中的 key=1：缓存条目必须移除，链表同步
    CHECK(ah.erase(1));
    CHECK(ah.cache_size() == 2);
    CHECK(ah.lru_list_size() == 2);

    // 删除后查 1 回退 B+ 树且结果为空
    CHECK(!ah.get(1).has_value());

    // 再次晋升新 key 3：缓存还剩 2 个条目，不会误删（无悬垂迭代器）
    promote_keys(ah, {3}, 3);
    CHECK(ah.cache_size() == 3);
    // 0/2/3 查询均正确（若 remove_from_lru 未实现，erase 后链表残留
    // 会导致 evict 时操作已删除的节点 → 崩溃或错乱）
    CHECK(ah.get(0).has_value());
    CHECK(ah.get(2).has_value());
    CHECK(ah.get(3).has_value());
}

// ---------- 4. 小容量下的随机对拍（淘汰不破坏正确性） ----------

static void test_consistency_small_capacity() {
    const size_t CAP = 32;
    AdaptiveHashIndex ah(CAP);
    std::vector<int> keys = gen_random_unique_range(2000, -1000, 7);
    std::mt19937 rng(1234);
    std::map<int, Rid> reference;

    for (int k : keys) {
        ah.insert(k, rid_of(k));
        reference[k] = rid_of(k);
    }
    // 随机查询热点（集中在部分 key 上，触发晋升 + 淘汰换血）
    for (int i = 0; i < 20000; ++i) {
        const int k = keys[rng() % keys.size()];
        auto res = ah.get(k);
        auto it = reference.find(k);
        CHECK(res.has_value() == (it != reference.end()));
        if (res.has_value() && it != reference.end()) {
            CHECK(res.value() == it->second);
        }
    }
    CHECK(ah.cache_size() <= CAP);
    CHECK(ah.lru_list_size() == ah.cache_size());

    // 随机删除 + 查询对拍（删除后的 key 不得再被查到）
    for (int i = 0; i < 3000; ++i) {
        const int k = keys[rng() % keys.size()];
        if (i % 3 == 0) {
            const bool got = ah.erase(k);
            CHECK(got == (reference.erase(k) > 0));
        } else {
            auto res = ah.get(k);
            auto it = reference.find(k);
            CHECK(res.has_value() == (it != reference.end()));
        }
    }
    CHECK(ah.cache_size() <= CAP);
    CHECK(ah.lru_list_size() == ah.cache_size());
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
    std::printf("AdaptiveHash LRU Test:\n");
    test_capacity_limit();
    test_lru_eviction_order();
    test_erase_invalidates_lru();
    test_consistency_small_capacity();
    std::printf("  %d passed, %d failed\n", g_pass, g_fail);
    std::printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
