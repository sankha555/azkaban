#!/bin/bash

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

BANDWIDTH="1gbit"

echo "=== Shaping the loopback link to $BANDWIDTH ==="

run_tc() {
    if [ "$(id -u)" -eq 0 ]; then
        tc "$@"
    elif sudo -n true 2>/dev/null; then
        sudo -n tc "$@"
    else
        echo "    (asking for sudo -- run scripts/setup.sh once to avoid this)"
        sudo tc "$@"
    fi
}

# 'replace' is idempotent: it adds the qdisc when lo is unshaped and rewrites it
# when scripts/setup.sh or an earlier run already left one behind.
if run_tc qdisc replace dev lo root netem rate "$BANDWIDTH"; then
    echo "lo shaped to $BANDWIDTH."
else
    echo "" >&2
    echo "ERROR: could not shape lo to $BANDWIDTH." >&2
    echo "  In Docker this needs --cap-add=NET_ADMIN; on a bare host it needs" >&2
    echo "  root, or the passwordless-tc rule that scripts/setup.sh installs." >&2
    echo "" >&2
    echo "  Refusing to continue: the proof costs would be measured over an" >&2
    echo "  unshaped link and would not be comparable to the reported numbers." >&2
    exit 1
fi

tc qdisc show dev lo

echo ""
echo "=== Building the C++ executables ==="
mkdir -p build
cd build
cmake .. > /dev/null
make -j$(nproc) robustness fairness
cd "$ROOT"
echo "build done."

mkdir -p results/proof_cost

QUICK_MODELS="R1 F1"
ALL_MODELS="R1 R2 R3 R4 R5 R6 F1 F2 F3 F4 F5 F6"

case "${1:-}" in
    --quick) MODELS="$QUICK_MODELS"; echo "quick mode: $MODELS" ;;
    --all)   MODELS="$ALL_MODELS" ;;
    "")      MODELS="$ALL_MODELS"
             echo "no mode given, running the full sweep (use --quick for the short version)" ;;
    -*)      echo "unknown option: $1"; echo "usage: $0 [--quick | --all | MODEL ...]"; exit 1 ;;
    *)       MODELS="$@" ;;
esac

echo ""
echo "=== Running proof cost measurement experiments for: $MODELS ==="

ROBUST_ROWS=$(mktemp)
FAIR_ROWS=$(mktemp)

COUNT=0
TOTAL=$(echo $MODELS | wc -w)

for MODEL in $MODELS; do
    COUNT=$((COUNT + 1))

    case "$MODEL" in
        R*) EXE=build/bin/robustness ;;
        F*) EXE=build/bin/fairness ;;
        *)  echo "skipping $MODEL: not an R or F model"; continue ;;
    esac

    if [ ! -f "data/configs/$MODEL.json" ]; then
        echo "skipping $MODEL: data/configs/$MODEL.json does not exist"
        continue
    fi

    OUTDIR="results/proof_cost/$MODEL"
    mkdir -p "$OUTDIR"

    echo ""
    echo ">>> [$COUNT/$TOTAL] Running $MODEL ..."

    "$EXE" 1 "$MODEL" > "$OUTDIR/prover.log" 2>&1 &
    PROVER_PID=$!
    sleep 1
    "$EXE" 2 "$MODEL" > "$OUTDIR/verifier.log" 2>&1 &
    VERIFIER_PID=$!

    wait $PROVER_PID   || echo "    prover exited with an error, see $OUTDIR/prover.log"
    wait $VERIFIER_PID || echo "    verifier exited with an error, see $OUTDIR/verifier.log"

    DATASET=$(grep -m1 "^Dataset:" "$OUTDIR/verifier.log" | awk '{print $2}')
    NEURONS=$(grep -m1 "^Neurons:" "$OUTDIR/verifier.log" | awk '{print $2}')
    DELTA=$(grep -m1   "^Delta"    "$OUTDIR/verifier.log" | awk '{print $3}')

    PTIME=$(grep -m1 "End-to-End Proof Time:" "$OUTDIR/prover.log"   | awk '{print $4}')
    VTIME=$(grep -m1 "End-to-End Proof Time:" "$OUTDIR/verifier.log" | awk '{print $4}')

    PCOMM=$(grep -m1 "End-to-End Communication" "$OUTDIR/prover.log"   | awk '{print $(NF-1)}')
    VCOMM=$(grep -m1 "End-to-End Communication" "$OUTDIR/verifier.log" | awk '{print $(NF-1)}')
    COMM=$(awk -v p="${PCOMM:-0}" -v v="${VCOMM:-0}" 'BEGIN { printf "%.6f", p + v }')
    if [ -z "$PCOMM" ] && [ -z "$VCOMM" ]; then COMM="-"; fi

    if [ -z "$PTIME" ] || [ -z "$VTIME" ]; then
        echo "    could not read the timings for $MODEL, skipping it in the table."
        continue
    fi

    PROOFTIME=$(awk -v p="$PTIME" -v v="$VTIME" 'BEGIN { if (p > v) print p; else print v }')

    echo "    done: $MODEL  ($DATASET, $NEURONS neurons)"

    if [ "${MODEL:0:1}" = "R" ]; then
        echo "$DATASET $MODEL $NEURONS $DELTA $PROOFTIME $COMM" >> "$ROBUST_ROWS"
    else
        echo "$DATASET $MODEL $NEURONS $DELTA $PROOFTIME $COMM" >> "$FAIR_ROWS"
    fi
