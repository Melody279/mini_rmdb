#pragma once

// 统一索引抽象（参考 RMDB 竞赛的 Index 接口语义）
// - Key   : 索引键，本实验先用 int
// - Value : 记录地址，本实验先用 Rid
//
// 说明：
//   BPlusTree 第一天只实现 insert / get；
//   第二天补齐 erase / range_query 后，再让 BPlusTree
//   和 AdaptiveHashIndex 继承本接口，保证两者可互换。

#include <cstddef>
#include <vector>

#include "maybe.h"
#include "rid.h"

template <typename Key, typename Value>
class Index {
public:
    // 唯一键语义：key 已存在时返回 false
    virtual bool insert(const Key& key, const Value& value) = 0;

    virtual bool erase(const Key& key) = 0;

    // 点查询：key 不存在时返回空
    virtual Maybe<Value> get(const Key& key) = 0;

    // 范围查询：[lower, upper] 闭区间
    virtual std::vector<Value> range_query(const Key& lower, const Key& upper) = 0;

    virtual ~Index() = default;
};
