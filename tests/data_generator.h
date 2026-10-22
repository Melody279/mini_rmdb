#pragma once

// 固定种子的测试数据生成器：保证实验结果可重复
// 三种分布：顺序 / 逆序 / 随机不重复（随机用固定种子 42）

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

inline std::vector<int> gen_sorted(int n) {
    std::vector<int> v(static_cast<std::size_t>(n));
    std::iota(v.begin(), v.end(), 0);
    return v;
}

inline std::vector<int> gen_reversed(int n) {
    std::vector<int> v(static_cast<std::size_t>(n));
    // iota 从反向迭代器写入：v[0] = n-1, ..., v[n-1] = 0
    std::iota(v.rbegin(), v.rend(), 0);
    return v;
}

// 生成 [0, n) 的一个随机排列（不重复）
inline std::vector<int> gen_random_unique(int n, unsigned seed = 42) {
    std::vector<int> v(static_cast<std::size_t>(n));
    std::iota(v.begin(), v.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}

// 生成 [lo, lo+n) 的一个随机排列（不重复）；lo 可为负，覆盖负数 key 路径
inline std::vector<int> gen_random_unique_range(int n, int lo, unsigned seed = 42) {
    std::vector<int> v(static_cast<std::size_t>(n));
    std::iota(v.begin(), v.end(), lo);
    std::mt19937 rng(seed);
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}

// 生成一个随机查询序列，元素取自 keys（允许重复，模拟真实查询负载）
inline std::vector<int> gen_query_workload(const std::vector<int>& keys,
                                           std::size_t num_queries,
                                           unsigned seed = 7) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, keys.size() - 1);
    std::vector<int> q;
    q.reserve(num_queries);
    for (std::size_t i = 0; i < num_queries; ++i) {
        q.push_back(keys[pick(rng)]);
    }
    return q;
}
