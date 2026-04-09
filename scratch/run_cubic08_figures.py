#!/usr/bin/env python3
"""
Run and plot CUBIC'08-inspired figures:
- cubic-fig-5  : throughput vs time (CUBIC vs NewReno) at RTT=8ms and RTT=82ms
- cubic-fig-10a: throughput share vs RTT (BW fixed at 400Mbps)
- cubic-fig-10b: throughput share vs bandwidth (RTT fixed at 10ms)
- cubic-fig-10c: throughput share vs bandwidth (RTT fixed at 100ms)
"""

import os
import subprocess
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "scratch" / "tcp_fusion_results"
RESULTS.mkdir(parents=True, exist_ok=True)

VARIANTS = [
    "TcpCubic",
    "TcpBic",
    "TcpHighSpeed",
    "TcpFusion",
    "TcpNewReno",
]

LABELS = {
    "TcpCubic": "CUBIC",
    "TcpBic": "BIC",
    "TcpHighSpeed": "HighSpeed TCP",
    "TcpFusion": "TCP-Fusion",
    "TcpNewReno": "TCP NewReno",
}

COLORS = {
    "TcpCubic": "#2ca02c",
    "TcpBic": "#9467bd",
    "TcpHighSpeed": "#ff7f0e",
    "TcpFusion": "#d62728",
    "TcpNewReno": "#1f77b4",
}


def run_cmd(args):
    cp = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if cp.returncode != 0:
        raise RuntimeError(f"Command failed: {' '.join(args)}\nSTDOUT:\n{cp.stdout}\nSTDERR:\n{cp.stderr}")
    return cp.stdout.strip()


def run_ns3_scratch(scratch_args):
    # Use --no-build, assume caller built once before.
    cmd = ["./ns3", "run", scratch_args, "--no-build"]
    return run_cmd(cmd)


def parse_last_two_floats(stdout_text):
    lines = [ln.strip() for ln in stdout_text.splitlines() if ln.strip()]
    for ln in reversed(lines):
        parts = ln.split()
        if len(parts) >= 2:
            try:
                return float(parts[0]), float(parts[1])
            except ValueError:
                continue
    raise RuntimeError("Could not parse two throughput values from output")


def write_share_dat(path, x_name, x_values, rows):
    # rows: dict variant -> list of shares
    with open(path, "w") as f:
        f.write("# " + x_name + " " + " ".join(VARIANTS) + "\n")
        for i, xv in enumerate(x_values):
            vals = [rows[v][i] for v in VARIANTS]
            f.write(f"{xv} " + " ".join(f"{z:.4f}" for z in vals) + "\n")


def build_once():
    print("[1/4] Building ns-3 scratch programs...")
    run_cmd(["./ns3", "build"])


def run_fig5_data():
    print("[2/4] Running cubic-fig-5 data (throughput vs time)...")
    for rtt in (8, 82):
        out = RESULTS / f"cubic_fig5_rtt{rtt}.dat"
        rel_out = f"scratch/tcp_fusion_results/cubic_fig5_rtt{rtt}.dat"
        cmd = (
            f"cubic08-throughput-time "
            f"--tcpVariant=ns3::TcpCubic --rttMs={rtt} --bandwidthMbps=400 "
            f"--simTime=120 --sampleInterval=1 --outFile={rel_out}"
        )
        run_ns3_scratch(cmd)
        print(f"  wrote {out}")


def run_fig10_data():
    print("[3/4] Running cubic-fig-10a/10b/10c data (share sweeps)...")

    # 10a: BW fixed, RTT sweep
    rtts = [10, 20, 40, 80, 120, 160]
    rows_10a = {v: [] for v in VARIANTS}
    for rtt in rtts:
        for v in VARIANTS:
            cmd = (
                f"cubic08-share-sweep --tcpVariant=ns3::{v} --rttMs={rtt} "
                f"--bandwidthMbps=400 --simTime=40 --nVariantFlows=4 --nRenoFlows=4"
            )
            out = run_ns3_scratch(cmd)
            var_mbps, reno_mbps = parse_last_two_floats(out)
            share = 100.0 * var_mbps / max(1e-9, var_mbps + reno_mbps)
            rows_10a[v].append(share)

    write_share_dat(RESULTS / "cubic_fig10a.dat", "rtt_ms", rtts, rows_10a)

    # 10b: RTT fixed 10ms, bandwidth sweep
    bws = [50, 100, 200, 400, 800]
    rows_10b = {v: [] for v in VARIANTS}
    for bw in bws:
        for v in VARIANTS:
            cmd = (
                f"cubic08-share-sweep --tcpVariant=ns3::{v} --rttMs=10 "
                f"--bandwidthMbps={bw} --simTime=40 --nVariantFlows=4 --nRenoFlows=4"
            )
            out = run_ns3_scratch(cmd)
            var_mbps, reno_mbps = parse_last_two_floats(out)
            share = 100.0 * var_mbps / max(1e-9, var_mbps + reno_mbps)
            rows_10b[v].append(share)

    write_share_dat(RESULTS / "cubic_fig10b.dat", "bandwidth_mbps", bws, rows_10b)

    # 10c: RTT fixed 100ms, bandwidth sweep
    rows_10c = {v: [] for v in VARIANTS}
    for bw in bws:
        for v in VARIANTS:
            cmd = (
                f"cubic08-share-sweep --tcpVariant=ns3::{v} --rttMs=100 "
                f"--bandwidthMbps={bw} --simTime=40 --nVariantFlows=4 --nRenoFlows=4"
            )
            out = run_ns3_scratch(cmd)
            var_mbps, reno_mbps = parse_last_two_floats(out)
            share = 100.0 * var_mbps / max(1e-9, var_mbps + reno_mbps)
            rows_10c[v].append(share)

    write_share_dat(RESULTS / "cubic_fig10c.dat", "bandwidth_mbps", bws, rows_10c)


