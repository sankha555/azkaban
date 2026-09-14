# Azkaban: A Zero-Knowledge Abstract Analysis for Neural Networks

This is the artifact for the ACM CCS 2026 paper "Azkaban: A
Zero-Knowledge Abstract Analysis for Neural Networks".

Please read this document in a linear fashion to understand the process to evaluate the artifact.

## A. Layout

```
azkaban/
    |-- data/
        |-- configs/               # experimental configurations by model name
        |-- inputs/                # test inputs by dataset name
        |-- models/                # ONNX model files by model name
        |-- params/                # parsed model parameters by model name
    |-- eran/                    
        |-- ELINA/                   # core ERAN code (functionally equivalent to the version at [https://github.com/eth-sri/eran] with minor debugging changes)
        |-- data/                    # test inputs by dataset name used for ERAN experiments
        |-- python_helpers/          # helper tools (functionally equivalent to the version at [https://github.com/eth-sri/eran] with minor debugging changes)
        |-- requirements.txt         # Python requirements to run ERAN code (Azkaban does not require Python)
        |-- test_deepzono.py         # driver script to run cleartext floating-point certification experiments using ERAN using DeepZono abstract interpreter
    |-- scripts/                 # shell scripts for setup, Docker initialization and running experiments
    |-- src/                     # Azkaban's C++ sources (and vendored emp-toolkit)
        |-- cleartext/               # header files to run cleartext fixed-point certification over F_{2^61-1}
        |-- emp-ot                   # vendored EMP code (functionally equivalent to the version at [https://github.com/emp-toolkit/emp-ot] with minor changes)
        |-- emp-tool                 # vendored EMP code (functionally equivalent to the version at [https://github.com/emp-toolkit/emp-tool] with minor changes)
        |-- emp-zk-math              # vendored EMP code (functionally equivalent to the version at [https://github.com/CryptMatrix/ZKMath] with additions for sound division and truncation)
        |-- emp-zk                   # vendored EMP code (functionally equivalent to the version at [https://github.com/emp-toolkit/emp-zk] with minor changes)
        |-- interval                 # templated header file for sound interval arithmetic
        |-- <header>.h               # header files for each layer of the neural network, zonotope, model, utilities, etc.
    |-- tests/                  
        |-- CMakeLists.txt
        |-- cleartext_accuracy.cpp # computes cleartext certification accuracy for Azkaban. Invoked by scripts/certification_accuracy_experiment.sh
        |-- fairness.cpp           # computes ZK cost for a single run of fairness certification on a model. Invoked by scripts/proof_costs_experiment.sh
        |-- robustness.cpp         # computes ZK cost for a single run of robustness certification on a model. Invoked by scripts/proof_costs_experiment.sh
    |-- .dockerignore
    |-- .gitignore
    |-- CMakeLists.txt
    |-- Dockerfile
    |-- README.md
```

ELINA (under `eran/ELINA`) is compiled in place by `scripts/setup.sh`; its
python bindings load the resulting `.so` files straight out of the source tree.
No Gurobi download or licence is needed.

---

## B. Supported Experiments

### Overview
| Name | Description | Reference Tables in Paper | Interpretation |
| --- | --- | --- | --- |
| `Proof Cost` | Prover and Verifier costs for ZK robustness (time and communication) and ZK Fairness (time) certification using Azkaban. | Table 2 (Column "Proof Cost")<br> Table 3 (Column "Ours") | The numbers show the end-to-end cost for running sound robustness/fairness certification in zero-knowledge for a given model and one input example | 
| `Certification Accuracy` | Cleartext (no-ZK) robustness certification accuracy for Azkaban and ERAN | Table 2 (Column "Accuracy(%)") | The percentage of examples that Azkaban and ERAN certify for adversarial robustness in a given radius (`\delta`) out of the examples that are classified correctly. It is expected that the percentages match (or are extremely close) between the two runs; this shows that Azkaban's fixed-point certification is as useful as ERAN's floating-point certification (and more efficient for the ZKP usecase)|

### Models and Datasets
We support 5 datasets and 12 models for in this experimental suite.
| Task | Datasets | Models |
| --- | --- | --- |
| Robustness Certification | MNIST<br>CIFAR-10 | R1, R2, R3<br>R4, R5, R6|
| Fairness Certification | Adult<br>Credit<br>German | F1, F2<br>F3, F4<br>F5, F6|

