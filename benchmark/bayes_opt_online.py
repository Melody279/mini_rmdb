# -*- coding: utf-8 -*-
"""贝叶斯优化·在线版：连续参数空间 + 真实负载评估（备赛版）。

升级点（对应 BAYES_OPT.md 局限 1、2）：
- 在线评估：每次"评估"= 真跑一次 C++ 负载（eval_point.exe），带真实噪声，模拟决赛"提交->看TPC-C"闭环
- 连续参数空间：容量 [512,16384]（log2 编码 9~14 连续）、阈值 [1,10]（取整），网格穷举无法覆盖

目标：热点 80/20 负载下命中率。对比：
- 网格穷举（11x10=110 点，1 次评估/点）：近似全局最优
- 贝叶斯优化（3 初始 + 12 迭代 = 15 次评估，10 个种子取平均）
- 随机搜索（15 次评估）
"""
import math
import random
import subprocess
import sys

EXE = "build/eval_point.exe"
LOAD_HOT = 0


def evaluate(cap, thr):
    """真跑一次负载，返回 (命中率, 耗时ms)。带真实噪声（单次测量）。"""
    r = subprocess.run([EXE, str(int(cap)), str(int(thr)), str(LOAD_HOT)],
                       capture_output=True, text=True, timeout=120)
    line = r.stdout.strip().split()
    if len(line) < 3:
        return 0.0, 1e9
    return float(line[0]), float(line[1])


# ---------- 手写高斯过程 ----------
def rbf_k(x1, x2, l=1.0, s=1.0):
    d2 = (x1[0] - x2[0]) ** 2 + (x1[1] - x2[1]) ** 2
    return s * s * math.exp(-d2 / (2 * l * l))


def invert(A):
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


def gp_predict(X, y, Xq, l=1.0, s=1.0, noise=0.02):
    n = len(X)
    K = [[rbf_k(X[i], X[j], l, s) + (noise if i == j else 0.0) for j in range(n)] for i in range(n)]
    Kinv = invert(K)
    if Kinv is None:
        return None, None
    means, vars_ = [], []
    for xq in Xq:
        kx = [rbf_k(xq, X[i], l, s) for i in range(n)]
        mu = sum(kx[i] * sum(Kinv[i][j] * y[j] for j in range(n)) for i in range(n))
        v = rbf_k(xq, xq, l, s) - sum(kx[i] * sum(Kinv[i][j] * kx[j] for j in range(n)) for i in range(n))
        means.append(mu)
        vars_.append(max(v, 1e-12))
    return means, vars_


def expected_improvement(means, vars_, y_best):
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


# ---------- 参数空间（连续） ----------
def encode(cap, thr):
    # 容量 log2 连续化（512->9.0, 16384->14.0），阈值取整后原值
    return (math.log2(max(cap, 1)), float(int(thr)))


def sample_point(rng):
    # 随机采样一个候选参数点（连续空间）
    cap = 512 * (2 ** rng.uniform(0.0, 5.0))   # [512, 16384]
    thr = rng.uniform(1.0, 10.0)
    return cap, thr


def random_search(n_evals, seed):
    rng = random.Random(seed)
    best = 0.0
    traj = []
    for _ in range(n_evals):
        cap, thr = sample_point(rng)
        hit, _ = evaluate(cap, thr)
        best = max(best, hit)
        traj.append(best)
    return traj


def bayes_opt(n_init, n_iter, seed):
    """贝叶斯优化：真实在线评估。返回 (每轮最优轨迹, 最终参数点, 最终命中率)。"""
    rng = random.Random(seed)
    sampled = []      # (enc, hit)
    # 初始随机点
    for _ in range(n_init):
        cap, thr = sample_point(rng)
        hit, _ = evaluate(cap, thr)
        sampled.append((encode(cap, thr), hit))
    best_traj = [max(h for _, h in sampled)]
    for _ in range(n_iter):
        X = [e for e, _ in sampled]
        y = [h for _, h in sampled]
        # 候选 = 细网格（log2 容量 64 档 x 阈值 10 档 = 640 点），EI 在其上选，剔除已采样
        cand = []
        for i in range(64):
            cap = 2.0 ** (9.0 + 5.0 * i / 63.0)   # [512, 16384]
            for t in range(1, 11):
                enc = encode(cap, t)
                if all(abs(enc[0] - x[0]) > 1e-9 or enc[1] != x[1] for x in X):
                    cand.append((enc, cap, t))
        Xq = [c[0] for c in cand]
        means, vars_ = gp_predict(X, y, Xq)
        if means is None:
            break
        ei = expected_improvement(means, vars_, max(y))
        idx = max(range(len(ei)), key=lambda i: ei[i])
        enc, cap, thr = cand[idx]
        hit, _ = evaluate(cap, thr)
        sampled.append((enc, hit))
        best_traj.append(max(best_traj[-1], hit))
    best_pt = max(sampled, key=lambda t: t[1])
    return best_traj, best_pt[0], best_pt[1]


def grid_baseline():
    """网格穷举近似：11 档容量 x 10 档阈值 = 110 点，1 次评估/点。返回全局最优命中率。"""
    best = 0.0
    caps = [512, 724, 1024, 1448, 2048, 2896, 4096, 5793, 8192, 11585, 16384]
    for c in caps:
        for t in range(1, 11):
            hit, _ = evaluate(c, t)
            best = max(best, hit)
    return best


def main():
    print("bayes_opt_online: continuous space + real load evaluation (hot 80/20)", flush=True)
    grid_best = grid_baseline()
    print("grid 110-point approx best hit = %.4f" % grid_best, flush=True)

    seeds = list(range(6))
    b_bests, b_rounds = [], []
    for seed in seeds:
        traj, enc, hit = bayes_opt(3, 12, seed)
        b_bests.append(hit)
        target = grid_best * 0.99
        found = next((i + 1 for i, v in enumerate(traj) if v >= target), 15)
        b_rounds.append(found)
        print("  seed=%d final=%.4f (cap~%d thr~%d) first>=99%%best at round %d"
              % (seed, hit, int(2 ** enc[0]), int(enc[1]), found), flush=True)

    r_bests = []
    for seed in seeds:
        traj = random_search(15, seed)
        r_bests.append(traj[-1])

    print("", flush=True)
    print("bayes15 avg best = %.4f (of grid %.4f, ratio %.3f)"
          % (sum(b_bests) / len(b_bests), grid_best, sum(b_bests) / len(b_bests) / grid_best), flush=True)
    print("random15 avg best = %.4f (ratio %.3f)"
          % (sum(r_bests) / len(r_bests), sum(r_bests) / len(r_bests) / grid_best), flush=True)
    print("bayes avg first-hit round = %.1f (15 evaluations total)" % (sum(b_rounds) / len(b_rounds)), flush=True)


if __name__ == "__main__":
    main()