def _load_numeric_table(path):
    with open(path) as f:
        header = None
        for line in f:
            if line.startswith("#"):
                header = line[1:].strip().split()
                break
    data = np.loadtxt(path, comments="#")
    return header, data


def plot_fig5():
    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    for i, rtt in enumerate((8, 82)):
        path = RESULTS / f"cubic_fig5_rtt{rtt}.dat"
        d = np.loadtxt(path, comments="#")
        t, var, reno = d[:, 0], d[:, 1], d[:, 2]
        ax = axes[i]
        ax.plot(t, var, color=COLORS["TcpCubic"], linewidth=1.8, label="CUBIC flow")
        ax.plot(t, reno, color=COLORS["TcpNewReno"], linewidth=1.8, linestyle="--", label="NewReno flow")
        ax.set_ylabel("Throughput (Mbps)")
        ax.set_title(f"RTT = {rtt} ms, Bottleneck = 400 Mbps")
        ax.grid(True, alpha=0.3, linestyle="--")
        ax.legend(loc="upper right")
    axes[-1].set_xlabel("Time (s)")
    fig.suptitle("cubic-fig-5: Throughput vs Time (one CUBIC + one NewReno)", y=0.99)
    fig.tight_layout()
    out = RESULTS / "cubic-fig-5.png"
    fig.savefig(out, dpi=160)
    plt.close(fig)
    print(f"  wrote {out}")


def _plot_share(path, title, x_label, out_name):
    header, data = _load_numeric_table(path)
    x = data[:, 0]

    fig, ax = plt.subplots(figsize=(10, 6))
    for i, v in enumerate(VARIANTS, start=1):
        ax.plot(x, data[:, i], marker="o", linewidth=2,
                label=LABELS.get(v, v), color=COLORS.get(v, "gray"))

    ax.axhline(50.0, color="black", linestyle=":", linewidth=1.4, alpha=0.7, label="Equal share (50%)")
    ax.set_xlabel(x_label)
    ax.set_ylabel("Variant group throughput share (%)")
    ax.set_title(title)
    ax.grid(True, alpha=0.3, linestyle="--")
    ax.set_ylim(0, 100)
    ax.legend(loc="best")
    fig.tight_layout()
    out = RESULTS / out_name
    fig.savefig(out, dpi=160)
    plt.close(fig)
    print(f"  wrote {out}")


def plot_fig10s():
    _plot_share(
        RESULTS / "cubic_fig10a.dat",
        "cubic-fig-10a: Throughput Share vs RTT (BW = 400 Mbps)",
        "RTT (ms)",
        "cubic-fig-10a.png",
    )
    _plot_share(
        RESULTS / "cubic_fig10b.dat",
        "cubic-fig-10b: Throughput Share vs Bandwidth (RTT = 10 ms)",
        "Bandwidth (Mbps)",
        "cubic-fig-10b.png",
    )
    _plot_share(
        RESULTS / "cubic_fig10c.dat",
        "cubic-fig-10c: Throughput Share vs Bandwidth (RTT = 100 ms)",
        "Bandwidth (Mbps)",
        "cubic-fig-10c.png",
    )


def main():
    print(f"Results directory: {RESULTS}")
    build_once()
    run_fig5_data()
    run_fig10_data()
    print("[4/4] Plotting figures...")
    plot_fig5()
    plot_fig10s()
    print("Done.")


if __name__ == "__main__":
    main()
