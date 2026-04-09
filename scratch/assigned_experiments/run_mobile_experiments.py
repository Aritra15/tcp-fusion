#!/usr/bin/env python3
"""
Run assigned mobile experiments for:
- wifi-mobile (802.11 + mobility)
- lrwpan-mobile (802.15.4 + mobility)

One-factor-at-a-time sweeps around baseline.
"""

import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CFG_PATH = Path(__file__).resolve().parent / "config_mobile.json"


def load_cfg():
    with open(CFG_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def run_cmd(cmd):
    cp = subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if cp.returncode != 0:
        raise RuntimeError(f"Command failed: {' '.join(cmd)}\nSTDOUT:\n{cp.stdout}\nSTDERR:\n{cp.stderr}")
    return cp.stdout


def ensure_parent(path_str):
    p = ROOT / path_str
    p.parent.mkdir(parents=True, exist_ok=True)
    return p


def main():
    cfg = load_cfg()

    output_csv = ensure_parent(cfg["outputCsv"])
    if output_csv.exists():
        output_csv.unlink()

    print("Building ns-3 once...")
    run_cmd(["./ns3", "build"])

    baseline = cfg["baseline"]
    sweeps = cfg["sweeps"]

    total_runs = 0
    scenarios = list(cfg["scenarioScripts"].keys())
    variants = cfg["variants"]
    seeds = cfg["seeds"]

    for scenario in scenarios:
        for varied_param, values in sweeps.items():
            for value in values:
                for variant in variants:
                    for seed in seeds:
                        params = dict(baseline)
                        params[varied_param] = value

                        script = cfg["scenarioScripts"][scenario]
                        cmdarg = (
                            f"{script} "
                            f"--tcpVariant={variant} "
                            f"--nNodes={params['nNodes']} "
                            f"--nFlows={params['nFlows']} "
                            f"--pktPerSec={params['pktPerSec']} "
                            f"--speed={params['speed']} "
                            f"--simTime={cfg['simTime']} "
                            f"--seed={seed} "
                            f"--outFile={cfg['outputCsv']} "
                            f"--append=true"
                        )

                        print(
                            f"[{scenario}] var={varied_param}:{value} variant={variant} seed={seed}"
                        )
                        run_cmd(["./ns3", "run", cmdarg, "--no-build"])
                        total_runs += 1

    print(f"Done. Total runs: {total_runs}")
    print(f"CSV: {output_csv}")


if __name__ == "__main__":
    main()
