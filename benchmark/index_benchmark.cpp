// Day3 性能基准：B+ Tree vs AdaptiveHashIndex
// 场景：顺序/随机插入、均匀点查询、80/20热点点查询、范围查询
// 指标：总耗时、平均延迟、P50、P95、吞吐、哈希命中/回退
// 每组 repeat 次，去首取平均
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include <windows.h>

#include "adaptive_hash_index.h"
#include "b_plus_tree.h"
#include "data_generator.h"

// Windows QPC 计时（MinGW 6.3 的 steady_clock 分辨率仅 ~15ms，无法测单次操作）
static double g_qpc_freq = 0.0;
static double now_sec() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / g_qpc_freq;
}
static double ms_since(double t0) { return (now_sec() - t0) * 1000.0; }
static double ns_since(double t0) { return (now_sec() - t0) * 1e9; }
static Rid rid_of(int i) { return Rid{i / 10, i % 10}; }

// 热点查询负载：80% 落 [0,2000)，20% 落 [2000,10000)（前 20% 为热点）
static std::vector<int> gen_hot_workload(int n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> hot(0, 1999);
    std::uniform_int_distribution<int> cold(2000, 9999);
    std::uniform_int_distribution<int> side(0, 9);
    std::vector<int> q;
    q.reserve(n);
    for (int i = 0; i < n; ++i) {
        q.push_back((side(rng) < 8) ? hot(rng) : cold(rng));
    }
    return q;
}

struct Stat {
    double total_ms = 0;   // 累计耗时
    double ops = 0;        // 累计操作数
    std::vector<double> lat;  // 全部延迟(us)
    double p50 = 0, p95 = 0;
    size_t hit = 0, fb = 0;  // AdaptiveHash 统计

    void finalize() {
        if (lat.empty()) return;
        std::sort(lat.begin(), lat.end());
        size_t n = lat.size();
        p50 = lat[n / 2];
        p95 = lat[static_cast<size_t>(n * 0.95)];
    }
    double avg_ms() const { return total_ms / (ops == 0 ? 1 : 1); }  // 由调用处处理
};

static void print_row(const char* name, const Stat& s, double ms) {
    std::printf("  %-22s %8.2f ms  avg %6.2f ns  P50 %5.2f ns  P95 %6.2f ns  "
                "%7.0f ops/s%s\n",
                name, ms, ms * 1000000.0 / s.ops, s.p50, s.p95, s.ops / (ms / 1000.0),
                "");
}

// ---- 插入基准 ----
static void bench_insert(const char* label, const std::vector<int>& keys, int repeat) {
    std::printf("\n[Insert %s  n=%u]\n", label, (unsigned)keys.size());
    {
        Stat st; st.ops = keys.size();
        for (int r = 0; r < repeat; ++r) {
            BPlusTree tree;
            double t0 = now_sec();
            for (int k : keys) tree.insert(k, rid_of(k));
            double dt = ms_since(t0);
            if (r > 0) st.total_ms += dt;  // 去首
            st.lat.push_back(dt * 1000.0);
        }
        st.lat.clear();  // 插入场景不统计延迟分位，用总耗时
        double ms = st.total_ms / (repeat - 1);
        Stat dummy; dummy.ops = keys.size() * (repeat - 1);
        std::printf("  %-22s %8.2f ms  %7.0f inserts/s\n", "B+ Tree", ms,
                    keys.size() * (repeat - 1) / (ms / 1000.0));
    }
    {
        Stat st; st.ops = keys.size();
        size_t hit = 0, fb = 0;
        for (int r = 0; r < repeat; ++r) {
            AdaptiveHashIndex ah;
            double t0 = now_sec();
            for (int k : keys) ah.insert(k, rid_of(k));
            double dt = ms_since(t0);
            if (r > 0) st.total_ms += dt;
            hit += ah.hash_hit_count(); fb += ah.bplus_fallback_count();
        }
        double ms = st.total_ms / (repeat - 1);
        std::printf("  %-22s %8.2f ms  %7.0f inserts/s  (hit=%u fb=%u)\n",
                    "AdaptiveHash", ms, keys.size() * (repeat - 1) / (ms / 1000.0),
                    (unsigned)(hit / repeat), (unsigned)(fb / repeat));
    }
}

