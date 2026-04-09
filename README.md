# TCP Fusion on ns-3.39

This repository contains a course project implementation and evaluation of the
TCP Fusion congestion control algorithm in ns-3.

Most project work is in the `scratch/` folder, plus the TCP Fusion source files
in `src/internet/model/`.

## Project Scope

- Custom TCP implementation:
  - `src/internet/model/tcp-fusion.h`
  - `src/internet/model/tcp-fusion.cc`
- Simulation scenarios and automation scripts:
  - `scratch/tcp-fusion-single-flow.cc`
  - `scratch/tcp-fusion-two-flows.cc`
  - `scratch/tcp-fusion-three-flows.cc`
  - `scratch/tcp-fusion-cwnd.cc`
  - `scratch/run_tcp_fusion_experiments.py`
  - `scratch/run_tcp_fusion_fast.py`
- Outputs and report assets:
  - `scratch/tcp_fusion_results/`
  - `scratch/paper and reports/`

## TCP Fusion Summary

TCP Fusion is a hybrid algorithm that combines:

- Delay-based control (Vegas-style) for proactive congestion handling.
- Bandwidth estimation (Westwood-style) to adapt window increase.
- Reno safety floor to preserve baseline fairness and stability.

Key idea:

- Keep two windows and use the larger one:
  - fusion window (delay/bandwidth aware)
  - reno shadow window (AIMD floor)
- Effective congestion window:

  `cwnd = max(fusion_cwnd, reno_cwnd)`

Main control expressions used in the implementation:

- `diff = cwnd * (RTT - RTTmin) / RTT`
- `alpha = cwnd * Dmin / RTT`
- `Winc = BW / beta`
- Loss reduction:
  `cwnd_new = max((RTTmin/RTT) * cwnd, cwnd/2)`

## Build

From repository root:

```bash
./ns3 configure --enable-examples
./ns3 build
```

## Run Core Experiments

### 1) Single Flow Throughput

```bash
./ns3 run "tcp-fusion-single-flow --tcpVariant=ns3::TcpFusion --lossRate=1e-4 --simTime=60"
```

Output format:

```text
<lossRate> <throughputMbps>
```

### 2) Two Coexisting Flows (Variant + NewReno)

```bash
./ns3 run "tcp-fusion-two-flows --tcpVariant=ns3::TcpFusion --lossRate=1e-4 --simTime=60"
```

Output format:

```text
<lossRate> <variantThroughputMbps> <renoThroughputMbps>
```

### 3) Full Automated Sweep + Plot Generation

```bash
python scratch/run_tcp_fusion_experiments.py
```

Fast version:

```bash
python scratch/run_tcp_fusion_fast.py
```

Generated data/plots are saved in `scratch/tcp_fusion_results/`.

## Important Repository Note (Git Push Friendly)

This project ignores generated build/cache artifacts so the repository remains
clean and pushable.

Current `.gitignore` excludes:

- `build/`
- `cmake-cache/`
- `CMakeFiles/`
- `CMakeCache.txt`
- `*.o`, `*.so`, `*.a`
- `*.log`

This keeps commits focused on source code, experiment scripts, and report files.

## Typical Git Workflow

```bash
git status
git add README.md .gitignore
git add src/internet/model/tcp-fusion.h src/internet/model/tcp-fusion.cc
git add scratch/tcp-fusion-*.cc scratch/run_tcp_fusion_*.py
git commit -m "Add TCP Fusion implementation and experiment suite"
git push
```

## Reference

K. Kaneko, T. Fujikawa, Z. Su, and J. Katto,
"TCP-Fusion: A Hybrid Congestion Control Algorithm for High-speed Networks"
(basis for this implementation and experiment design).
