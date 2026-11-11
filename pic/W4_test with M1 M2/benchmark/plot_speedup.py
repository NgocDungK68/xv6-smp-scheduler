#!/usr/bin/env python3
"""
plot_speedup.py - Ve bieu do Speedup / Efficiency tu results.csv (do bang bench.c).

Input CSV (co header), do run_bench.py tao ra:
    kernel,workload,cpus,run,time
    stock,alloc,1,1,156
    mine,alloc,8,3,22

  workload la cot tuy chon; neu khong co, tat ca duoc coi la mot workload.
  time = thoi gian hoan thanh TONG khoi luong co dinh (tick).

Speedup(N) = T(1) / T(N), lay median cac lan chay.

Dau ra:
  <stem>_<workload>.png : Speedup + Efficiency cho tung workload
  <stem>_all.png        : luoi Speedup cua tat ca workload (neu co >1 workload)

Vi du:
    python3 plot_speedup.py results.csv
    python3 plot_speedup.py results.csv --baseline stock --workload alloc
"""
import argparse
import csv
import os
import statistics
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load(path):
    # data[workload][kernel][cpus] = [time, ...]
    data = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    dropped = 0
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            t = float(row["time"])
            if t <= 0:  # 0 tick: khoi luong qua nho so voi do phan giai cua timer
                dropped += 1
                continue
            wl = (row.get("workload") or "all").strip()
            data[wl][row["kernel"].strip()][int(row["cpus"])].append(t)
    if dropped:
        print(f"[canh bao] bo qua {dropped} dong co time <= 0 "
              f"(tang `total` trong bench de do chinh xac hon)", file=sys.stderr)
    if not data:
        sys.exit("Khong co du lieu hop le trong file CSV.")
    return data


def speedups(per_kernel, baseline):
    """Tra ve {kernel: (cpus, median, speedup, lo, hi, efficiency)}."""
    shared_t1 = None
    if baseline:
        if baseline not in per_kernel or 1 not in per_kernel[baseline]:
            return None
        shared_t1 = statistics.median(per_kernel[baseline][1])
    out = {}
    for name, per_cpu in per_kernel.items():
        if shared_t1 is None and 1 not in per_cpu:
            print(f"[bo qua] '{name}' khong co du lieu 1 CPU", file=sys.stderr)
            continue
        t1 = shared_t1 if shared_t1 is not None else statistics.median(per_cpu[1])
        cpus = sorted(per_cpu)
        med = [statistics.median(per_cpu[n]) for n in cpus]
        sp = [t1 / m for m in med]
        # thoi gian lau nhat -> speedup thap nhat; nhanh nhat -> cao nhat
        lo = [t1 / max(per_cpu[n]) for n in cpus]
        hi = [t1 / min(per_cpu[n]) for n in cpus]
        eff = [s / n for s, n in zip(sp, cpus)]
        out[name] = (cpus, med, sp, lo, hi, eff)
    return out


def draw_speedup(ax, res, title, top):
    for name, (cpus, _, sp, lo, hi, _) in res.items():
        yerr = [[s - l for s, l in zip(sp, lo)], [h - s for s, h in zip(sp, hi)]]
        ax.errorbar(cpus, sp, yerr=yerr, marker="o", capsize=3, label=name)
    ax.plot([1, top], [1, top], "k--", lw=1, label="Ideal (linear)")
    ax.set_xlabel("So CPU")
    ax.set_ylabel("Speedup")
    ax.set_title(title)
    ax.set_xticks(range(1, top + 1))
    ax.grid(alpha=0.3)
    ax.legend()


def draw_efficiency(ax, res, title, top):
    for name, (cpus, _, _, _, _, eff) in res.items():
        ax.plot(cpus, eff, marker="o", label=name)
    ax.axhline(1.0, color="k", ls="--", lw=1, label="Ideal")
    ax.set_xlabel("So CPU")
    ax.set_ylabel("Efficiency = Speedup / N")
    ax.set_ylim(0, 1.15)
    ax.set_title(title)
    ax.set_xticks(range(1, top + 1))
    ax.grid(alpha=0.3)
    ax.legend()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("-o", "--output", default="speedup.png",
                    help="ten file goc; hau to _<workload>.png duoc them vao")
    ap.add_argument("--workload", default=None, help="chi ve mot workload")
    ap.add_argument("--baseline", default=None,
                    help="kernel dung lam moc T(1) chung (vd: stock). "
                         "Mac dinh: moi kernel chuan hoa theo T(1) cua chinh no.")
    args = ap.parse_args()

    data = load(args.csv)
    workloads = [args.workload] if args.workload else sorted(data)
    for wl in workloads:
        if wl not in data:
            sys.exit(f"Khong co workload '{wl}'. Co: {', '.join(sorted(data))}")

    stem, ext = os.path.splitext(args.output)
    base_note = f"T(1) cua '{args.baseline}'" if args.baseline else "T(1) cua tung kernel"
    results = {}

    print(f"{'workload':<9}{'kernel':<10}{'cpus':>5}{'median':>10}{'speedup':>10}{'eff.':>8}")
    for wl in workloads:
        res = speedups(data[wl], args.baseline)
        if res is None:
            print(f"[bo qua] workload '{wl}': khong co 1 CPU cua '{args.baseline}'", file=sys.stderr)
            continue
        if not res:
            print(f"[bo qua] workload '{wl}': khong co kernel nao co du lieu 1 CPU hop le", file=sys.stderr)
            continue
        results[wl] = res
        for name, (cpus, med, sp, _, _, eff) in res.items():
            for n, m, s, e in zip(cpus, med, sp, eff):
                print(f"{wl:<9}{name:<10}{n:>5}{m:>10.1f}{s:>10.2f}{e:>8.2f}")

        top = max(n for r in res.values() for n in r[0])
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.5))
        draw_speedup(ax1, res, f"{wl}: Speedup", top)
        draw_efficiency(ax2, res, f"{wl}: Efficiency", top)
        fig.tight_layout(rect=(0, 0.06, 1, 1))
        fig.text(0.5, 0.015, f"Moc chuan hoa: {base_note}", ha="center", fontsize=9)
        out = f"{stem}_{wl}{ext}" if (len(data) > 1 or args.workload) else args.output
        fig.savefig(out, dpi=150)
        plt.close(fig)
        print(f"Da luu: {out}", file=sys.stderr)

    if len(results) > 1:
        n = len(results)
        cols = 2
        rows = (n + cols - 1) // cols
        fig, axes = plt.subplots(rows, cols, figsize=(5.5 * cols, 4.2 * rows), squeeze=False)
        for ax, (wl, res) in zip(axes.flat, results.items()):
            top = max(c for r in res.values() for c in r[0])
            draw_speedup(ax, res, wl, top)
        for ax in list(axes.flat)[n:]:
            ax.axis("off")
        fig.tight_layout(rect=(0, 0.04, 1, 1))
        fig.text(0.5, 0.01, f"Moc chuan hoa: {base_note}", ha="center", fontsize=9)
        out = f"{stem}_all{ext}"
        fig.savefig(out, dpi=150)
        plt.close(fig)
        print(f"Da luu: {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
