#!/usr/bin/env python3
"""Draws RQ1-RQ4 bar charts from result/kernel_results.csv and prints a summary table."""
import csv
import math
import statistics
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

METHODS = ("contiguous", "vectorized", "lzom")
LABELS = {"contiguous": "Contiguous", "vectorized": "Vectorized", "lzom": "LZOM"}
CORPORA = ("canterbury", "calgary", "large", "silesia")


def load(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in ("uncompressed_bytes", "compressed_bytes", "memory_bytes"):
            r[k] = int(r[k])
        for k in ("ratio", "seconds", "mb_per_sec", "decomp_seconds", "decomp_mb_per_sec"):
            r[k] = float(r[k])
    return rows


def sem(vals):
    return statistics.stdev(vals) / math.sqrt(len(vals)) if len(vals) > 1 else 0.0


def cell(rows, corpus, method):
    return [r for r in rows if r["category"] == corpus and r["method"] == method]


def draw(path, title, ylabel, values, errors, fmt, note):
    """values/errors: {corpus: {method: float}}. One group of bars per corpus."""
    width = 0.25
    fig, ax = plt.subplots(figsize=(9.5, 6))
    for i, m in enumerate(METHODS):
        xs = [k + (i - 1) * width for k in range(len(CORPORA))]
        vals = [values[c][m] for c in CORPORA]
        bars = ax.bar(xs, vals, width, label=LABELS[m], zorder=3)
        if any(errors[c][m] for c in CORPORA):
            ax.errorbar(xs, vals, yerr=[errors[c][m] for c in CORPORA], fmt="none",
                        ecolor="black", capsize=3, zorder=4)
        ax.bar_label(bars, labels=[fmt(v) for v in vals], fontsize=7.5, padding=10)
    ax.set_xticks(range(len(CORPORA)))
    ax.set_xticklabels([c.capitalize() for c in CORPORA])
    ax.set_ylabel(ylabel)
    ax.set_title(title, fontweight="bold")
    ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1), frameon=False)
    ax.spines[["top", "right"]].set_visible(False)
    bottom, top = ax.get_ylim()
    ax.set_ylim(bottom, top * 1.18)
    ax.grid(axis="y", linewidth=0.5, alpha=0.4, linestyle="--")
    ax.set_axisbelow(True)
    ax.text(0.01, 0.98, note, transform=ax.transAxes, ha="left", va="top", fontsize=8,
            bbox=dict(boxstyle="round", facecolor="#d9f2d9", edgecolor="#4a904a"))
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def main():
    csv_path = Path(sys.argv[1] if len(sys.argv) > 1 else "result/kernel_results.csv")
    out_dir = Path(sys.argv[2] if len(sys.argv) > 2 else "result")
    rows = load(csv_path)

    ratio, ratio_e = {}, {}
    mem, mem_e = {}, {}
    thr, thr_e = {}, {}
    dthr, dthr_e = {}, {}
    for c in CORPORA:
        ratio[c], ratio_e[c], mem[c], mem_e[c] = {}, {}, {}, {}
        thr[c], thr_e[c], dthr[c], dthr_e[c] = {}, {}, {}, {}
        for m in METHODS:
            sub = cell(rows, c, m)
            if not sub:
                continue
            # ratio and memory are exact for a given input; the same in every repetition
            ratio[c][m] = sub[0]["uncompressed_bytes"] / sub[0]["compressed_bytes"]
            ratio_e[c][m] = 0.0
            mem[c][m] = sum(r["memory_bytes"] for r in sub) / sum(r["uncompressed_bytes"] for r in sub)
            mem_e[c][m] = 0.0
            t = [r["mb_per_sec"] for r in sub]
            thr[c][m], thr_e[c][m] = statistics.mean(t), sem(t)
            d = [r["decomp_mb_per_sec"] for r in sub]
            dthr[c][m], dthr_e[c][m] = statistics.mean(d), sem(d)

    draw(out_dir / "rq1_ratio.png", "RQ1 — Compression Ratio",
         "uncompressed / compressed", ratio, ratio_e, lambda v: f"{v:.2f}x", "Higher is better")
    draw(out_dir / "rq2_memory.png", "RQ2 — Memory Footprint",
         "bytes needed per input byte", mem, mem_e, lambda v: f"{v:.2f}x", "Lower is better")
    draw(out_dir / "rq3_throughput.png", "RQ3 — Compression Throughput",
         "MB/s", thr, thr_e, lambda v: f"{v:.0f}", "Higher is better")
    draw(out_dir / "rq4_decompression.png", "RQ4 — Decompression Throughput",
         "MB/s", dthr, dthr_e, lambda v: f"{v:.0f}", "Higher is better")

    print("corpus      method        ratio   mem/byte   comp MB/s (±SEM)   decomp MB/s (±SEM)")
    for c in CORPORA:
        for m in METHODS:
            if m not in ratio[c]:
                continue
            print(f"{c:<11} {LABELS[m]:<12} {ratio[c][m]:>6.2f}x {mem[c][m]:>9.2f}x "
                  f"{thr[c][m]:>9.1f} ± {thr_e[c][m]:<6.1f} {dthr[c][m]:>9.1f} ± {dthr_e[c][m]:<6.1f}")


if __name__ == "__main__":
    main()
