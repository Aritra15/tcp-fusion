#!/usr/bin/env python3
"""
Generate cubic-fig-5.2: throughput vs time for one TCP-Fusion flow
competing with one TCP NewReno flow (RTT=8ms and RTT=82ms).

This is intentionally separate from run_cubic08_figures.py to avoid
running all Cubic'08 sweeps.
"""

from pathlib import Path
import subprocess
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "scratch" / "tcp_fusion_results"
RESULTS.mkdir(parents=True, exist_ok=True)


def run_cmd(args):
    cp = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if cp.returncode != 0:
        raise RuntimeError(
            f"Command failed: {' '.join(args)}\nSTDOUT:\n{cp.stdout}\nSTDERR:\n{cp.stderr}"
        )
    return cp.stdout.strip()


def run_data_gen(rtt_ms: int):
    out_rel = f"scratch/tcp_fusion_results/cubic_fig5_2_rtt{rtt_ms}.dat"
    cmd = (
        f"cubic08-throughput-time "
        f"--tcpVariant=ns3::TcpFusion --rttMs={rtt_ms} --bandwidthMbps=400 "
        f"--simTime=120 --sampleInterval=1 --outFile={out_rel}"
    )
    run_cmd(["./ns3", "run", cmd, "--no-build"])
    return RESULTS / f"cubic_fig5_2_rtt{rtt_ms}.dat"


def plot_fig():
    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    colors = {"fusion": "#d62728", "reno": "#1f77b4"}

    for i, rtt in enumerate((8, 82)):
        path = RESULTS / f"cubic_fig5_2_rtt{rtt}.dat"
        d = np.loadtxt(path, comments="#")
        t, fusion, reno = d[:, 0], d[:, 1], d[:, 2]

        ax = axes[i]
        ax.plot(t, fusion, color=colors["fusion"], linewidth=1.9, label="TCP-Fusion flow")
        ax.plot(t, reno, color=colors["reno"], linewidth=1.9, linestyle="--", label="TCP NewReno flow")
        ax.set_ylabel("Throughput (Mbps)")
        ax.set_title(f"RTT = {rtt} ms, Bottleneck = 400 Mbps")
        ax.grid(True, alpha=0.3, linestyle="--")
        ax.legend(loc="upper right")

    axes[-1].set_xlabel("Time (s)")
    fig.suptitle("cubic-fig-5.2: Throughput vs Time (one TCP-Fusion + one NewReno)", y=0.99)
    fig.tight_layout()
    out = RESULTS / "cubic-fig-5.2.png"
    fig.savefig(out, dpi=160)
    plt.close(fig)
    print(f"Generated: {out}")


def main():
    # Build once so scratch program is available.
    run_cmd(["./ns3", "build"])

    for rtt in (8, 82):
        path = run_data_gen(rtt)
        print(f"Generated data: {path}")

    plot_fig()


if __name__ == "__main__":
    main()
