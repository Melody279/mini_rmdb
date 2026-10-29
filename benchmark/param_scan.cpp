// Week2 参数扫描：缓存容量 × 晋升阈值 × 负载类型
// 目的：回答老师建议的第一项——"缓存热点数 / 缓存大小限制怎么设"，
//       给出参数敏感性数据（命中率 / 总耗时 / 缓存条数），为后续参数自动寻优（贝叶斯）提供基线。
// 控制变量：同一 10k 数据集（种子 42）、同一 100k 操作序列、只变 (容量, 阈值)。
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

#include <windows.h>

#include "adaptive_hash_index.h"
#include "b_plus_tree.h"
#include "data_generator.h"

// 读写混合负载中，>= WRITE_TAG 的值表示"插入 (v - WRITE_TAG)"
static const int WRITE_TAG = 20000;

static double g_qpc_freq = 0.0;
static double now_sec() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / g_qpc_freq;
}
static double ms_since(double t0) { return (now_sec() - t0) * 1000.0; }
static Rid rid_of(int i) { return Rid{i / 10, i % 10}; }

// 热点 80/20：80% 落 [0,2000)，20% 落 [2000,10000)
static std::vector<int> gen_hot(int n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> hot(0, 1999);
    std::uniform_int_distribution<int> cold(2000, 9999);
    std::uniform_int_distribution<int> side(0, 9);
    std::vector<int> q;
    q.reserve(n);
    for (int i = 0; i < n; ++i) q.push_back((side(rng) < 8) ? hot(rng) : cold(rng));
    return q;
}

// 均匀：0-9999 等概率
static std::vector<int> gen_uniform(int n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> uni(0, 9999);
    std::vector<int> q;
    q.reserve(n);
    for (int i = 0; i < n; ++i) q.push_back(uni(rng));
    return q;
}

// 读写混合：80% 查询（内部仍保持 80/20 热点分布）+ 20% 插入新 key（10000 起递增）
static std::vector<int> gen_rw(int n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> hot(0, 1999);
    std::uniform_int_distribution<int> cold(2000, 9999);
    std::uniform_int_distribution<int> side(0, 9);
    std::vector<int> ops;
    ops.reserve(n);
    int next_new = 10000;
    for (int i = 0; i < n; ++i) {
        if (side(rng) < 8) {
            ops.push_back((side(rng) < 8) ? hot(rng) : cold(rng));  // 读（80/20 热点）
        } else {
            ops.push_back(WRITE_TAG + (++next_new));                // 写：插入新 key
        }
    }
    return ops;
}

struct Result {
    double ms = 0;         // 去首取平均总耗时（含读+写）
    double hit_rate = 0;   // 读命中率
    size_t cache = 0;      // 最终缓存条数
};

static Result run_one(size_t cap, int thr, const std::vector<int>& ops, int repeat) {
    double sum_ms = 0;
    double hit_sum = 0, read_sum = 0;
    size_t cache_last = 0;
    for (int r = 0; r < repeat; ++r) {
        // 建树（不计时）：与 benchmark 一致，按 keys 顺序插入 10k
        std::vector<int> keys = gen_random_unique(10000, 42);
        AdaptiveHashIndex idx(cap, thr);
        for (int k : keys) idx.insert(k, rid_of(k));

        size_t hit_before = idx.hash_hit_count();
        size_t fb_before = idx.bplus_fallback_count();

        double t0 = now_sec();
        for (int v : ops) {
            if (v >= WRITE_TAG) {
                idx.insert(v - WRITE_TAG, rid_of(v));
            } else {
                idx.get(v);
            }
        }
        double d = ms_since(t0);
        if (r > 0) sum_ms += d;
        // 真实缓存命中率：用索引内部计数（查询要么命中要么回退 B+ 树）
        hit_sum += (double)(idx.hash_hit_count() - hit_before);
        read_sum += (double)(idx.hash_hit_count() - hit_before) + (double)(idx.bplus_fallback_count() - fb_before);
        cache_last = idx.cache_size();
    }
    Result res;
    res.ms = sum_ms / (double)(repeat - 1);
    res.hit_rate = read_sum > 0 ? hit_sum / read_sum : 0.0;
    res.cache = cache_last;
    return res;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpc_freq = (double)freq.QuadPart;

    const int REPEAT = 5;  // 5 次去首取 4 次平均（扫描组合多，控制总时长）
    const std::vector<int> caps = {512, 2048, 4096, 8192, 16384};
    const std::vector<int> thrs = {1, 2, 3, 5, 10};
    const char* load_names[3] = {"uniform", "hot8020", "rw_80r20w"};
    const std::vector<int> loads[3] = {
        gen_uniform(100000, 7),
        gen_hot(100000, 99),
        gen_rw(100000, 7)
    };

    FILE* out = std::fopen("benchmark/param_scan.csv", "w");
    if (out) {
        std::fprintf(out, "load,capacity,threshold,read_hit_rate,ms_avg,cache_size\n");
    }

    std::printf("%-10s %8s %10s %12s %10s %10s\n",
                "load", "capacity", "threshold", "hit_rate", "ms", "cache");
    for (int li = 0; li < 3; ++li) {
        for (size_t c : caps) {
            for (int t : thrs) {
                Result r = run_one(c, t, loads[li], REPEAT);
                std::printf("%-10s %8u %10d %11.1f%% %9.2f %9u\n",
                            load_names[li], (unsigned)c, t, r.hit_rate * 100.0, r.ms, (unsigned)r.cache);
                if (out) {
                    std::fprintf(out, "%s,%u,%d,%.4f,%.2f,%u\n",
                                 load_names[li], (unsigned)c, t, r.hit_rate, r.ms, (unsigned)r.cache);
                }
            }
        }
    }
    if (out) std::fclose(out);

    std::printf("\nCSV saved: benchmark/param_scan.csv\n");
    std::printf("Note: uniform/hot = 100k reads; rw = 80k reads + 20k inserts. Tree built with 10k keys (seed 42).\n");
    return 0;
}
