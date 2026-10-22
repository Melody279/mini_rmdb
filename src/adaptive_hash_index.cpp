#include "adaptive_hash_index.h"

bool AdaptiveHashIndex::insert(int key, Rid rid) {
    const bool ok = base_index_.insert(key, rid);
    if (ok) {
        // 保守策略：数据变化即失效缓存，下次查询重新按热度晋升
        hot_cache_.erase(key);
        access_count_.erase(key);
    }
    return ok;
}

bool AdaptiveHashIndex::erase(int key) {
    hot_cache_.erase(key);
    access_count_.erase(key);
    return base_index_.erase(key);
}

Maybe<Rid> AdaptiveHashIndex::get(int key) {
    // 1. 缓存命中：O(1) 返回
    auto it = hot_cache_.find(key);
    if (it != hot_cache_.end()) {
        ++hash_hit_;
        return Maybe<Rid>(it->second);
    }

    // 2. 缓存未命中：回退 B+ 树
    ++bplus_fallback_;
    auto result = base_index_.get(key);

    // 3. 自适应晋升：只统计查得到的 key，达到阈值后进入热点缓存
    if (result.has_value()) {
        int& cnt = access_count_[key];
        ++cnt;
        if (cnt >= PROMOTE_THRESHOLD) {
            hot_cache_[key] = result.value();
        }
    }
    return result;
}

std::vector<Rid> AdaptiveHashIndex::range_query(int lower, int upper) const {
    return base_index_.range_query(lower, upper);
}
