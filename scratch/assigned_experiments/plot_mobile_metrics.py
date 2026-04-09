#!/usr/bin/env python3
"""
Plot assignment graphs from mobile_raw_results.csv.
Generates one figure per (scenario, metric, varied parameter).
"""

import csv
import json
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
CFG_PATH = Path(__file__).resolve().parent / "config_mobile.json"

METRICS = [
    ("throughputMbps", "Network Throughput (Mbps)", "throughput"),
    ("avgDelayMs", "End-to-End Delay (ms)", "delay"),
    ("pdr", "Packet Delivery Ratio", "pdr"),
    ("dropRatio", "Packet Drop Ratio", "drop"),
    ("energyJ", "Energy Consumption (J)", "energy"),
]

COLORS = {
    "ns3::TcpFusion": "#d62728",
    "ns3::TcpNewReno": "#1f77b4",
    "ns3::TcpCubic": "#2ca02c",
}

LABELS = {
    "ns3::TcpFusion": "TCP-Fusion",
    "ns3::TcpNewReno": "TCP NewReno",
    "ns3::TcpCubic": "TCP CUBIC",
}


def load_cfg():
    with open(CFG_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def read_rows(csv_path):
    rows = []
    with open(csv_path, "r", encoding="utf-8") as f:
        r = csv.DictReader(f)
        for row in r:
            row["nNodes"] = int(float(row["nNodes"]))
            row["nFlows"] = int(float(row["nFlows"]))
            row["pktPerSec"] = int(float(row["pktPerSec"]))
            row["speed"] = float(row["speed"])
            row["simTime"] = float(row["simTime"])
            row["seed"] = int(float(row["seed"]))
            for m, _, _ in METRICS:
                row[m] = float(row[m])
            rows.append(row)
    return rows


def mean(vals):
    return sum(vals) / len(vals) if vals else 0.0


def filter_one_factor_rows(rows, scenario, varied_param, x_value, baseline):
    out = []
    for row in rows:
        if row["scenario"] != scenario:
            continue
        if float(row[varied_param]) != float(x_value):
            continue
        ok = True
        for p in ["nNodes", "nFlows", "pktPerSec", "speed"]:
            if p == varied_param:
                continue
            if float(row[p]) != float(baseline[p]):
                ok = False
                break
        if ok:
            out.append(row)
    return out


def plot_all():
    cfg = load_cfg()
    csv_path = ROOT / cfg["outputCsv"]
    plots_dir = ROOT / cfg["outputPlotsDir"]
    plots_dir.mkdir(parents=True, exist_ok=True)

    rows = read_rows(csv_path)

    scenarios = list(cfg["scenarioScripts"].keys())
    variants = cfg["variants"]
    baseline = cfg["baseline"]

    for scenario in scenarios:
        for varied_param, x_values in cfg["sweeps"].items():
            for metric_key, metric_label, metric_slug in METRICS:
                plt.figure(figsize=(9, 5.5))

                for v in variants:
                    y = []
                    for xv in x_values:
                        sub = filter_one_factor_rows(rows, scenario, varied_param, xv, baseline)
                        sub = [r for r in sub if r["tcpVariant"] == v]
                        y.append(mean([r[metric_key] for r in sub]))

                    plt.plot(
                        x_values,
                        y,
                        marker="o",
                        linewidth=2,
                        color=COLORS.get(v, "gray"),
                        label=LABELS.get(v, v),
                    )

                plt.xlabel(varied_param)
                plt.ylabel(metric_label)
                plt.title(f"{scenario}: {metric_label} vs {varied_param}")
                plt.grid(True, linestyle="--", alpha=0.3)
                plt.legend()
                plt.tight_layout()

                out_name = f"{scenario}_{metric_slug}_vs_{varied_param}.png"
                plt.savefig(plots_dir / out_name, dpi=150)
                plt.close()
                print(f"Generated: {plots_dir / out_name}")


if __name__ == "__main__":
    plot_all()
