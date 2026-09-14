# AZKABAN: A Zero-Knowledge Abstract Analysis for Neural Networks

This is the artifact for the CCS 2026 submission of the paper "AZKABAN: A
Zero-Knowledge Abstract Analysis for Neural Networks".

## Quick start

Two commands. The first one does the whole setup, the second runs the
experiments.

```bash
bash scripts/setup.sh        # ~20 min: system packages, emp-toolkit, ELINA,
                             # the C++ binaries, the python venv, loopback
                             # shaping. Asks for sudo once, up front.

scripts/run_all.sh --quick   # both experiments, short version
scripts/run_all.sh --all     # both experiments, full sweep
```

`scripts/setup.sh` ends with a verification block and prints what to run next.
It is safe to re-run: every step it has already done is skipped. Nothing else
needs to be installed or configured by hand.

Tested on Ubuntu 24.04 (x86-64). A CPU with AES-NI, PCLMUL, AVX2 and RDSEED is
required -- Intel Broadwell / AMD Excavator (2015) or newer.

## Running in Docker instead

```bash
docker build --platform linux/amd64 -t azkaban:ccs .
docker run --rm --cap-add=NET_ADMIN -v "$PWD/results:/artifact/results" \
    azkaban:ccs --quick
```

`--cap-add=NET_ADMIN` is required: the proof-cost experiment shapes the
container's own loopback to 1 Gbit with `tc`.

## The experiments

| Script | What it measures | Output table |
| --- | --- | --- |
| `scripts/proof_costs_experiment.sh` | prover/verifier time and communication | `results/robustness_costs.txt`, `results/fairness_costs.txt` |
| `scripts/certification_accuracy_experiment.sh` | Azkaban's fixed-point zonotope vs. ERAN's floating-point DeepZono | `results/robustness_certification_accuracy.txt` |

`scripts/run_all.sh` runs both. Either script also takes an explicit model list,
e.g. `scripts/certification_accuracy_experiment.sh R1 R3`.

Per-run logs are kept alongside the tables:

```
results/proof_cost/<MODEL>/{prover,verifier}.log
results/certification_accuracy/<MODEL>/{azkaban,eran}.txt
```

## Layout

```
data/configs/<MODEL>.json   experiment configuration (delta, example indices, ...)
data/models/<MODEL>.onnx    the networks: R* robustness, F* fairness
src/                        Azkaban's C++ sources (and vendored emp-toolkit)
eran/                       the ERAN baseline: test_deepzono.py + ELINA + helpers
scripts/                    setup and experiment drivers
```

ELINA (under `eran/ELINA`) is compiled in place by `scripts/setup.sh`; its
python bindings load the resulting `.so` files straight out of the source tree.
It is built with DeepPoly and fconv but deliberately without Gurobi, which only
guards a spatial-constraint path that L-infinity certification never reaches --
so no Gurobi download or licence is needed.
