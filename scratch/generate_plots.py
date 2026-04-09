#!/usr/bin/env python3
"""
generate_plots.py — Standalone plotting script for TCP-Fusion experiments.
Reads .dat files from tcp_fusion_results/ and produces 4  PNG figures.

Run:  python3 scratch/generate_plots.py
"""

import os
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RESULTS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tcp_fusion_results")

# ── Styling ──────────────────────────────────────────────────────────────────
COLORS = {
    "TcpFusion":        "#d62728",   # bold red
    "TcpNewReno":       "#1f77b4",   # blue
    "TcpCubic":         "#2ca02c",   # green
    "TcpBic":           "#9467bd",   # purple
    "TcpHighSpeed":     "#ff7f0e",   # orange
    "TcpWestwoodPlus":  "#e377c2",   # pink
    "TcpVegas":         "#8c564b",   # brown
}
MARKERS = {
    "TcpFusion": "o", "TcpNewReno": "s", "TcpCubic": "^",
    "TcpBic": "D", "TcpHighSpeed": "v", "TcpWestwoodPlus": "*",
    "TcpVegas": "p",
}
LABELS = {
    "TcpFusion": "TCP-Fusion", "TcpNewReno": "TCP NewReno",
    "TcpCubic": "CUBIC", "TcpBic": "BIC",
    "TcpHighSpeed": "HighSpeed TCP", "TcpWestwoodPlus": "Westwood+",
    "TcpVegas": "TCP Vegas",
}

plt.rcParams.update({
    "font.size": 12, "axes.labelsize": 13, "axes.titlesize": 14,
    "legend.fontsize": 10, "figure.dpi": 150,
    "grid.alpha": 0.3, "grid.linestyle": "--",
})


