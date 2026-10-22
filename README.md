# mini\_rmdb — 可测试的迷你 RMDB 索引实验框架

面向 CSCC\_DB 备赛周训练。目标是：**实现 B+ 树基线索引，并在其上叠加轻量级自适应哈希索引，对比查询与写入性能**。

当前进度：**Day 3 完成 — 性能基准测试与对比报告（**`benchmark/`**）。结论：点查询（每个 key 被查 ≥3 次）自适应哈希快约 1.3×；写入与范围查询两者持平；阶数扫描显示 16 阶为查询甜点位（比 4 阶快 4.4×）。详见&#x20;**`benchmark/REPORT.md`**。** 追加完善：**删除升级为完整 B+ 树下界维护（借位 / 合并 / 根压缩），MAX\_KEYS 参数化，负数 key 全覆盖 + 随机对拍通过。Day4 完成缓存容量上限 + LRU 淘汰（TODO 驱动，本人补齐 3 处算法），六套测试合计 31.4 万断言全绿。**

## 目录结构



```
mini_rmdb/
├── include/
│   ├── rid.h                    # 记录地址 Rid {page_no, slot_no}
│   ├── maybe.h                  # Maybe<T>：std::optional 的兼容层（g++ 6.3 可用）
│   ├── index.h                  # 统一索引抽象 Index<Key, Value>（Day3 接入）
│   ├── b_plus_tree.h            # B+ 树声明
│   └── adaptive_hash_index.h    # 自适应哈希（热点缓存）声明
├── src/
│   ├── b_plus_tree.cpp          # B+ 树：insert / get / erase / range_query / split / validate_tree
│   └── adaptive_hash_index.cpp  # 热点缓存：晋升 / 命中 / 失效
├── tests/
│   ├── data_generator.h         # 固定种子数据生成器（顺序 / 逆序 / 随机）
│   ├── b_plus_tree_test.cpp     # Day1 单元测试
│   ├── b_plus_tree_day2_test.cpp# Day2 单元测试（range / erase / validate）
│   ├── adaptive_hash_test.cpp   # Day2 自适应哈希单元测试
│   ├── negative_key_test.cpp    # Day3 增补：负数 key 全覆盖 + 随机操作对拍
│   ├── erase_rebalance_test.cpp # Day3 增补：删除借位/合并/根压缩专项测试
│   └── adaptive_hash_lru_test.cpp# Day4 验收测试：缓存容量上限 + LRU 淘汰（TODO 驱动）
├── build/                       # 编译产物
├── benchmark/                   # Day3 性能基准
│   ├── index_benchmark.cpp      # QPC 纳秒计时，插入/点查询/范围查询对比
│   └── REPORT.md                # 实验结果与性能分析
├── CMakeLists.txt               # 三个测试 target + CTest
└── README.md
```

## 编译与运行（当前机器：g++ 6.3，无 cmake）



```
cd D:\2026_Autumn\CSCC\DB\mini_rmdb
# Day1
g++ -std=c++1z -O2 -Wall -Wextra -I include src/b_plus_tree.cpp tests/b_plus_tree_test.cpp -o build/bplus_test.exe
build\bplus_test.exe
# Day2 B+ 树
g++ -std=c++1z -O2 -Wall -Wextra -I include src/b_plus_tree.cpp tests/b_plus_tree_day2_test.cpp -o build/day2_bplus_test.exe
build\day2_bplus_test.exe
# Day2 自适应哈希
g++ -std=c++1z -O2 -Wall -Wextra -I include src/b_plus_tree.cpp src/adaptive_hash_index.cpp tests/adaptive_hash_test.cpp -o build/adaptive_hash_test.exe
build\adaptive_hash_test.exe
# Day3 负数 key 全覆盖
g++ -std=c++1z -O2 -Wall -Wextra -I include -I tests src/b_plus_tree.cpp src/adaptive_hash_index.cpp tests/negative_key_test.cpp -o build/negative_key_test.exe
build\negative_key_test.exe
# Day3 删除下界维护
g++ -std=c++1z -O2 -Wall -Wextra -I include -I tests src/b_plus_tree.cpp tests/erase_rebalance_test.cpp -o build/erase_rebalance_test.exe
build\erase_rebalance_test.exe
# Day4 自适应哈希容量 + LRU（TODO 验收测试，完成 TODO(4.1~4.3) 后应全绿）
g++ -std=c++1z -O2 -Wall -Wextra -I include -I tests src/b_plus_tree.cpp src/adaptive_hash_index.cpp tests/adaptive_hash_lru_test.cpp -o build/adaptive_hash_lru_test.exe
build\adaptive_hash_lru_test.exe
# Day3 基准
g++ -std=c++1z -O2 -Wall -Wextra -I include -I tests src/b_plus_tree.cpp src/adaptive_hash_index.cpp benchmark/index_benchmark.cpp -o build/bench.exe
build\bench.exe
```

测试结果（-O2 与 -O0 双验证通过）：



