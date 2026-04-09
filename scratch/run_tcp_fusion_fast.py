#!/usr/bin/env python3


import os
import subprocess
import sys
import shutil

NS3_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(NS3_DIR, "scratch", "tcp_fusion_results")
SIM_TIME = 15
SEG_SIZE = 1448

LOSS_RATES = [1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1]

SINGLE_FLOW_VARIANTS = [
    "ns3::TcpFusion", "ns3::TcpNewReno", "ns3::TcpCubic",
    "ns3::TcpBic", "ns3::TcpHighSpeed", "ns3::TcpWestwoodPlus",
]
TWO_FLOW_VARIANTS_FIG5 = ["ns3::TcpFusion", "ns3::TcpWestwoodPlus"]
TWO_FLOW_VARIANTS_FIG6 = ["ns3::TcpHighSpeed", "ns3::TcpBic", "ns3::TcpCubic"]
CWND_VARIANTS = ["ns3::TcpFusion", "ns3::TcpCubic", "ns3::TcpNewReno"]


def run_ns3(program, args_str):
    cmd = f'./ns3 run "{program} {args_str}" --no-build'
    print(f"  >> {cmd}")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True,
                            cwd=NS3_DIR, timeout=600)
    if result.returncode != 0:
        print(f"  !! STDERR: {result.stderr[-200:]}")
    lines = [l.strip() for l in result.stdout.strip().split("\n") if l.strip()]
    return lines[-1] if lines else ""


def ensure_dir(d):
    os.makedirs(d, exist_ok=True)


def run_single_flow():
    print("\n" + "=" * 70)
    print("EXPERIMENT 1: Single flow throughput vs loss rate")
    print("=" * 70)
    results = {}
    for variant in SINGLE_FLOW_VARIANTS:
        name = variant.split("::")[-1]
        results[name] = []
        for lr in LOSS_RATES:
            args = (f"--tcpVariant={variant} --lossRate={lr} "
                    f"--simTime={SIM_TIME} --segmentSize={SEG_SIZE}")
            out = run_ns3("tcp-fusion-single-flow", args)
            try:
                parts = out.split()
                thr = float(parts[1])
            except (IndexError, ValueError):
                thr = 0.0
            results[name].append((lr, thr))
            print(f"    {name} loss={lr:.0e}  thr={thr:.2f} Mbps")

    fpath = os.path.join(OUT_DIR, "single_flow_throughput.dat")
    with open(fpath, "w") as f:
        f.write("# lossRate " + " ".join([v.split("::")[-1] for v in SINGLE_FLOW_VARIANTS]) + "\n")
        for i, lr in enumerate(LOSS_RATES):
            row = [f"{lr:.1e}"]
            for variant in SINGLE_FLOW_VARIANTS:
                name = variant.split("::")[-1]
                row.append(f"{results[name][i][1]:.4f}")
            f.write(" ".join(row) + "\n")
    return results


def run_two_flows(variants, label):
    print(f"\n{'=' * 70}")
    print(f"EXPERIMENT 2 ({label}): Two coexisting flows")
    print("=" * 70)
    results = {}
    for variant in variants:
        name = variant.split("::")[-1]
        results[name] = []
        for lr in LOSS_RATES:
            args = (f"--tcpVariant={variant} --lossRate={lr} "
                    f"--simTime={SIM_TIME} --segmentSize={SEG_SIZE}")
            out = run_ns3("tcp-fusion-two-flows", args)
            try:
                parts = out.split()
                vthr, rthr = float(parts[1]), float(parts[2])
            except (IndexError, ValueError):
                vthr, rthr = 0.0, 0.0
            results[name].append((lr, vthr, rthr))
            print(f"    {name}+Reno loss={lr:.0e}  var={vthr:.2f}  reno={rthr:.2f}")

    fpath = os.path.join(OUT_DIR, f"two_flows_{label}.dat")
    with open(fpath, "w") as f:
        f.write("# lossRate")
        for v in variants:
            n = v.split("::")[-1]
            f.write(f" {n}_thr Reno_with_{n}")
        f.write("\n")
        for i, lr in enumerate(LOSS_RATES):
            row = [f"{lr:.1e}"]
            for v in variants:
                n = v.split("::")[-1]
                row.append(f"{results[n][i][1]:.4f}")
                row.append(f"{results[n][i][2]:.4f}")
            f.write(" ".join(row) + "\n")
    return results


