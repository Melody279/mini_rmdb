// 在线参数评估：接受 (cap, thr, skew, write_pct, hot_set) 参数，真跑一次负载，输出命中率/耗时/缓存条数
// 用法：eval_point.exe <cap> <thr> <skew> <write_pct> <hot_set>
//   skew:      查询中热点集占比（%）
//   write_pct: 操作中写入占比（%），写入 key 从 10000 递增
//   hot_set:   热点集大小（前 N 个 key 为热点，其余 [N,10000) 为冷）；TPC-C 形态：800 = 4/50 仓库
// 输出单行：<hit_rate> <ms> <cache_size>
// 供 Python 贝叶斯优化做"在线评估"（每次选点后真跑，模拟提交-看结果-校准）
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <windows.h>

#include "adaptive_hash_index.h"
#include "b_plus_tree.h"
#include "data_generator.h"

static double g_qpc_freq = 0.0;
static double now_sec() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / g_qpc_freq;
}
static double ms_since(double t0) { return (now_sec() - t0) * 1000.0; }
static Rid rid_of(int i) { return Rid{i / 10, i % 10}; }

static const int WRITE_TAG = 20000;

// 参数化负载：skew% 的查询落在热点集 [0,hot_set)，其余 [hot_set,10000)；write_pct% 的操作是插入
static std::vector<int> gen_ops(int n, unsigned seed, int skew, int write_pct, int hot_set) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> hot(0, hot_set - 1);
    std::uniform_int_distribution<int> cold(hot_set, 9999);
    std::uniform_int_distribution<int> qside(0, 99);  // 热点 vs 冷查询
    std::uniform_int_distribution<int> wside(0, 99);  // 读 vs 写
    std::vector<int> ops;
    ops.reserve(n);
    int next = 10000;
    for (int i = 0; i < n; ++i) {
        if (wside(rng) < write_pct) {
            ops.push_back(WRITE_TAG + (++next));
        } else {
            ops.push_back((qside(rng) < skew) ? hot(rng) : cold(rng));
        }
    }
    return ops;
}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::printf("usage: eval_point.exe <cap> <thr> <skew> <write_pct> <hot_set>\n");
        return 1;
    }
    int cap = std::atoi(argv[1]);
    int thr = std::atoi(argv[2]);
    int skew = std::atoi(argv[3]);
    int write_pct = std::atoi(argv[4]);
    int hot_set = std::atoi(argv[5]);
    if (cap < 16) cap = 16;
    if (thr < 1) thr = 1;
    if (skew < 1) skew = 1;
    if (skew > 99) skew = 99;
    if (write_pct < 0) write_pct = 0;
    if (write_pct > 90) write_pct = 90;
    if (hot_set < 16) hot_set = 16;
    if (hot_set > 9999) hot_set = 9999;

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpc_freq = (double)freq.QuadPart;

    std::vector<int> ops = gen_ops(100000, 7, skew, write_pct, hot_set);

    std::vector<int> keys = gen_random_unique(10000, 42);
    AdaptiveHashIndex idx((size_t)cap, thr);
    for (int k : keys) idx.insert(k, rid_of(k));

    size_t hit0 = idx.hash_hit_count();
    size_t fb0 = idx.bplus_fallback_count();
    double t0 = now_sec();
    for (int v : ops) {
        if (v >= WRITE_TAG) {
            idx.insert(v - WRITE_TAG, rid_of(v));
        } else {
            idx.get(v);
        }
    }
    double ms = ms_since(t0);
    size_t hits = idx.hash_hit_count() - hit0;
    size_t fb = idx.bplus_fallback_count() - fb0;
    double hit_rate = (hits + fb) > 0 ? (double)hits / (double)(hits + fb) : 0.0;

    std::printf("%.4f %.2f %u\n", hit_rate, ms, (unsigned)idx.cache_size());
    return 0;
}
