# mini_rmdb — 可测试的迷你 RMDB 索引实验框架

面向 CSCC_DB 备赛周训练。目标是：**实现 B+ 树基线索引，并在其上叠加轻量级自适应哈希索引，对比查询与写入性能**。

当前进度：**Day 3 完成 — 性能基准测试与对比报告（`benchmark/`）。结论：点查询（每个 key 被查 ≥3 次）自适应哈希快约 1.3×；写入与范围查询两者持平。详见 `benchmark/REPORT.md`。**

## 目录结构

```text
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
│   └── adaptive_hash_test.cpp   # Day2 自适应哈希单元测试
├── build/                       # 编译产物
├── benchmark/                   # Day3 性能基准
│   ├── index_benchmark.cpp      # QPC 纳秒计时，插入/点查询/范围查询对比
│   └── REPORT.md                # 实验结果与性能分析
├── CMakeLists.txt               # 三个测试 target + CTest
└── README.md
```

## 编译与运行（当前机器：g++ 6.3，无 cmake）

```powershell
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
# Day3 基准
g++ -std=c++1z -O2 -Wall -Wextra -I include -I tests src/b_plus_tree.cpp src/adaptive_hash_index.cpp benchmark/index_benchmark.cpp -o build/bench.exe
build\bench.exe
```

测试结果（-O2 与 -O0 双验证通过）：

| 测试 | 断言数 | 结果 |
|---|---:|---|
| B+ Tree Day1 | 30606 | 0 failed |
| B+ Tree Day2 | 15087 | 0 failed |
| AdaptiveHash | 30576 | 0 failed |

## Day 2 验收对照（计划文档）

| 验收点 | 覆盖情况 |
|---|---|
| B+ 树支持点查询和范围查询 | test_range_query_basic / boundary / vs_map |
| 自适应哈希点查询结果与 B+ 树一致 | test_consistency_10000_queries（10000 次对拍） |
| 热点 Key 能够进入哈希缓存 | test_promote_after_threshold（3 次后晋升） |
| 删除后不会返回旧值 | test_erase_invalidates_cache / test_consistency_with_erase |
| 随机测试 10000 次查询结果一致 | test_consistency_10000_queries（80/20 热点负载） |

## 关键设计决策

1. **MAX_KEYS = 4（小度数）**：插入第 5 个 key 即触发叶子分裂，方便单测覆盖；性能测试时可调大。
2. **先插入后分裂**：节点允许临时超容，超过 MAX_KEYS 再分裂，逻辑简单不易错。
3. **叶子分裂上提右半最小 key**，**内部节点分裂上提中间 key（分隔符，不保留在两侧）**——这是 B+ 树最容易写错的两处。
4. **find_leaf 的边界**：内部节点 `keys[i]` 是 `children[i+1]` 子树的最小 key，所以 `key == keys[i]` 时应进入 `children[i+1]`。
5. **数据生成器固定种子（42）**：实验结果可重复复现。
6. **删除采用实验版简化**：不做借位 / 合并 / 上提，允许内部节点欠载（可只剩 1 个孩子），只处理"叶子删空则摘除 + 级联摘除空内部节点"，因此所有叶子保持等深、find_leaf 与范围查询始终正确。删除最小 key 后父节点分隔键允许滞后（仍满足划分有效性）。完整下界维护列为后续工作。
7. **prev_leaf 必须取左兄弟子树的"最右"叶子**：链表中 leaf 的直接前驱是左兄弟子树 key 最大的叶子；曾误用"最左"导致删除时跳过链表中间段（已修复并有对拍测试保障）。
8. **自适应哈希 = B+ 树主索引 + unordered_map 热点缓存**：点查询计数达到阈值 3 晋升进缓存；插入 / 删除用保守策略直接失效缓存条目；范围查询完全委托 B+ 树。

## Day 3 计划（衔接）

1. 实现 `benchmark/index_benchmark.cpp`：顺序 / 随机插入（1k/10k/100k）、均匀点查询、80/20 热点点查询、范围查询，对比 B+ 树 vs 自适应哈希。
2. 指标：总耗时、平均延迟、P50/P95、哈希命中 / 回退次数、插入 / 查询吞吐。
3. 每组实验运行 5 次去首取均值，输出对比表与结论报告。

## 本实验暂不实现（报告需说明）

磁盘持久化、Buffer Pool、WAL/恢复、并发控制、事务、变长 Key、SQL 解析。整体接口参考 RMDB 索引抽象，后续可将内存节点映射到 Page 并接入 BufferPoolManager。
