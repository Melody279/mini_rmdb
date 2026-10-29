# -*- coding: utf-8 -*-
"""贝叶斯优化模拟：在 param_scan.csv 真值基线上对比"网格穷举 vs 贝叶斯采样"。

目标函数（每个负载一个）：参数 (容量, 阈值) -> 缓存命中率（用 CSV 中已测真值）。
- 网格穷举：25 次评估（5 容量 x 5 阈值），必得全局最优。
- 贝叶斯优化：初始随机 3 点 + 迭代 12 次（EI 采集），共 15 次评估。
用纯标准库手写：RBF 核高斯过程回归 + 高斯消元求逆 + Expected Improvement。
"""
import csv
import math
import random
import sys


def load_data(path):
    rows = {}
    with open(path, encoding="utf-8") as f:
        r = csv.DictReader(f)
        for row in r:
            load = row["load"]
            cap = int(row["capacity"])
            thr = int(row["threshold"])
            hit = float(row["read_hit_rate"])
            ms = float(row["ms_avg"])
            rows.setdefault(load, []).append((cap, thr, hit, ms))
    return rows


def encode(cap, thr):
    # 容量取 log2（512..16384 -> 9..14），阈值原值：两个维度尺度相近
    return (math.log2(cap), float(thr))


def rbf_k(x1, x2, l=1.0, s=1.0):
    d2 = (x1[0] - x2[0]) ** 2 + (x1[1] - x2[1]) ** 2
    return s * s * math.exp(-d2 / (2 * l * l))


def invert(A):
    """高斯-约当消元求逆（n<=25，够用）"""
    n = len(A)
    M = [row[:] + [1.0 if i == j else 0.0 for j in range(n)] for i, row in enumerate(A)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(M[r][col]))
        M[col], M[piv] = M[piv], M[col]
        pv = M[col][col]
        if abs(pv) < 1e-14:
            return None
        M[col] = [v / pv for v in M[col]]
        for r in range(n):
            if r != col:
                f = M[r][col]
                if f != 0.0:
                    M[r] = [M[r][c] - f * M[col][c] for c in range(2 * n)]
    return [row[n:] for row in M]


def gp_predict(X, y, Xq, l=1.0, s=1.0, noise=0.05):
    """后验均值/方差。X: 已采样编码点, y: 观测, Xq: 待预测编码点"""
    n = len(X)
    K = [[rbf_k(X[i], X[j], l, s) + (noise if i == j else 0.0) for j in range(n)] for i in range(n)]
    Kinv = invert(K)
    if Kinv is None:
        return None, None
    means, vars_ = [], []
    for xq in Xq:
        kx = [rbf_k(xq, X[i], l, s) for i in range(n)]
        mu = sum(kx[i] * sum(Kinv[i][j] * y[j] for j in range(n)) for i in range(n))
        v = rbf_k(xq, xq, l, s) - sum(
            kx[i] * sum(Kinv[i][j] * kx[j] for j in range(n)) for i in range(n)
        )
        means.append(mu)
        vars_.append(max(v, 1e-12))
    return means, vars_


def expected_improvement(means, vars_, y_best):
    """EI：期望改进。探索=方差大处，利用=均值高处，自动平衡。"""
    sqrt2 = math.sqrt(2.0)
    ei = []
    for m, v in zip(means, vars_):
        sd = math.sqrt(v)
        if sd < 1e-12:
            ei.append(0.0)
            continue
        z = (m - y_best) / sd
        cdf = 0.5 * (1.0 + math.erf(z / sqrt2))
        pdf = math.exp(-z * z / 2.0) / math.sqrt(2.0 * math.pi)
        ei.append((m - y_best) * cdf + sd * pdf)
    return ei


def simulate(points, n_init=3, n_iter=12, seed=0):
    """在 25 个真值点上模拟贝叶斯优化（评估=查真值表）。返回每步后的当前最优命中率。"""
    rng = random.Random(seed)
    # (enc, (cap,thr,hit,ms))
    cand = [(encode(c, t), (c, t, h, m)) for (c, t, h, m) in points]
    pool = list(cand)
    init = rng.sample(pool, n_init)
    rest = [x for x in pool if x not in init]

    sampled = list(init)  # 已评估
    X = [e for e, _ in sampled]
    y = [pt[2] for _, pt in sampled]
    best_traj = [max(y)]

    for _ in range(n_iter):
        Xq = [e for e, _ in rest]
        means, vars_ = gp_predict(X, y, Xq)
        if means is None:
            break
        ei = expected_improvement(means, vars_, max(y))
        idx = max(range(len(ei)), key=lambda i: ei[i])
        nxt = rest[idx]
        sampled.append(nxt)
        X = [e for e, _ in sampled]
        y = [pt[2] for _, pt in sampled]
        rest.pop(idx)
        best_traj.append(max(y))
    return best_traj


def random_search_traj(points, n_total=15, seed=0):
    """对照：纯随机采 15 个点，记录逐步最优（不建模、不利用已有信息）"""
    rng = random.Random(seed)
    pts = list(points)
    order = rng.sample(pts, n_total)
    best = 0.0
    traj = []
    for p in order:
        best = max(best, p[2])
        traj.append(best)
    return traj


def main():
    data = load_data("benchmark/param_scan.csv")
    print("load       grid25_best  bayes15_avg  random15_avg  bayes_found_round", flush=True)
    for load in ("uniform", "hot8020", "rw_80r20w"):
        pts = data[load]
        grid_best = max(p[2] for p in pts)
        b_avgs, r_avgs, rounds = [], [], []
        for seed in range(20):
            traj = simulate(pts, n_init=3, n_iter=12, seed=seed)
            b_avgs.append(traj[-1])
            # 首次达到 >= 99% 最优的轮次（找不到则为 15）
            target = grid_best * 0.99
            found = next((i + 1 for i, v in enumerate(traj) if v >= target), 15)
            rounds.append(found)
            r_avgs.append(random_search_traj(pts, 15, seed)[-1])
        print("%-10s  %.4f      %.4f      %.4f        %.1f"
              % (load, grid_best, sum(b_avgs) / 20.0, sum(r_avgs) / 20.0,
                 sum(rounds) / 20.0), flush=True)
        # 附：贝叶斯达到 99% 最优时的耗时 vs 网格最优耗时（在最终点集里取最优）
        ms_best, ms_grid = 0.0, 1e9
        for p in pts:
            if p[2] == grid_best:
                ms_grid = min(ms_grid, p[3])
        for seed in range(20):
            rng = random.Random(seed)
            cand = [(encode(c, t), (c, t, h, m)) for (c, t, h, m) in pts]
            pool = list(cand)
            init = rng.sample(pool, 3)
            rest = [x for x in pool if x not in init]
            sampled = list(init)
            X = [e for e, _ in sampled]
            y = [pt[2] for _, pt in sampled]
            for _ in range(12):
                Xq = [e for e, _ in rest]
                means, vars_ = gp_predict(X, y, Xq)
                if means is None:
                    break
                ei = expected_improvement(means, vars_, max(y))
                idx = max(range(len(ei)), key=lambda i: ei[i])
                sampled.append(rest[idx])
                X = [e for e, _ in sampled]
                y = [pt[2] for _, pt in sampled]
                rest.pop(idx)
            best_pt = max(sampled, key=lambda x: x[1][2])
            ms_best += best_pt[1][3]
        print("           (bayes best ms avg=%.2f, grid best ms=%.2f)" % (ms_best / 20.0, ms_grid), flush=True)


if __name__ == "__main__":
    main()
