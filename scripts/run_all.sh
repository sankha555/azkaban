#!/bin/bash

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

MODE="${1:---quick}"
case "$MODE" in
    --quick|--all) ;;
    *) echo "usage: $0 [--quick | --all]"; exit 1 ;;
esac

echo "############################################################"
echo "# Azkaban artifact -- ${MODE#--} run"
echo "# started $(date '+%Y-%m-%d %H:%M:%S')"
echo "############################################################"

FAILED=""

echo ""
echo "############ Experiment 1/2: proof costs ############"
if scripts/proof_costs_experiment.sh "$MODE"; then
    echo "proof-cost experiment finished."
else
    echo "proof-cost experiment FAILED."
    FAILED="$FAILED proof_costs"
fi

echo ""
echo "############ Experiment 2/2: certification accuracy ############"
if scripts/certification_accuracy_experiment.sh "$MODE"; then
    echo "certification-accuracy experiment finished."
else
    echo "certification-accuracy experiment FAILED."
    FAILED="$FAILED certification_accuracy"
fi

echo ""
echo "############################################################"
echo "# Results"
echo "############################################################"
echo "Tables:"
for TABLE in results/robustness_costs.txt results/fairness_costs.txt \
             results/robustness_certification_accuracy.txt; do
    [ -f "$TABLE" ] && echo "  $TABLE"
done
echo "Logs:"
echo "  results/proof_cost/<MODEL>/{prover,verifier}.log"
echo "  results/certification_accuracy/<MODEL>/{azkaban,eran}.txt"

if [ -n "$FAILED" ]; then
    echo ""
    echo "FAILED experiments:$FAILED"
    exit 1
fi

echo ""
echo "finished $(date '+%Y-%m-%d %H:%M:%S')"