- All test examples for the above datasets can be found at: `data/inputs/DATASET_test.txt`
- ERAN experiments use the same test examples as above, but from CSV files in `eran/data/DATASET_test.csv`
- All model parameters (parsed versions, required for Azkaban experiments) can be found at: `data/params/MODEL.txt`
- All model files (ONNX versions, required for ERAN experiments) can be found at: `data/params/MODEL.onnx`


### Experimental Configurations
Each experiment is run using the respective model's config file found at `data/configs/MODEL.json`. A config file is structured as:
```
# JSON
{
  "input_file": "data/inputs/<DATASET>_test.txt",
  "params_file": "data/params/<MODEL>.txt",
  "input_features": ([Integer] Number of input features in the dataset),
  "example_indices": ([Comma-separated list of integers] Indices of examples to be tested from the input file),
  "delta": ([Real] Certification radius),
  "sensitive_attribute": ([Integer] Only found in F1-F6.json; index of sensitive attribute for fairness certification),
  "sensitive_attribute_values": ([Pair of Reals] Only found in F1-F6.json; value 1 and value 2 for the sensitive attribute),
  "architecture": ([List of Maps]; Each map represents a particular layer of the neural network)
}
```
    

### Outputs
All experiment outputs are written to the directory `results/`. This directory is auto-created (if does not exist already) when running the experiment scripts (see running instructions below).

- At the end of successful execution of each script above (or just running `scripts/run_all.sh`), results are summarized in a table which can be found at:
```
# for Proof Cost experiments 
results/robustness_costs.txt (mimics Table 2 structure)
results/fairness_costs.txt (mimics Table 3 structure)
```
```
# for Certification Accuracy experiments 
results/robustness_certification_accuracy.txt (mimics Table 2 structure)
```

- Per-run logs are maintained alongside the tables in folders named after the model names:
```
# for Proof Cost experiments
results/proof_cost/<MODEL>/prover.log
results/proof_cost/<MODEL>/verifier.log
```
```
# for Certification Accuracy experiments
results/certification_accuracy/<MODEL>/azkaban.txt
results/certification_accuracy/<MODEL>/eran.txt
```

---

## C. System Requirements

### Recommended Host
NOTE: Docker is one possible way to evaluate the artifact. The other one is manually installing and running the artifact, for which the Docker specific requirements can be ignored.
| Requirement | Minimum |
|---|---:|
| OS | Ubuntu 22.04+ or another modern Linux with Docker support |
| CPU architecture | x86-64 |
| CPU features | AES-NI, AVX2, RDSEED |
| RAM | 16 GB |
| Cores | 4 logical cores |
| Storage | 40 GB free |
| Docker | Docker Engine with NET_ADMIN capability enabled (for throttling network bandwidth in the container). Docker Engine must be installed from the official Docker repository (not a snap-based install) |

### Notes
- The artifact Docker image is built from Ubuntu 24.04 in the Dockerfile.
- The artifact is explicitly compiled for a portable x86-64 baseline and requires x86-64 CPU features such as AES-NI, AVX2, and RDSEED.
- The proof-cost experiment uses `tc` to shape loopback traffic and therefore requires container privileges via `--cap-add=NET_ADMIN`. 
- Older Ubuntu versions may work if the host kernel and Docker runtime are compatible, but Ubuntu 24.04 LTS is the recommended and best-supported option.

---

## D.1. Running Manually
### 1. Setup
First run `scripts/setup.sh`. This scripts installs dependencies, builds executables and sets up virtual environments. It should take roughly ~20 mins to run.

   **NOTE: Make sure you have `sudo` access.**

```bash
bash scripts/setup.sh        # ~20 min: system packages, emp-toolkit, ELINA,
                             # the C++ binaries, the python venv, loopback
                             # shaping. Asks for sudo once, up front.
```

`scripts/setup.sh` ends with a verification block and prints what to run next.
It is safe to re-run: every step it has already done is skipped. Nothing else
needs to be installed or configured by hand.
<br>
### 2. Running experiments
- **Both Experiments (Least Effort Option)**: `scripts/run_all.sh`
  - Quick Usage (~15-20 mins)
  ```bash
  scripts/run_all.sh --quick # short version (to verify that all code is functional).
                             # 1. Proof Costs experiment run for models R1 and F1.
                             # 2. Certification Accuracy experiment run for models R1 and R2.
  ```

  - Full Usage (~10 hours)
  ```bash
  scripts/run_all.sh --all   # full version (to reproduce results);
                             # 1. Proof Costs experiment run for models R1-R6 and F1-F6.
                             # 2. Certification Accuracy experiment run for models R1-R6.
  ```
  
