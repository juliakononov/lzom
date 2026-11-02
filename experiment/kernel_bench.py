#!/usr/bin/env python3
"""
Writes result/kernel_results.csv (one row per corpus, method and repetition,
the format plot.py reads) and checks the stored bytes once per corpus/method.
"""
import argparse
import csv
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE = HERE.parent / "lzom_module.ko"
SYSFS = Path("/sys/module/lzom_module/parameters")
DEV = Path("/dev/lzom0")
CORPORA = ("canterbury", "calgary", "large", "silesia")
METHODS = {"lzom": "lzom", "cont": "contiguous", "vect": "vectorized"}
MB = 1024 * 1024
DEV_KB = 262144


def sh(*cmd, check=True):
    return subprocess.run([str(c) for c in cmd], check=check, capture_output=True, text=True)


def param(name, value=None):
    path = SYSFS / name
    if value is None:
        return int(path.read_text())
    path.write_text(str(value))


def unload():
    for mod in ("lzom_module", "brd"):
        sh("rmmod", mod, check=False)


def load():
    unload()
    sh("modprobe", "brd", "rd_nr=1", f"rd_size={DEV_KB}", "max_part=0")
    sh("insmod", MODULE)
    (SYSFS / "path").write_text("/dev/ram0")
    for _ in range(50):
        if DEV.exists() and DEV.is_block_device():
            return
        time.sleep(0.1)
    raise RuntimeError("/dev/lzom0 did not appear")


def concat_corpus(src_dir, out_path):
    with open(out_path, "wb") as out:
        for f in sorted(p for p in src_dir.iterdir() if p.is_file()):
            out.write(f.read_bytes())
    return out_path.stat().st_size


def dd_write(cpu, src, size):
    full, tail = divmod(size, MB)
    tail = tail // 4096 * 4096
    if full:
        r = sh("taskset", "-c", cpu, "dd", f"if={src}", f"of={DEV}", "bs=1M", f"count={full}",
               "oflag=direct", "status=none", check=False)
        if r.returncode:
            raise RuntimeError(f"dd full blocks failed: {r.stderr}")
    if tail:
        r = sh("taskset", "-c", cpu, "dd", f"if={src}", f"of={DEV}", f"bs={tail}", "count=1",
               f"skip={full * MB}", "iflag=skip_bytes", f"seek={full * MB}", "oflag=seek_bytes,direct",
               "status=none", check=False)
        if r.returncode:
            raise RuntimeError(f"dd tail failed: {r.stderr}")
    return full * MB + tail


def verify(src, written):
    r = sh("cmp", "-n", written, src, DEV, check=False)
    if r.returncode:
        raise RuntimeError(f"stored bytes differ from the original: {r.stderr}")


def one_run(cpu, src, size, method):
    param("comp_type", method)
    param("stats_reset", 1)
    written = dd_write(cpu, src, size)
    return {
        "bytes": param("stats_w_bytes"),
        "comp_bytes": param("stats_w_comp_bytes"),
        "copy_ns": param("stats_w_copy_ns"),
        "comp_ns": param("stats_w_comp_ns"),
        "decomp_ns": param("stats_w_decomp_ns"),
        "mem": param("stats_w_mem"),
        "written": written,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reps", type=int, default=15, help="measured repetitions per corpus and method")
    ap.add_argument("--cpu", default="2", help="CPU to pin dd to")
    ap.add_argument("--corpora", default=",".join(CORPORA))
    ap.add_argument("--out", default=str(HERE / "result" / "kernel_results.csv"))
    args = ap.parse_args()

    out = Path(args.out)
    out.parent.mkdir(exist_ok=True)
    rows = []
    workdir = Path(tempfile.mkdtemp(prefix="lzom_corpus_"))
    try:
        load()
        for corpus in args.corpora.split(","):
            src = workdir / f"{corpus}.bin"
            size = concat_corpus(HERE / "dataset" / corpus, src)
            print(f"{corpus}: {size} bytes", flush=True)
            for key, label in METHODS.items():
                print(f"  {label}", flush=True)
                for rep in range(args.reps + 1):
                    run = one_run(args.cpu, src, size, key)
                    if rep == 1:
                        verify(src, run["written"])
                    if rep == 0:
                        continue
                    b = run["bytes"]
                    ns = run["copy_ns"] + run["comp_ns"]
                    rows.append({
                        "category": corpus,
                        "file": corpus,
                        "method": label,
                        "uncompressed_bytes": b,
                        "compressed_bytes": run["comp_bytes"],
                        "ratio": f"{b / run['comp_bytes']:.6f}",
                        "seconds": f"{ns / 1e9:.9f}",
                        "mb_per_sec": f"{(b / 1e6) / (ns / 1e9):.3f}",
                        "memory_bytes": run["mem"],
                        "decomp_seconds": f"{run['decomp_ns'] / 1e9:.9f}",
                        "decomp_mb_per_sec": f"{(b / 1e6) / (run['decomp_ns'] / 1e9):.3f}",
                    })
                    print(f"    rep {rep}/{args.reps}: {rows[-1]['mb_per_sec']} MB/s comp, "
                          f"{rows[-1]['decomp_mb_per_sec']} MB/s decomp", flush=True)
            with open(out, "w", newline="") as f:
                w = csv.DictWriter(f, fieldnames=list(rows[0]))
                w.writeheader()
                w.writerows(rows)
    finally:
        unload()
        shutil.rmtree(workdir, ignore_errors=True)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
