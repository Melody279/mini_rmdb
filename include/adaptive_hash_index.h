#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>
#include <vector>

#include "b_plus_tree.h"
#include "maybe.h"
#include "rid.h"

// 自适应哈希索引（Day 2 优化层，Day 4 加入容量限制 + LRU 淘汰）
//
// 结构：B+ 树主索引 + 内存热点缓存（unordered_map + LRU 链表）
// 思路：根据访问热度自适应——某个 key 的点查询次数达到阈值后，
//       把它"晋升"进哈希缓存，后续点查询 O(1) 命中；
//       冷门 key 仍回退 B+ 树（O(log n) 树高）。
//
// ================= Day4 TODO：缓存容量上限 + LRU 淘汰 =================
// 问题：当前 hot_cache_ 无上限，热点 key 全部晋升后缓存≈全量 key，
//       内存失控，且失去"只缓存热点"的聚焦意义。
// 目标：缓存条数不超过 max_capacity_，满了按 LRU（最久未访问）淘汰。
//
// 数据结构的经典教科书组合（O(1) 插入/查找/淘汰）：
//   - hot_cache_：unordered_map<int, CacheEntry>，值里存"LRU 链表中的迭代器"
//   - lru_list_ ：std::list<int>，按最近访问从新到旧排序（头部最新、尾部最旧）
//   - 命中刷新：把链表节点 splice 到头部，迭代器不失效，无需改 map
//   - 满员淘汰：取链表尾部 key（最久未用），从 map 和链表中一起删除
//
// 你需要补的算法点（共 3 处，均在 adaptive_hash_index.cpp，提示见 TODO）：
//   TODO(4.1) remove_from_lru()：失效时同步移除链表条目，防止悬垂迭代器
//   TODO(4.2) get() 命中分支  ：把命中 key 刷新到链表头部（LRU 语义）
//   TODO(4.3) get() 晋升分支  ：容量检查 + 满员淘汰 + 新条目入链表
//
// 其余工程细节（结构声明 / 构造函数 / 统计 / 测试 / 文档）已由助手完成。
// 完成 TODO 后运行 build/adaptive_hash_lru_test.exe，应全绿。
// ====================================================================
class AdaptiveHashIndex {
public:
    // 缓存容量上限：构造参数，默认 4096（约 10k 数据前 40% 热点）
    // 晋升阈值：访问次数达到该值后晋升进热点缓存（实验参数，默认 3）
    explicit AdaptiveHashIndex(size_t max_capacity = 4096, int promote_threshold = 3)
        : base_index_(), max_capacity_(max_capacity), promote_threshold_(promote_threshold) {}

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

    // 统计：当前热点缓存条目数（受 max_capacity_ 约束）
    size_t cache_size() const { return hot_cache_.size(); }

    // 统计：LRU 链表长度（调试用，正常应恒等于 hot_cache_.size()）
    size_t lru_list_size() const { return lru_list_.size(); }

private:
    // 缓存条目：值 + 在 LRU 链表中的位置（迭代器），实现 O(1) 移动/删除
    struct CacheEntry {
        Rid rid;
        std::list<int>::iterator lru_it;
    };

    // TODO(4.x) 需要操作的辅助结构：见文件头注释

    BPlusTree base_index_;                              // 主索引
    std::unordered_map<int, CacheEntry> hot_cache_;     // 热点缓存（含 LRU 位置）
    std::unordered_map<int, int> access_count_;         // 访问热度计数
    std::list<int> lru_list_;                           // 头=最近使用，尾=最久未用
    size_t max_capacity_;                               // 缓存容量上限
    int promote_threshold_;                             // 晋升阈值（访问多少次后进缓存）

    size_t hash_hit_ = 0;
    size_t bplus_fallback_ = 0;

    // TODO(4.1)：从 LRU 链表与缓存中移除 key（key 不在缓存时无操作）。
    //            被 insert / erase 失效路径调用，防止残留悬垂迭代器。
    void remove_from_lru(int key);

    // TODO(4.3)：缓存已满时淘汰最久未访问的条目（链表尾部），返回被淘汰的 key
    int evict_lru();
};
