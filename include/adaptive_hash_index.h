#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "b_plus_tree.h"
#include "maybe.h"
#include "rid.h"

// 自适应哈希索引（Day 2 优化层）
//
// 结构：B+ 树主索引 + 内存热点缓存（unordered_map）
// 思路：根据访问热度自适应——某个 key 的点查询次数达到阈值后，
//       把它"晋升"进哈希缓存，后续点查询 O(1) 命中；
//       冷门 key 仍回退 B+ 树（O(log n) 树高）。
//
// 特点（与计划文档一致）：
//   - 点查询优先查缓存，miss 才回退 B+ 树，并统计命中/回退次数
//   - 插入 / 删除采用保守策略：直接失效缓存条目（不维护复杂一致性）
//   - 范围查询与删除语义完全委托给 B+ 树（哈希层只优化等值点查）
//   - 与 B+ 树对外行为完全一致（唯一键、可删除），可互换
class AdaptiveHashIndex {
public:
    // 访问次数达到该阈值后晋升进热点缓存（实验参数）
    static constexpr int PROMOTE_THRESHOLD = 3;

    // 唯一键插入；key 已存在返回 false。插入后失效对应缓存。
    bool insert(int key, Rid rid);

    // 删除；key 不存在返回 false。删除前先失效缓存。
    bool erase(int key);

    // 点查询：缓存命中直接返回；miss 回退 B+ 树并累计访问热度。
    Maybe<Rid> get(int key);

    // 范围查询：完全委托给 B+ 树（热点缓存不参与）
    std::vector<Rid> range_query(int lower, int upper) const;

    size_t size() const { return base_index_.size(); }

    // 统计：哈希缓存直接命中次数
    size_t hash_hit_count() const { return hash_hit_; }

    // 统计：哈希未命中后回退 B+ 树的次数
    size_t bplus_fallback_count() const { return bplus_fallback_; }

    // 统计：当前热点缓存条目数
    size_t cache_size() const { return hot_cache_.size(); }

private:
    BPlusTree base_index_;                            // 主索引
    std::unordered_map<int, Rid> hot_cache_;          // 热点缓存
    std::unordered_map<int, int> access_count_;       // 访问热度计数
    size_t hash_hit_ = 0;
    size_t bplus_fallback_ = 0;
};