| 测试                             | 断言数   | 结果       |
| ------------------------------ | ----- | -------- |
| B+ Tree Day1                   | 30606 | 0 failed |
| B+ Tree Day2                   | 15087 | 0 failed |
| AdaptiveHash                   | 30576 | 0 failed |
| Negative Key（负数全覆盖对拍）          | 98902 | 0 failed |
| Erase Rebalance（借位 / 合并 / 根压缩） | 88894 | 0 failed |
| AH LRU（容量上限 + LRU 淘汰）        | 49437 | 0 failed |

## Day 2 验收对照（计划文档）



| 验收点                | 覆盖情况                                                             |
| ------------------ | ---------------------------------------------------------------- |
| B+ 树支持点查询和范围查询     | test\_range\_query\_basic / boundary / vs\_map                   |
| 自适应哈希点查询结果与 B+ 树一致 | test\_consistency\_10000\_queries（10000 次对拍）                     |
| 热点 Key 能够进入哈希缓存    | test\_promote\_after\_threshold（3 次后晋升）                          |
| 删除后不会返回旧值          | test\_erase\_invalidates\_cache / test\_consistency\_with\_erase |
| 随机测试 10000 次查询结果一致 | test\_consistency\_10000\_queries（80/20 热点负载）                    |

## 关键设计决策



1. **MAX\_KEYS 参数化（默认 4）**：默认小度数便于单测覆盖（插入 5 个 key 即分裂）；构造参数可调（如 `BPlusTree(16)`），用于阶数性能对比——实验表明 16 阶附近点查询最优。

2. **先插入后分裂**：节点允许临时超容，超过 MAX\_KEYS 再分裂，逻辑简单不易错。

3. **叶子分裂上提右半最小 key**，**内部节点分裂上提中间 key（分隔符，不保留在两侧）**—— 这是 B+ 树最容易写错的两处。

4. **find\_leaf 的边界**：内部节点 `keys[i]` 是 `children[i+1]` 子树的最小 key，所以 `key == keys[i]` 时应进入 `children[i+1]`。

5. **数据生成器固定种子（42）**：实验结果可重复复现。

6. **删除维护完整 B+ 树下界（借位 / 合并 / 根压缩）**：非根节点至少 MIN\_KEYS=2 个 key（内部节点至少 3 个孩子），欠载时优先向兄弟借位、借不到则合并；根节点只剩 1 个孩子时压缩（树高减一），根叶子删空时树清空。借位旋转按 "分隔键 = 右子树最小 key" 语义推导：**内部借左** = 左兄弟最大子树 S 移入本节点最前 + 旧分隔键下落为本节点新 keys \[0] + 分隔键更新为 S 的最小 key（即左兄弟旧最大 key）；**内部借右**对称。删除最小 key 后父节点分隔键仍允许滞后（划分有效性由 validate\_tree 保障）。

7. **prev\_leaf 必须取左兄弟子树的 "最右" 叶子**：链表中 leaf 的直接前驱是左兄弟子树 key 最大的叶子；曾误用 "最左" 导致删除时跳过链表中间段（已修复并有对拍测试保障）。

8. **自适应哈希 = B+ 树主索引 + unordered\_map 热点缓存**：点查询计数达到阈值 3 晋升进缓存；插入 / 删除用保守策略直接失效缓存条目；范围查询完全委托 B+ 树。

9. **校验器哨兵必须用 INT\_MIN 而非 -1**：`validate_leaf_chain` / `validate_tree` 的叶子链检查曾用 `prev = -1` 假设 key≥0，负数 key 下误报 "链不严格递增"。修复为 `std::numeric_limits<int>::lowest()`，并由 negative\_key\_test（98902 断言）保障。

10. **Day4 自适应哈希容量 + LRU（TODO 驱动，已完成）**：热点缓存加容量上限（构造参数，默认 4096），用 `unordered_map<int, CacheEntry> + std::list<int>` 经典组合实现 O(1) 的查找 / 刷新 / 淘汰。算法关键 3 处由本人补齐：TODO(4.1) 失效时链表同步、TODO(4.2) 命中 splice 刷新链表、TODO(4.3) 满员淘汰 LRU 尾。实测：热点负载保持命中 86%、性能优势不变，内存从 ~9k 条降到 ≤4096 条；均匀负载出现缓存抖动（命中 39%），有界缓存在无热点场景下换血成本 > 收益——真实系统行为，详见 REPORT 第 8 节。

## Day 3 计划（衔接）



1. 实现 `benchmark/index_benchmark.cpp`：顺序 / 随机插入（1k/10k/100k）、均匀点查询、80/20 热点点查询、范围查询，对比 B+ 树 vs 自适应哈希。

2. 指标：总耗时、平均延迟、P50/P95、哈希命中 / 回退次数、插入 / 查询吞吐。

3. 每组实验运行 5 次去首取均值，输出对比表与结论报告。

## 本实验暂不实现（报告需说明）

磁盘持久化、Buffer Pool、WAL / 恢复、并发控制、事务、变长 Key、SQL 解析。整体接口参考 RMDB 索引抽象，后续可将内存节点映射到 Page 并接入 BufferPoolManager。