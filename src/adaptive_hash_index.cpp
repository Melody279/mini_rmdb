#include "adaptive_hash_index.h"

bool AdaptiveHashIndex::insert(int key, Rid rid) {
    const bool ok = base_index_.insert(key, rid);
    if (ok) {
        // 保守策略：数据变化即失效缓存，下次查询重新按热度晋升
        remove_from_lru(key);   // TODO(4.1)：链表同步（未实现时仅残留链表节点）
        hot_cache_.erase(key);  // map 失效（工程细节，已实现）
        access_count_.erase(key);
    }
    return ok;
}

bool AdaptiveHashIndex::erase(int key) {
    remove_from_lru(key);   // TODO(4.1)：链表同步
    hot_cache_.erase(key);  // map 失效
    access_count_.erase(key);
    return base_index_.erase(key);
}

Maybe<Rid> AdaptiveHashIndex::get(int key) {
    // 1. 缓存命中：O(1) 返回
    auto it = hot_cache_.find(key);
    if (it != hot_cache_.end()) {
        ++hash_hit_;
        // TODO(4.2) 刷新 LRU：把命中 key 标记为"最近使用"
        //   提示：lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_it);
        //   std::list::splice 把链表节点整体搬移到头部，迭代器不失效，
        //   因此 map 里存的 lru_it 无需更新。
        //   注意：不改这一行，查询结果仍正确（只是淘汰顺序退化），
        //   但 LRU 语义测试 test_lru_eviction_order 会失败

        lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_it);

        return Maybe<Rid>(it->second.rid);
    }

    // 2. 缓存未命中：回退 B+ 树
    ++bplus_fallback_;
    auto result = base_index_.get(key);

    // 3. 自适应晋升：只统计查得到的 key，达到阈值后进入热点缓存
    if (result.has_value()) {
        int& cnt = access_count_[key];
        ++cnt;
        if (cnt >= PROMOTE_THRESHOLD) {
            // TODO(4.3) 容量检查 + LRU 淘汰（算法关键，你来填）：
            //   晋升前检查缓存是否已满，满了先淘汰最久未用条目：
            //     if (hot_cache_.size() >= max_capacity_) evict_lru();
            //   先淘汰再插入，保证 hot_cache_.size() <= max_capacity_。
            //   思考：被淘汰的 key 下次查询会回退 B+ 树并重新计数，
            //   这是"热点漂移"时缓存自动换血的设计基础。

            if (hot_cache_.size() >= max_capacity_) {
                evict_lru();
            }
            auto it = lru_list_.insert(lru_list_.begin(), key);  // 默认最新
            hot_cache_[key] = CacheEntry{result.value(), it};
        }
    }
    return result;
}

std::vector<Rid> AdaptiveHashIndex::range_query(int lower, int upper) const {
    return base_index_.range_query(lower, upper);
}

// TODO(4.1)：把 key 从 LRU 链表中移除（key 不在缓存时无操作）。
//   链表与 map 的删除分离：map 由调用方（insert/erase）负责，
//   这里只用 map 判断 key 是否在缓存，再同步删链表节点：
//     auto it = hot_cache_.find(key);
//     if (it == hot_cache_.end()) return;
//     lru_list_.erase(it->second.lru_it);
//   注意：必须先 find 再 erase 链表（迭代器来自 map 条目，map 删了就悬垂）。
void AdaptiveHashIndex::remove_from_lru(int key) {
    auto it = hot_cache_.find(key);
    if (it == hot_cache_.end()) {
        return;
    }

    lru_list_.erase(it->second.lru_it);  // TODO(4.1)
}

// TODO(4.3)：缓存已满时淘汰最久未访问的条目，返回被淘汰的 key
//   提示：
//     int victim = lru_list_.back();   // 链表尾部 = 最久未用
//     lru_list_.pop_back();
//     hot_cache_.erase(victim);        // 链表与 map 同步删除
//     return victim;
int AdaptiveHashIndex::evict_lru() {
    int victim = lru_list_.back();
    lru_list_.pop_back();
    hot_cache_.erase(victim);
    return victim; // TODO(4.3)
}
