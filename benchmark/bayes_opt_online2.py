# -*- coding: utf-8 -*-
"""贝叶斯优化·TPC-C 形态负载版：仿决赛负载寻优（备赛版 A 层改造）。

第 8 节负载参数化基础上，把负载调成决赛 TPC-C 形态：
- 窄热点：4/50 仓库 ≈ 8% 的 key（800 个）承载 65% 流量
- 写密集：NewOrder 45% + Payment 43% ≈ 88% 写（取 85%）
- 短事务等值点查 + 写后缓存失效

4 场景（两两隔离因素）：
  (65, 85, 800)  TPC-C 决赛形态
  (80,  0, 2000) 基线（第 8 节 80/0，对照）
  (65,  0, 800)  窄热点纯读（隔离"写压力"因素）
  (80, 85, 2000) 宽热点+写（隔离"热点宽度"因素）

用法：python benchmark/bayes_opt_online2.py
"""
import math
import random
import subprocess

EXE = "build/eval_point.exe"
LAMBDA = 0.005            # 耗时权重（ms 量级 5~30，λ×ms ≈ 0.025~0.15）
N_QUERY = 100000
SCENARIOS = [(65, 85, 800), (80, 0, 2000), (65, 0, 800), (80, 85, 2000)]


def evaluate(cap, thr, skew, write_pct, hot_set):
    """真跑一次负载，返回 (命中率, 耗时ms)。"""
    r = subprocess.run([EXE, str(int(cap)), str(int(thr)), str(skew), str(write_pct), str(hot_set)],
                       capture_output=True, text=True, timeout=120)
    line = r.stdout.strip().split()
    if len(line) < 3:
        return 0.0, 1e9
    return float(line[0]), float(line[1])


def score_of(hit, ms):
    return hit - LAMBDA * ms


# ---------- 手写高斯过程（同在线版） ----------
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
    return (math.log2(max(cap, 1)), float(int(thr)))


def sample_point(rng):
    cap = 512 * (2 ** rng.uniform(0.0, 5.0))   # [512, 16384]
    thr = rng.uniform(1.0, 10.0)
    return cap, thr


def random_search(n_evals, seed, skew, wr, hot):
    rng = random.Random(seed)
    best = -1e9
    traj = []
    for _ in range(n_evals):
        cap, thr = sample_point(rng)
        hit, ms = evaluate(cap, thr, skew, wr, hot)
        s = score_of(hit, ms)
        best = max(best, s)
        traj.append(best)
    return traj


def bayes_opt(n_init, n_iter, seed, skew, wr, hot):
    """贝叶斯优化（双目标 score）。返回 (每轮最优轨迹, 最终最优参数点, 最优score/命中/耗时)。"""
    rng = random.Random(seed)
    sampled = []
    for _ in range(n_init):
        cap, thr = sample_point(rng)
        hit, ms = evaluate(cap, thr, skew, wr, hot)
        sampled.append((encode(cap, thr), score_of(hit, ms), hit, ms))
    best_traj = [max(s for _, s, _, _ in sampled)]
    for _ in range(n_iter):
        X = [e for e, _, _, _ in sampled]
        y = [s for _, s, _, _ in sampled]
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
        hit, ms = evaluate(cap, thr, skew, wr, hot)
        sampled.append((enc, score_of(hit, ms), hit, ms))
        best_traj.append(max(best_traj[-1], score_of(hit, ms)))
    best_pt = max(sampled, key=lambda t: t[1])
    return best_traj, best_pt[0], best_pt[1], best_pt[2], best_pt[3]


def grid_baseline(skew, wr, hot):
    """网格 110 点（11 档容量 x 10 档阈值，1 次评估/点）。返回 (最优score, 最优hit, 最优ms, cap, thr)。"""
    best = (-1e9, 0.0, 1e9, 0, 0)
    caps = [512, 724, 1024, 1448, 2048, 2896, 4096, 5793, 8192, 11585, 16384]
    for c in caps:
        for t in range(1, 11):
            hit, ms = evaluate(c, t, skew, wr, hot)
            s = score_of(hit, ms)
            if s > best[0]:
                best = (s, hit, ms, c, t)
    return best


def main():
    print("bayes_opt_online2: TPC-C shape loads x dual objective (hit - %.3f*ms)" % LAMBDA, flush=True)
    for skew, wr, hot in SCENARIOS:
        gs, gh, gm, gc, gt = grid_baseline(skew, wr, hot)
        b_best, b_hit, b_ms, b_round = [], [], [], []
        for seed in range(3):
            traj, enc, s, hit, ms = bayes_opt(3, 12, seed, skew, wr, hot)
            b_best.append(s)
            b_hit.append(hit)
            b_ms.append(ms)
            target = gs * 0.99
            found = next((i + 1 for i, v in enumerate(traj) if v >= target), 15)
            b_round.append(found)
        r_best = []
        for seed in range(3):
            traj = random_search(15, seed, skew, wr, hot)
            r_best.append(traj[-1])
        b_avg = sum(b_best) / 3
        r_avg = sum(r_best) / 3
        print("== load skew=%d%% write=%d%% hot_set=%d | grid_best score=%.4f (hit=%.3f ms=%.1f cap=%d thr=%d)"
              % (skew, wr, hot, gs, gh, gm, gc, gt), flush=True)
        print("   bayes15 avg score=%.4f (ratio %.3f, avg hit=%.3f avg ms=%.1f, first>=99%% at ~%.1f/15)"
              % (b_avg, b_avg / gs if gs > 0 else 0, sum(b_hit) / 3, sum(b_ms) / 3, sum(b_round) / 3), flush=True)
        print("   random15 avg score=%.4f (ratio %.3f)" % (r_avg, r_avg / gs if gs > 0 else 0), flush=True)
    print("done", flush=True)


if __name__ == "__main__":
    main()
