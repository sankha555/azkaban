#!/usr/bin/env bash

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
RESULTS_DIR="${ROOT}/results"
CERT_DIR="${RESULTS_DIR}/certification_accuracy"
TABLE_FILE="${RESULTS_DIR}/robustness_certification_accuracy.txt"

QUICK_MODELS="R1 R2"
ALL_MODELS="R1 R2 R3 R4 R5 R6"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

step()  { printf '\n\033[1m=== %s ===\033[0m\n' "$*"; }
info()  { printf '  %s\n' "$*"; }
ok()    { printf '  \033[32m[ok]\033[0m   %s\n' "$*"; }
warn()  { printf '  \033[33m[warn]\033[0m %s\n' "$*"; }
fail()  { printf '  \033[31m[fail]\033[0m %s\n' "$*" >&2; }
die()   { fail "$*"; exit 1; }

activate_python() {
    if [ -n "${VIRTUAL_ENV:-}" ]; then
        PY="${VIRTUAL_ENV}/bin/python3"
        ok "using the already-active virtualenv: ${VIRTUAL_ENV}"
        return 0
    fi

    local candidate
    for candidate in "${ROOT}/env" "${ROOT}/venv" "${ROOT}/.venv" "${HOME}/env" "${HOME}/venv"; do
        if [ -f "${candidate}/bin/activate" ]; then
            source "${candidate}/bin/activate"
            PY="${candidate}/bin/python3"
            ok "activated virtualenv: ${candidate}"
            return 0
        fi
    done

    PY="${PYTHON:-python3}"
    warn "no virtualenv found; falling back to ${PY}"
}

check_python_deps() {
    local missing
    missing="$("${PY}" - <<'PYDEPS'
import importlib.util
required = ['numpy', 'onnx', 'tensorflow', 'gurobipy']
print(' '.join(m for m in required if importlib.util.find_spec(m) is None))
PYDEPS
)"
    if [ -n "${missing}" ]; then
        fail "missing python packages: ${missing}"
        info "the ERAN runs cannot work without them. Install everything with:"
        info "    bash ${ROOT}/scripts/setup.sh"
        exit 1
    fi
    ok "python dependencies present (numpy, onnx, tensorflow, gurobipy)"

    # Written by scripts/setup.sh; puts the installed ELINA libraries and the
    # ERAN python packages on the loader and import paths.
    if [ -f "${ROOT}/eran/env.sh" ]; then
        source "${ROOT}/eran/env.sh"
        ok "sourced eran/env.sh"
    else
        warn "eran/env.sh missing (scripts/setup.sh writes it)"
    fi

    # The ERAN driver dies on these three ctypes imports before it looks at a
    # single model, so check them here rather than once per model.
    if PYTHONPATH="${ROOT}/eran/ELINA/python_interface:${ROOT}/eran/python_helpers" \
       "${PY}" -c "import zonoml, fppoly, fconv" >/dev/null 2>&1; then
        ok "ERAN/ELINA bindings load"
    else
        fail "the ERAN/ELINA python bindings do not load, so every ERAN run would fail:"
        PYTHONPATH="${ROOT}/eran/ELINA/python_interface:${ROOT}/eran/python_helpers" \
            "${PY}" -c "import zonoml, fppoly, fconv" 2>&1 | tail -3 | sed 's/^/         /'
        info "ELINA has not been built. Build it (and everything else) with:"
        info "    bash ${ROOT}/scripts/setup.sh"
        exit 1
    fi
}

build_project() {
    step "1/4  Build C++ executables and prepare the python environment"

    command -v cmake >/dev/null 2>&1 || die "cmake not found"
    cmake -S "${ROOT}" -B "${BUILD_DIR}" >/dev/null || die "cmake configure failed"
    cmake --build "${BUILD_DIR}" --target cleartext_accuracy -j "${JOBS}" >/dev/null \
        || die "build of cleartext_accuracy failed"
    ok "built ${BUILD_DIR}/bin/cleartext_accuracy"

    activate_python
    check_python_deps
}

prepare_results() {
    step "2/4  Results directory"
    mkdir -p "${CERT_DIR}" || die "cannot create ${CERT_DIR}"
    ok "${RESULTS_DIR}"
}

validate_models() {
    step "Models to run"

    local model
    for model in ${MODELS}; do
        case "${model}" in
            F*|f*)
                die "'${model}' is a fairness model: certification accuracy is only supported for robustness experiments (R*) for now."
                ;;
        esac
        [ -f "${ROOT}/data/configs/${model}.json" ] \
            || die "no config for '${model}' (expected data/configs/${model}.json)"
    done
    ok "${MODELS}"
}

read_config_field() {
    "${PY}" - "$1" "$2" <<'PY'
import json, os, sys
model, field = sys.argv[1], sys.argv[2]
root = os.environ['EXPERIMENT_ROOT']
with open(os.path.join(root, 'data', 'configs', model + '.json')) as handle:
    config = json.load(handle)
if field == 'dataset':
    print(os.path.basename(config['input_file']).split('_')[0])
elif field == 'delta':
    print(config['delta'])
elif field == 'num_tests':
    print(len(config['example_indices']))
PY
}

progress() {
    local current="$1"
    printf '\n\033[1m--- %s ---\033[0m\n' "running: ${current}"
    if [ -n "${COMPLETED}" ]; then
        info "completed: ${COMPLETED}"
    else
        info "completed: (none yet)"
    fi
    info "remaining: ${REMAINING:-(none)}"
}