done

get_time() {
    awk -v s="$1" 'BEGIN {
        if (s < 60)       printf "%d s",   int(s + 0.5);
        else if (s < 3600) printf "%d min", int(s / 60 + 0.5);
        else               printf "%d h",   int(s / 3600 + 0.5);
    }'
}

get_comm() {
    awk -v g="$1" 'BEGIN {
        if (g == "" || g == "-")  { printf "-"; }
        else if (g >= 1)          { printf "%.2f GB", g; }
        else if (g >= 0.001)      { printf "%.3f GB", g; }
        else                      { printf "%.2f MB", g * 1024; }
    }'
}

make_table() {
    ROWS=$1
    TITLE=$2
    OUT=$3
    SHOW_DELTA=${4:-yes}
    SHOW_COMM=${5:-no}

    if [ "$SHOW_DELTA" = "yes" ]; then
        LINE="+------------+---------+-----------+-----------+------------------+"
        HEADER_FMT="| %-10s | %-7s | %9s | %9s | %16s |"
    else
        LINE="+------------+---------+-----------+------------------+"
        HEADER_FMT="| %-10s | %-7s | %9s | %16s |"
    fi

    if [ "$SHOW_COMM" = "yes" ]; then
        LINE="$LINE------------------+"
        HEADER_FMT="$HEADER_FMT %16s |\n"
    else
        HEADER_FMT="$HEADER_FMT\n"
    fi

    {
        echo "$TITLE"
        echo "(measured over a $BANDWIDTH loopback link, shaped with tc netem)"
        echo ""
        echo "$LINE"
        if [ "$SHOW_DELTA" = "yes" ]; then
            set -- "Dataset" "Model" "Neurons" "Delta" "Proof Time"
        else
            set -- "Dataset" "Model" "Neurons" "Proof Time"
        fi
        if [ "$SHOW_COMM" = "yes" ]; then
            printf "$HEADER_FMT" "$@" "Communication"
        else
            printf "$HEADER_FMT" "$@"
        fi
        echo "$LINE"
        while read -r DATASET MODEL NEURONS DELTA PROOFTIME COMM; do
            PRETTY_TIME=$(get_time "$PROOFTIME")
            PRETTY_COMM=$(get_comm "$COMM")
            if [ "$SHOW_DELTA" = "yes" ]; then
                set -- "$DATASET" "$MODEL" "$NEURONS" "$DELTA" "$PRETTY_TIME"
            else
                set -- "$DATASET" "$MODEL" "$NEURONS" "$PRETTY_TIME"
            fi
            if [ "$SHOW_COMM" = "yes" ]; then
                printf "$HEADER_FMT" "$@" "$PRETTY_COMM"
            else
                printf "$HEADER_FMT" "$@"
            fi
        done < "$ROWS"
        echo "$LINE"
    } > "$OUT"

    echo ""
    cat "$OUT"
}

echo ""
echo "=== Writing the result tables ==="

if [ -s "$ROBUST_ROWS" ]; then
    make_table "$ROBUST_ROWS" "Robustness proof costs" results/robustness_costs.txt yes yes
fi

if [ -s "$FAIR_ROWS" ]; then
    make_table "$FAIR_ROWS" "Fairness proof costs" results/fairness_costs.txt no no
fi

rm -f "$ROBUST_ROWS" "$FAIR_ROWS"

echo ""
echo "All experiments finished."
echo "Logs   : results/proof_cost/<MODEL>/{prover,verifier}.log"
echo "Tables : results/robustness_costs.txt and results/fairness_costs.txt"