- **Proof Cost**: `scripts/proof_costs_experiment.sh`
  - Example Usage 1: `scripts/proof_costs_experiment.sh` [Runs all models R1-R6 and F1-F6]
  - Example Usage 2: `scripts/proof_costs_experiment.sh R1 F1` [Runs models R1 and F1]

- **Certification Accuracy**: `scripts/certification_accuracy_experiment.sh`
  - Example Usage 1: `scripts/certification_accuracy_experiment.sh` [Runs all models R1-R6]
  - Example Usage 2: `scripts/certification_accuracy_experiment.sh R1 R3` [Runs models R1 and R3]
  - Note: This experiment is only supported for models `R1-R6`. Do not run it on models F1-F6.


### 3. Verifying Outputs
Output tables can be found at `results/`. Please reference these tables with Table 2 and Table 3 from the paper to check for result reproduction.

---

## D.2. Running using Docker
To run the experiments using Docker, please first read the instructions end-to-end below, then run the required commands. A troubleshooting guide 
has been provided in [DOCKER_TROUBLESHOOTING.md](DOCKER_TROUBLESHOOTING.md).

The image is `linux/amd64` only. On ARM see [DOCKER_TROUBLESHOOTING.md](DOCKER_TROUBLESHOOTING.md#[§F](#f-non-x86-64-host)) .

### 1. Install

First run 
```bash
./scripts/install-docker-and-pull.sh
```

This installs Docker from the official repository (skipped if already present),
adds you to the `docker` group, pulls the Docker image (`sankha555/azkaban-ccs:ccs`), and checks
whether your host can give containers a private network namespace.

**Read the last few lines of its output.** It prints the exact `docker run`
command for your machine, including a `--network host` flag if your host needs
it (for troubleshooting, see [DOCKER_TROUBLESHOOTING.md](DOCKER_TROUBLESHOOTING.md)[§B](#b-error-during-container-init--open-sysctl--permission-denied)).

If it just added you to the `docker` group, that group is not active in your
current shell. Either log out and back in — **a new terminal tab in an
already-running editor is not enough** — or prefix commands with
`sg docker -c '...'` as shown below.


### 2. Run Experiments

```bash
docker run --rm --cap-add=NET_ADMIN \
    -v "$PWD/results:/artifact/results" \
    sankha555/azkaban-ccs:ccs --quick
```

Add `--network host` if the install script told you to.

- `--quick` — a representative subset (proof costs: `R1 F1`; certification
  accuracy: `R1 R2`). Recommended for functional review.
- Replace `--quick` by `--all` for full experimental suite run to reproduce results (`R1`–`R6`, `F1`–`F6`). **Takes hours.**

`--cap-add=NET_ADMIN` is **required**: the proof-cost experiment throttles the localhost network bandwidth to 1 Gbit with `tc netem`, matching the paper's setup. The run
aborts if it is unable to throttle the bandwidth to 1Gbit.

If you are not yet in the `docker` group, you have to prefix the commands with `sg docker -c` below:

```bash
sg docker -c 'docker run --rm --cap-add=NET_ADMIN \
    -v "$PWD/results:/artifact/results" \
    sankha555/azkaban-ccs:ccs --quick'
```


### 3. Results

Written to `./results/`:

```
results/robustness_costs.txt                          # summary table of proof costs, robustness
results/fairness_costs.txt                            # summary table proof costs, fairness 
results/robustness_certification_accuracy.txt         # summary table of certification accuracy experiment
results/proof_cost/<MODEL>/prover.log                 # per-experiment proof cost, prover-side
results/proof_cost/<MODEL>/verifier.log               # per-experiment proof cost, verifier-side
results/certification_accuracy/<MODEL>/azkaban.txt    # per-experiment certification accuracy, fixed-point using Azkaban
results/certification_accuracy/<MODEL>/eran.txt       # per-experiment certification accuracy, floating-point using ERAN
```

Each cost table states its network condition in the header; it should read
`(measured over a 1gbit loopback link, shaped with tc netem)`.

A non-zero exit code names the failed experiment; per-model logs are under
`results/`.

---

## E. Acknowledgements
We used Claude Code towards preparing this artifact, specifically for scripting and code polishing. All AI-generated content has been human-validated. We thank the maintainers of `emp-toolkit`, `eth-sri`, `CryptMatrix/ZKMath` and `nlohmann/json` for open-sourcing their work, all of which from this artifact draws and builds upon.

Please direct all communication to Sankha Das at [sdas435@gatech.edu](mailto:sdas435@gatech.edu)