// ---- 点查询基准 ----
static void bench_point(const char* label, const std::vector<int>& queries, int repeat) {
    std::printf("\n[Point Lookup %s  n=%u queries]\n", label,
                (unsigned)queries.size());
    {
        Stat st; st.ops = queries.size() * (repeat - 1);
        for (int r = 0; r < repeat; ++r) {
            BPlusTree tree;
            std::vector<int> keys = gen_random_unique(10000, 42);
            for (int k : keys) tree.insert(k, rid_of(k));
            volatile long long checksum = 0;
            std::vector<double> lat;
            double t0 = now_sec();
            for (int q : queries) {
                double t1 = now_sec();
                auto res = tree.get(q);
                lat.push_back(ns_since(t1));
                if (res.has_value()) checksum += res.value().page_no;
            }
            double dt = ms_since(t0);
            if (r > 0) {
                st.total_ms += dt;
                st.lat.insert(st.lat.end(), lat.begin(), lat.end());
            }
        }
        st.finalize();
        print_row("B+ Tree", st, st.total_ms);
    }
    {
        Stat st; st.ops = queries.size() * (repeat - 1);
        for (int r = 0; r < repeat; ++r) {
            AdaptiveHashIndex ah;
            std::vector<int> keys = gen_random_unique(10000, 42);
            for (int k : keys) ah.insert(k, rid_of(k));
            volatile long long checksum = 0;
            std::vector<double> lat;
            double t0 = now_sec();
            for (int q : queries) {
                double t1 = now_sec();
                auto res = ah.get(q);
                lat.push_back(ns_since(t1));
                if (res.has_value()) checksum += res.value().page_no;
            }
            double dt = ms_since(t0);
            if (r > 0) {
                st.total_ms += dt;
                st.lat.insert(st.lat.end(), lat.begin(), lat.end());
            }
            st.hit += ah.hash_hit_count(); st.fb += ah.bplus_fallback_count();
        }
        st.hit /= (repeat - 1); st.fb /= (repeat - 1);
        st.finalize();
        std::printf("  %-22s %8.2f ms  avg %6.2f ns  P50 %5.2f ns  P95 %6.2f ns  "
                    "%7.0f ops/s  hit=%u fb=%u\n",
                    "AdaptiveHash", st.total_ms, st.total_ms * 1000000.0 / st.ops,
                    st.p50, st.p95, st.ops / (st.total_ms / 1000.0),
                    (unsigned)st.hit, (unsigned)st.fb);
    }
}

// ---- 范围查询基准 ----
static void bench_range(const std::vector<std::pair<int, int>>& ranges, int repeat) {
    std::printf("\n[Range Query]\n");
    std::vector<int> keys = gen_random_unique(10000, 42);
    for (auto& rg : ranges) {
        std::printf("  range [%d, %d]\n", rg.first, rg.second);
        {
            Stat st; st.ops = 100 * (repeat - 1);
            size_t res_count = 0;
            for (int r = 0; r < repeat; ++r) {
                BPlusTree tree;
                for (int k : keys) tree.insert(k, rid_of(k));
                volatile long long checksum = 0;
                double t0 = now_sec();
                for (int rep = 0; rep < 100; ++rep) {
                    checksum += tree.range_query(rg.first, rg.second).size();
                }
                double dt = ms_since(t0);
                if (r > 0) {
                    st.total_ms += dt;
                    res_count = static_cast<size_t>(checksum / 100);
                }
            }
            std::printf("    %-22s %8.2f ms  (%u results/query)\n", "B+ Tree",
                        st.total_ms, (unsigned)res_count);
        }
        {
            Stat st; st.ops = 100 * (repeat - 1);
            for (int r = 0; r < repeat; ++r) {
                AdaptiveHashIndex ah;
                for (int k : keys) ah.insert(k, rid_of(k));
                volatile long long checksum = 0;
                double t0 = now_sec();
                for (int rep = 0; rep < 100; ++rep) {
                    checksum += ah.range_query(rg.first, rg.second).size();
                }
                double dt = ms_since(t0);
                if (r > 0) st.total_ms += dt;
            }
            std::printf("    %-22s %8.2f ms\n", "AdaptiveHash", st.total_ms);
        }
    }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpc_freq = (double)freq.QuadPart;
    const int REPEAT = 9;  // 9 次，去首取 8 次平均

    std::printf("mini_rmdb Day3 Benchmark (repeat=%d, keep=%d)\n", REPEAT, REPEAT - 1);

    std::vector<int> sorted10k = gen_sorted(10000);
    std::vector<int> random10k = gen_random_unique(10000, 42);

    bench_insert("Sorted 10k", sorted10k, REPEAT);
    bench_insert("Random 10k", random10k, REPEAT);

    // 均匀点查询：10 万次
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> uniform(0, 9999);
    std::vector<int> uni_q;
    uni_q.reserve(100000);
    for (int i = 0; i < 100000; ++i) uni_q.push_back(uniform(rng));

    bench_point("Uniform 100k", uni_q, REPEAT);
    bench_point("Hot 80/20 100k", gen_hot_workload(100000, 99), REPEAT);

    std::vector<std::pair<int, int>> ranges = {{100, 200}, {1000, 2000}, {0, 9999}};
    bench_range(ranges, REPEAT);

    std::printf("\nBenchmark done.\n");
    return 0;
}