def run_cwnd_traces():
    print(f"\n{'=' * 70}")
    print("EXPERIMENT 3: Congestion window behaviour")
    print("=" * 70)
    for variant in CWND_VARIANTS:
        name = variant.split("::")[-1]
        prefix = f"cwnd_{name}"
        args = (f"--tcpVariant={variant} --simTime=60 "
                f"--lossRate=1e-5 --outPrefix={prefix} --outDir={OUT_DIR}")
        run_ns3("tcp-fusion-cwnd", args)
        print(f"    Generated cwnd traces for {name}")


def generate_plots(single_results, two_fig5_results, two_fig6_results):
    print(f"\n{'=' * 70}")
    print("GENERATING PLOTS")
    print("=" * 70)
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("  !! matplotlib not found. Raw data saved in", OUT_DIR)
        return

    colors = {
        "TcpFusion": "#e41a1c", "TcpNewReno": "#377eb8",
        "TcpCubic": "#4daf4a", "TcpBic": "#984ea3",
        "TcpHighSpeed": "#ff7f00", "TcpVegas": "#a65628",
        "TcpWestwoodPlus": "#f781bf",
    }
    markers = {
        "TcpFusion": "o", "TcpNewReno": "s", "TcpCubic": "^",
        "TcpBic": "D", "TcpHighSpeed": "v", "TcpVegas": "p",
        "TcpWestwoodPlus": "*",
    }

    # Figure 4: Single flow
    fig, ax = plt.subplots(figsize=(10, 6))
    for name, data in single_results.items():
        lrs = [d[0] for d in data]
        thrs = [d[1] for d in data]
        ax.semilogx(lrs, thrs, marker=markers.get(name, "o"),
                     color=colors.get(name, "gray"), label=name, linewidth=2, markersize=8)
    ax.set_xlabel("Packet Loss Rate", fontsize=13)
    ax.set_ylabel("Throughput (Mbps)", fontsize=13)
    ax.set_title("Fig 4: Single Flow Throughput vs Loss Rate (100 Mbps link)", fontsize=14)
    ax.legend(fontsize=10)
    ax.grid(True, which="both", alpha=0.3)
    ax.set_xlim([5e-7, 2e-1])
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "fig4_single_flow_throughput.png"), dpi=150)
    print("  Saved fig4_single_flow_throughput.png")
    plt.close(fig)

    # Figure 5: Two flows hybrid
    fig, ax = plt.subplots(figsize=(10, 6))
    for name, data in two_fig5_results.items():
        lrs = [d[0] for d in data]
        vthr = [d[1] for d in data]
        rthr = [d[2] for d in data]
        ax.semilogx(lrs, vthr, marker=markers.get(name, "o"),
                     color=colors.get(name, "gray"), label=f"{name}", linewidth=2, markersize=8)
        ax.semilogx(lrs, rthr, marker="s",
                     color=colors.get(name, "gray"), label=f"Reno (with {name})",
                     linewidth=1.5, markersize=6, linestyle="--", alpha=0.7)
    ax.set_xlabel("Packet Loss Rate", fontsize=13)
    ax.set_ylabel("Throughput (Mbps)", fontsize=13)
    ax.set_title("Fig 5: Two Coexisting Flows — Hybrid/Delay Variants + Reno", fontsize=14)
    ax.legend(fontsize=9, ncol=2)
    ax.grid(True, which="both", alpha=0.3)
    ax.set_xlim([5e-7, 2e-1])
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "fig5_two_flows_hybrid.png"), dpi=150)
    print("  Saved fig5_two_flows_hybrid.png")
    plt.close(fig)

    # Figure 6: Two flows loss-based
    fig, ax = plt.subplots(figsize=(10, 6))
    for name, data in two_fig6_results.items():
        lrs = [d[0] for d in data]
        vthr = [d[1] for d in data]
        rthr = [d[2] for d in data]
        ax.semilogx(lrs, vthr, marker=markers.get(name, "o"),
                     color=colors.get(name, "gray"), label=f"{name}", linewidth=2, markersize=8)
        ax.semilogx(lrs, rthr, marker="s",
                     color=colors.get(name, "gray"), label=f"Reno (with {name})",
                     linewidth=1.5, markersize=6, linestyle="--", alpha=0.7)
    ax.set_xlabel("Packet Loss Rate", fontsize=13)
    ax.set_ylabel("Throughput (Mbps)", fontsize=13)
    ax.set_title("Fig 6: Two Coexisting Flows — Loss-based Variants + Reno", fontsize=14)
    ax.legend(fontsize=9, ncol=2)
    ax.grid(True, which="both", alpha=0.3)
    ax.set_xlim([5e-7, 2e-1])
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "fig6_two_flows_lossbased.png"), dpi=150)
    print("  Saved fig6_two_flows_lossbased.png")
    plt.close(fig)

    # Figure 7: CWND
    fig, axes = plt.subplots(len(CWND_VARIANTS), 1,
                             figsize=(12, 4 * len(CWND_VARIANTS)), sharex=True)
    if len(CWND_VARIANTS) == 1:
        axes = [axes]
    for idx, variant in enumerate(CWND_VARIANTS):
        name = variant.split("::")[-1]
        ax = axes[idx]
        for fnum, label, style in [(1, f"{name} (variant)", "-"),
                                    (2, "NewReno (coexisting)", "--")]:
            fpath = os.path.join(OUT_DIR, f"cwnd_{name}_flow{fnum}.dat")
            if os.path.exists(fpath) and os.path.getsize(fpath) > 0:
                try:
                    data = np.loadtxt(fpath)
                    if data.ndim == 2 and data.shape[0] > 0:
                        t = data[:, 0]
                        cwnd = data[:, 1] / 1448.0
                        ax.plot(t, cwnd, linestyle=style, linewidth=0.8, label=label, alpha=0.8)
                except Exception as e:
                    print(f"  Warning: {fpath}: {e}")
        ax.set_ylabel("cwnd (segments)", fontsize=11)
        ax.set_title(f"CWND: {name} + NewReno", fontsize=12)
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3)
    axes[-1].set_xlabel("Time (s)", fontsize=11)
    fig.suptitle("Fig 7: Congestion Window Behaviour", fontsize=14, y=1.01)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "fig7_cwnd_behaviour.png"), dpi=150, bbox_inches="tight")
    print("  Saved fig7_cwnd_behaviour.png")
    plt.close(fig)


def main():
    ensure_dir(OUT_DIR)
    print("Pre-building ns-3...")
    subprocess.run("./ns3 build", shell=True, cwd=NS3_DIR, capture_output=True, timeout=300)

    total_runs = (len(SINGLE_FLOW_VARIANTS) * len(LOSS_RATES) +
                  (len(TWO_FLOW_VARIANTS_FIG5) + len(TWO_FLOW_VARIANTS_FIG6)) * len(LOSS_RATES) +
                  len(CWND_VARIANTS))
    print(f"Total simulation runs: {total_runs}")
    print(f"Sim time per run: {SIM_TIME}s (cwnd: 60s)")

    single_results = run_single_flow()
    two_fig5 = run_two_flows(TWO_FLOW_VARIANTS_FIG5, "fig5")
    two_fig6 = run_two_flows(TWO_FLOW_VARIANTS_FIG6, "fig6")
    run_cwnd_traces()
    generate_plots(single_results, two_fig5, two_fig6)

    print(f"\n{'=' * 70}")
    print("ALL EXPERIMENTS COMPLETE")
    print(f"Results and plots in: {OUT_DIR}")
    print("=" * 70)


if __name__ == "__main__":
    main()