run_experiments() {
    step "3/4  Certification accuracy runs"

    COMPLETED=""
    local -a model_list
    read -ra model_list <<< "${MODELS}"
    local index=0

    for model in "${model_list[@]}"; do
        index=$((index + 1))
        REMAINING="$(printf '%s ' "${model_list[@]:${index}}" | sed 's/[[:space:]]*$//')"
        progress "${model} (${index}/${#model_list[@]})"

        local out_dir="${CERT_DIR}/${model}"
        mkdir -p "${out_dir}"

        local dataset delta num_tests
        dataset="$(read_config_field "${model}" dataset)"
        delta="$(read_config_field "${model}" delta)"
        num_tests="$(read_config_field "${model}" num_tests)"
        info "dataset=${dataset} delta=${delta} examples=${num_tests}"

        info "azkaban: ${BUILD_DIR}/bin/cleartext_accuracy ${model}"
        local started=${SECONDS}
        if "${BUILD_DIR}/bin/cleartext_accuracy" "${model}" > "${out_dir}/azkaban.txt" 2>&1; then
            ok "azkaban done in $((SECONDS - started))s -> ${out_dir}/azkaban.txt"
        else
            fail "azkaban run failed for ${model} (see ${out_dir}/azkaban.txt)"
        fi

        info "eran: test_deepzono.py --model ${model} --dataset ${dataset} --delta ${delta} --num_tests ${num_tests}"
        started=${SECONDS}
        if "${PY}" "${ROOT}/eran/test_deepzono.py" \
                --model "${model}" --dataset "${dataset}" \
                --delta "${delta}" --num_tests "${num_tests}" \
                > "${out_dir}/eran.txt" 2>&1; then
            ok "eran done in $((SECONDS - started))s -> ${out_dir}/eran.txt"
        else
            fail "eran run failed for ${model} (see ${out_dir}/eran.txt)"
        fi

        COMPLETED="${COMPLETED}${COMPLETED:+ }${model}"
    done

    printf '\n'
    ok "all runs finished: ${COMPLETED}"
}

write_table() {
    step "4/4  Summary table"

    EXPERIMENT_MODELS="${MODELS}" "${PY}" - <<'PY' > "${TABLE_FILE}"
import os, re, datetime

root = os.environ['EXPERIMENT_ROOT']
models = os.environ['EXPERIMENT_MODELS'].split()
cert_dir = os.path.join(root, 'results', 'certification_accuracy')

def field(path, label):
    """First value of a '<label> ... : value' / '<label>: value' line."""
    if not os.path.isfile(path):
        return None
    pattern = re.compile(r'^\s*' + re.escape(label) + r'\s*:\s*(.+?)\s*$')
    with open(path, errors='replace') as handle:
        for line in handle:
            match = pattern.match(line)
            if match:
                return match.group(1)
    return None

def percent(value):
    if value is None:
        return '-'
    match = re.match(r'(\d+(?:\.\d+)?)\s*%', value)
    return match.group(1) if match else value.split(' ')[0]

rows = []
for model in models:
    azkaban = os.path.join(cert_dir, model, 'azkaban.txt')
    eran = os.path.join(cert_dir, model, 'eran.txt')
    rows.append([
        (field(azkaban, 'Dataset') or field(eran, 'Dataset') or '-'),
        model,
        (field(azkaban, 'Neurons') or field(eran, 'Neurons') or '-'),
        (field(azkaban, 'Delta') or field(eran, 'Delta') or '-'),
        percent(field(eran, 'Certification Accuracy')),
        percent(field(azkaban, 'Certification Accuracy')),
    ])

headers = ['Dataset', 'Model', 'Neurons', 'Delta', 'ERAN Accuracy (%)', 'Azkaban Accuracy (%)']
widths = [max(len(headers[i]), *(len(row[i]) for row in rows)) if rows else len(headers[i])
          for i in range(len(headers))]

def line(char='-', joint='+'):
    return joint + joint.join(char * (width + 2) for width in widths) + joint

def render(cells):
    return '| ' + ' | '.join(cell.ljust(widths[i]) for i, cell in enumerate(cells)) + ' |'

out = []
out.append('Robustness certification accuracy')
out.append('generated ' + datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
out.append('')
out.append(line('='))
out.append(render(headers))
out.append(line('='))
for row in rows:
    out.append(render(row))
out.append(line('='))
out.append('')
out.append('ERAN    : floating-point zonotope (eran/test_deepzono.py)')
out.append('Azkaban : fixed-point zonotope (build/bin/cleartext_accuracy)')
out.append('Accuracy computed as: certified examples / correctly classified examples.')
out.append('Per-run logs found at: results/certification_accuracy/<MODEL>/{azkaban,eran}.txt')
print('\n'.join(out))
PY

    if [ -s "${TABLE_FILE}" ]; then
        ok "wrote ${TABLE_FILE}"
        printf '\n'
        cat "${TABLE_FILE}"
    else
        fail "failed to write ${TABLE_FILE}"
    fi
}

case "${1:-}" in
    --quick) MODELS="${QUICK_MODELS}" ;;
    --all)   MODELS="${ALL_MODELS}" ;;
    "")      MODELS="${ALL_MODELS}"
             info "no mode given, running the full sweep (use --quick for the short version)" ;;
    -*)      die "unknown option: $1 (usage: $0 [--quick | --all | MODEL ...])" ;;
    *)       MODELS="$*" ;;
esac
export EXPERIMENT_ROOT="${ROOT}"
PY="${PYTHON:-python3}"

printf '\033[1mCertification accuracy experiment\033[0m\n'
info "project root: ${ROOT}"

validate_models
build_project
prepare_results
run_experiments
write_table

printf '\n'
ok "experiment complete."