def load_table(fname):
    """Load a whitespace-separated .dat file, skipping comment lines."""
    path = os.path.join(RESULTS_DIR, fname)
    if not os.path.exists(path):
        print(f"  !! Missing: {path}")
        return None, None
    with open(path) as f:
        header = None
        for line in f:
            if line.startswith("#"):
                header = line.lstrip("# ").strip().split()
                continue
        f.seek(0)
    data = np.loadtxt(path, comments="#")
    return header, data


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 4 — Single flow throughput vs loss rate
# ═══════════════════════════════════════════════════════════════════════════════
def plot_fig4():
    header, data = load_table("single_flow_throughput.dat")
    if data is None:
        return
    loss_rates = data[:, 0]
    fig, ax = plt.subplots(figsize=(10, 6))
    for i, name in enumerate(header[1:], 1):
        ax.semilogx(loss_rates, data[:, i],
                     marker=MARKERS.get(name, "o"),
                     color=COLORS.get(name, "gray"),
                     label=LABELS.get(name, name),
                     linewidth=2.2, markersize=8)
    ax.set_xlabel("Packet Loss Rate")
    ax.set_ylabel("Throughput (Mbps)")
    ax.set_title("Fig 4 — Throughput of Single Flow vs. Loss Rate\n(100 Mbps bottleneck, 40 ms RTT)")
    ax.legend(loc="upper right")
    ax.grid(True, which="both")
    ax.set_xlim([5e-7, 2e-1])
    ax.set_ylim(bottom=0)
    fig.tight_layout()
    out = os.path.join(RESULTS_DIR, "fig4_single_flow_throughput.png")
    fig.savefig(out)
    print(f"  ✓ {out}")
    plt.close(fig)


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 5 — Two coexisting flows: hybrid/delay variants + Reno
# ═══════════════════════════════════════════════════════════════════════════════
def plot_fig5():
    header, data = load_table("two_flows_fig5.dat")
    if data is None:
        return
    loss_rates = data[:, 0]

    # Parse columns pairwise: variant_thr, Reno_with_variant
    fig, ax = plt.subplots(figsize=(10, 6))
    col = 1
    while col < len(header):
        vname = header[col].replace("_thr", "")
        vthr = data[:, col]
        rthr = data[:, col + 1]
        c = COLORS.get(vname, "gray")
        ax.semilogx(loss_rates, vthr,
                     marker=MARKERS.get(vname, "o"), color=c,
                     label=LABELS.get(vname, vname),
                     linewidth=2.2, markersize=8)
        ax.semilogx(loss_rates, rthr,
                     marker="s", color=c,
                     label=f"Reno (with {LABELS.get(vname, vname)})",
                     linewidth=1.5, markersize=6, linestyle="--", alpha=0.7)
        col += 2

    ax.set_xlabel("Packet Loss Rate")
    ax.set_ylabel("Throughput (Mbps)")
    ax.set_title("Fig 5 — Throughput of Two Coexisting Flows\n(Hybrid/Delay Variants + Reno, 100 Mbps)")
    ax.legend(loc="upper right", ncol=2, fontsize=9)
    ax.grid(True, which="both")
    ax.set_xlim([5e-7, 2e-1])
    ax.set_ylim(bottom=0)
    fig.tight_layout()
    out = os.path.join(RESULTS_DIR, "fig5_two_flows_hybrid.png")
    fig.savefig(out)
    print(f"  ✓ {out}")
    plt.close(fig)


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 6 — Two coexisting flows: loss-based variants + Reno
# ═══════════════════════════════════════════════════════════════════════════════
def plot_fig6():
    header, data = load_table("two_flows_fig6.dat")
    if data is None:
        return
    loss_rates = data[:, 0]

    fig, ax = plt.subplots(figsize=(10, 6))

    # --- Also load TCP-Fusion data from fig5 so it appears for comparison ---
    h5, d5 = load_table("two_flows_fig5.dat")
    if d5 is not None and h5 is not None:
        # Find TcpFusion columns in fig5 data
        for ci in range(1, len(h5)):
            if "TcpFusion_thr" == h5[ci]:
                # Use the same loss-rate x-axis from fig5
                lr5 = d5[:, 0]
                vthr5 = d5[:, ci]
                rthr5 = d5[:, ci + 1]
                c = COLORS.get("TcpFusion", "red")
                ax.semilogx(lr5, vthr5,
                             marker=MARKERS.get("TcpFusion", "o"), color=c,
                             label=LABELS.get("TcpFusion", "TCP-Fusion"),
                             linewidth=2.2, markersize=8)
                ax.semilogx(lr5, rthr5,
                             marker="s", color=c,
                             label=f"Reno (with {LABELS.get('TcpFusion', 'TCP-Fusion')})",
                             linewidth=1.5, markersize=6, linestyle="--", alpha=0.7)
                break

    # --- Original loss-based variants from fig6 data ---
    col = 1
    while col < len(header):
        vname = header[col].replace("_thr", "")
        vthr = data[:, col]
        rthr = data[:, col + 1]
        c = COLORS.get(vname, "gray")
        ax.semilogx(loss_rates, vthr,
                     marker=MARKERS.get(vname, "o"), color=c,
                     label=LABELS.get(vname, vname),
                     linewidth=2.2, markersize=8)
        ax.semilogx(loss_rates, rthr,
                     marker="s", color=c,
                     label=f"Reno (with {LABELS.get(vname, vname)})",
                     linewidth=1.5, markersize=6, linestyle="--", alpha=0.7)
        col += 2

    ax.set_xlabel("Packet Loss Rate")
    ax.set_ylabel("Throughput (Mbps)")
    ax.set_title("Fig 6 — Throughput of Two Coexisting Flows\n(Loss-based Variants + TCP-Fusion + Reno, 100 Mbps)")
    ax.legend(loc="upper right", ncol=2, fontsize=9)
    ax.grid(True, which="both")
    ax.set_xlim([5e-7, 2e-1])
    ax.set_ylim(bottom=0)
    fig.tight_layout()
    out = os.path.join(RESULTS_DIR, "fig6_two_flows_lossbased.png")
    fig.savefig(out)
    print(f"  ✓ {out}")
    plt.close(fig)


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 7 — Congestion window behaviour
# ═══════════════════════════════════════════════════════════════════════════════
def plot_fig7():
    variants = ["TcpFusion", "TcpCubic", "TcpNewReno"]
    n = len(variants)
    fig, axes = plt.subplots(n, 1, figsize=(14, 4.2 * n), sharex=True)
    if n == 1:
        axes = [axes]

    for idx, name in enumerate(variants):
        ax = axes[idx]
        for fnum, flabel, ls, alpha in [
            (1, f"{LABELS.get(name, name)} (variant flow)", "-", 0.9),
            (2, "NewReno (coexisting flow)", "--", 0.7),
        ]:
            fpath = os.path.join(RESULTS_DIR, f"cwnd_{name}_flow{fnum}.dat")
            if not os.path.exists(fpath) or os.path.getsize(fpath) == 0:
                print(f"  !! Missing/empty: {fpath}")
                continue
            try:
                d = np.loadtxt(fpath)
                if d.ndim < 2 or d.shape[0] == 0:
                    continue
                t = d[:, 0]
                cwnd_seg = d[:, 1] / 1448.0  # convert bytes → segments
                # Subsample for cleaner plot if too many points
                step = max(1, len(t) // 5000)
                ax.plot(t[::step], cwnd_seg[::step],
                        linestyle=ls, linewidth=0.8, label=flabel,
                        alpha=alpha, color=COLORS.get(name if fnum == 1 else "TcpNewReno", "gray"))
            except Exception as e:
                print(f"  !! Error loading {fpath}: {e}")

        ax.set_ylabel("cwnd (segments)")
        ax.set_title(f"{LABELS.get(name, name)} + NewReno coexisting (loss = 10⁻⁵)")
        ax.legend(loc="upper right", fontsize=9)
        ax.grid(True)

    axes[-1].set_xlabel("Time (s)")
    fig.suptitle("Fig 7 — Congestion Window Behaviour", fontsize=15, y=1.01)
    fig.tight_layout()
    out = os.path.join(RESULTS_DIR, "fig7_cwnd_behaviour.png")
    fig.savefig(out, bbox_inches="tight")
    print(f"  ✓ {out}")
    plt.close(fig)


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 8 — Three identical flows: per-flow throughput fairness
# ═══════════════════════════════════════════════════════════════════════════════
def plot_fig8():
    path = os.path.join(RESULTS_DIR, "three_flows.dat")
    if not os.path.exists(path):
        print(f"  !! Missing: {path}")
        return

    fig, ax = plt.subplots(figsize=(12, 6))

    # Load manually — first col is variant name (string), rest are floats
    variants = []
    flows = []  # list of [f1, f2, f3]
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.strip().split()
            variants.append(parts[0])
            flows.append([float(x) for x in parts[1:]])

    flows = np.array(flows)
    n = len(variants)
    x = np.arange(n)
    width = 0.22

    bars1 = ax.bar(x - width, flows[:, 0], width, label="Flow 1",
                   color="#1f77b4", edgecolor="white", linewidth=0.8)
    bars2 = ax.bar(x,         flows[:, 1], width, label="Flow 2",
                   color="#ff7f0e", edgecolor="white", linewidth=0.8)
    bars3 = ax.bar(x + width, flows[:, 2], width, label="Flow 3",
                   color="#2ca02c", edgecolor="white", linewidth=0.8)

    # Annotate bar values
    for bars in [bars1, bars2, bars3]:
        for bar in bars:
            h = bar.get_height()
            ax.annotate(f"{h:.1f}",
                        xy=(bar.get_x() + bar.get_width() / 2, h),
                        xytext=(0, 3), textcoords="offset points",
                        ha="center", va="bottom", fontsize=8)

    # Fair-share line
    ax.axhline(y=100.0 / 3, color="red", linestyle=":", linewidth=1.5,
               label="Fair share (33.3 Mbps)", alpha=0.7)

    pretty = [LABELS.get(v, v) for v in variants]
    ax.set_xticks(x)
    ax.set_xticklabels(pretty, rotation=20, ha="right")
    ax.set_ylabel("Throughput (Mbps)")
    ax.set_title("Fig 8 — Throughput Fairness of 3 Identical Flows\n(100 Mbps bottleneck, 40 ms RTT, no random loss)")
    ax.legend(loc="upper right")
    ax.grid(True, axis="y")
    ax.set_ylim(bottom=0, top=max(flows.max() * 1.15, 50))
    fig.tight_layout()
    out = os.path.join(RESULTS_DIR, "fig8_three_flows_fairness.png")
    fig.savefig(out)
    print(f"  ✓ {out}")
    plt.close(fig)


# ═══════════════════════════════════════════════════════════════════════════════
def main():
    print(f"Results dir: {RESULTS_DIR}")
    print()
    print("Generating Figure 4 (single flow throughput)...")
    plot_fig4()
    print("Generating Figure 5 (two flows – hybrid/delay)...")
    plot_fig5()
    print("Generating Figure 6 (two flows – loss-based)...")
    plot_fig6()
    print("Generating Figure 7 (cwnd behaviour)...")
    plot_fig7()
    print("Generating Figure 8 (three-flow fairness)...")
    plot_fig8()
    print()
    print("All plots saved to:", RESULTS_DIR)


if __name__ == "__main__":
    main()
