#!/bin/bash

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

SKIP_SUDOERS=0
[ "${1:-}" = "--no-sudoers" ] && SKIP_SUDOERS=1

echo "==================================="
echo " Part 1: C++ build"
echo "==================================="

echo "Installing system packages (needs sudo)..."
sudo apt-get update
sudo apt-get install -y build-essential cmake git wget \
    libssl-dev libgmp-dev python3 python3-pip python3-venv \
    iperf3 iproute2

if [ ! -f /usr/local/include/emp-ot/emp-ot.h ]; then
    echo "Installing emp-toolkit..."
    mkdir -p deps
    cd deps
    wget -q https://raw.githubusercontent.com/emp-toolkit/emp-readme/master/scripts/install.py
    python3 install.py --deps --tool --ot
    cd "$ROOT"
else
    echo "emp-toolkit already installed, skipping."
fi

echo "Building the project..."
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd "$ROOT"

echo "Azkaban build completed. Binaries are in build/"

echo "==================================="
echo " Part 2: Python setup"
echo "==================================="

if [ ! -d venv ]; then
    echo "Making virtualenv..."
    python3 -m venv venv
else
    echo "venv already exists, reusing it."
fi

source venv/bin/activate

echo "Installing python packages (this takes a while)..."
pip install --upgrade pip
pip install -r eran/requirements.txt

export PYTHONPATH="$ROOT:$PYTHONPATH"

echo "ERAN setup completed."

echo "==================================="
echo " Part 3: One-time privileged setup"
echo "==================================="

TC_BIN=$(command -v tc || echo /usr/sbin/tc)
SUDOERS_FILE=/etc/sudoers.d/azkaban-experiments

echo "Shaping the loopback link to 1gbit (needs sudo)..."
if sudo tc qdisc show dev lo | grep -q netem; then
    sudo tc qdisc change dev lo root netem rate 1gbit
else
    sudo tc qdisc add dev lo root netem rate 1gbit
fi
sudo tc qdisc show dev lo

if [ "$SKIP_SUDOERS" = "1" ]; then
    echo "Skipping the passwordless-tc rule (--no-sudoers given)."
    echo "The experiment scripts will ask for your password when they re-shape lo."
elif sudo -n true 2>/dev/null && [ -f "$SUDOERS_FILE" ]; then
    echo "Passwordless tc already configured ($SUDOERS_FILE)."
else
    echo "Allowing passwordless '$TC_BIN' for user $(id -un)..."
    TMP_SUDOERS=$(mktemp)
    printf '%s ALL=(root) NOPASSWD: %s\n' "$(id -un)" "$TC_BIN" > "$TMP_SUDOERS"
    if sudo visudo -cf "$TMP_SUDOERS" > /dev/null; then
        sudo install -m 0440 -o root -g root "$TMP_SUDOERS" "$SUDOERS_FILE"
        echo "Wrote $SUDOERS_FILE"
        echo "  -> only '$TC_BIN' is passwordless, only for $(id -un)."
        echo "  -> undo any time with: sudo rm $SUDOERS_FILE"
    else
        echo "WARNING: generated sudoers rule was rejected by visudo, not installing it."
    fi
    rm -f "$TMP_SUDOERS"
fi

echo ""
echo "All done!"
echo "Run 'source venv/bin/activate' to use the python environment."
echo "No further step needs sudo: run scripts/proof_costs_experiment.sh and"
echo "scripts/certification_accuracy_experiment.sh directly."
