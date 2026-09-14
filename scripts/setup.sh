#!/usr/bin/env bash
#
# Setup for the Azkaban artifact.
#
# This is the only script that needs to be run to complete all setup. It installs the system
# packages, builds and installs emp-toolkit and ELINA, creates the python
# virtualenv, shapes the loopback link, and verifies the result. Running it
# twice is safe: every step is skipped if it is already done.
#
#     bash scripts/setup.sh
#
# Afterwards, run the experiments directly:
#
#     scripts/run_all.sh --quick        # both experiments, short version
#     scripts/run_all.sh --all          # both experiments, full sweep
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

JOBS="$(nproc 2>/dev/null || echo 4)"
ELINA_DIR="${ROOT}/eran/ELINA"
ENV_FILE="${ROOT}/eran/env.sh"
# Where emp-toolkit and ELINA are installed. /usr/local needs root; point
# AZKABAN_PREFIX somewhere writable (e.g. "$HOME/.local") to install without it.
PREFIX="${AZKABAN_PREFIX:-/usr/local}"

SKIP_SUDOERS=0
SKIP_APT=0
SKIP_TC=0
FORCE=0

usage() {
    # the leading comment block, minus the '#!' line and the '# ' prefixes
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' \
        "${BASH_SOURCE[0]}"
    cat <<'USAGE'
Options:
  --no-sudoers   do not install the passwordless-tc sudoers rule
  --skip-apt     do not touch apt (assume the system packages are present)
  --skip-tc      do not shape the loopback link
  --force        rebuild emp-toolkit and ELINA even if they look done
  -h, --help     this message
USAGE
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --no-sudoers) SKIP_SUDOERS=1 ;;
        --skip-apt)   SKIP_APT=1 ;;
        --skip-tc)    SKIP_TC=1 ;;
        --force)      FORCE=1 ;;
        -h|--help)    usage ;;
        *) echo "unknown option: $1 (try --help)" >&2; exit 1 ;;
    esac
    shift
done

# --------------------------------------------------------------------------
# output helpers (same vocabulary as the experiment scripts)
# --------------------------------------------------------------------------
step() { printf '\n\033[1m=== %s ===\033[0m\n' "$*"; }
info() { printf '  %s\n' "$*"; }
ok()   { printf '  \033[32m[ok]\033[0m   %s\n' "$*"; }
warn() { printf '  \033[33m[warn]\033[0m %s\n' "$*"; }
fail() { printf '  \033[31m[fail]\033[0m %s\n' "$*" >&2; }
die()  { fail "$*"; exit 1; }

# --------------------------------------------------------------------------
# privilege escalation: nothing if we are already root, sudo otherwise
# --------------------------------------------------------------------------
if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
else
    command -v sudo >/dev/null 2>&1 || die "not root and sudo is not installed"
    SUDO="sudo"
fi

# Installing into a prefix we already own (AZKABAN_PREFIX=$HOME/.local) needs
# no sudo -- and neither does root, wherever it installs.
if [ -w "${PREFIX}" ] || [ -w "$(dirname "${PREFIX}")" ]; then
    INSTALL_SUDO=""
else
    INSTALL_SUDO="${SUDO}"
fi

# ldconfig is only worth running for a prefix the loader already searches; for
# anything else, eran/env.sh's LD_LIBRARY_PATH is what makes the libraries
# findable.
case "${PREFIX}" in
    /usr|/usr/*|/lib|/lib/*) ldconfig_maybe() { ${INSTALL_SUDO} ldconfig; } ;;
    *)                       ldconfig_maybe() { :; } ;;
esac

TMPDIR_SETUP=""
cleanup() { [ -n "${TMPDIR_SETUP}" ] && rm -rf "${TMPDIR_SETUP}"; }
trap cleanup EXIT

printf '\033[1mAzkaban artifact setup\033[0m\n'
info "project root: ${ROOT}"
info "parallelism:  ${JOBS} jobs"
info "install prefix: ${PREFIX}"

# Ask for the password once, up front, but only for the things that actually
# need it -- with --skip-apt/--skip-tc and a writable prefix, nothing does.
SUDO_NEEDED_FOR=""
[ -n "${SUDO}" ] && [ "${SKIP_APT}" = "0" ] && SUDO_NEEDED_FOR="apt"
[ -n "${SUDO}" ] && [ "${SKIP_TC}" = "0" ] && SUDO_NEEDED_FOR="${SUDO_NEEDED_FOR:+${SUDO_NEEDED_FOR}, }tc on lo"
[ -n "${INSTALL_SUDO}" ] && SUDO_NEEDED_FOR="${SUDO_NEEDED_FOR:+${SUDO_NEEDED_FOR}, }'make install' into ${PREFIX}"
if [ -n "${SUDO_NEEDED_FOR}" ]; then
    info "this needs sudo for: ${SUDO_NEEDED_FOR}"
    ${SUDO} -v || die "sudo authentication failed"
fi

# --------------------------------------------------------------------------
# 1. System packages
# --------------------------------------------------------------------------
# libgmp/libmpfr/libcdd are ELINA's dependencies; ELINA's fconv needs cddlib's
# headers, which Ubuntu ships under /usr/include/cddlib.
APT_PACKAGES="build-essential cmake git wget curl ca-certificates m4
              libssl-dev libgmp-dev libmpfr-dev libcdd-dev
              python3 python3-pip python3-venv python3-dev
              iperf3 iproute2"

install_packages() {
    step "1/7  System packages"

    if [ "${SKIP_APT}" = "1" ]; then
        warn "--skip-apt given; not touching apt"
        return 0
    fi
    command -v apt-get >/dev/null 2>&1 || {
        warn "apt-get not found; install these yourself: $(echo ${APT_PACKAGES})"
        return 0
    }

    info "apt-get update"
    ${SUDO} apt-get update -qq || die "apt-get update failed"
    info "installing: $(echo ${APT_PACKAGES})"
    ${SUDO} env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ${APT_PACKAGES} > /dev/null || die "apt-get install failed"
    ok "system packages installed"
}

# --------------------------------------------------------------------------
# 2. emp-toolkit, from the copies vendored in src/
# --------------------------------------------------------------------------
# Nothing is fetched from GitHub, so the build cannot be broken by an upstream
# change. emp-tool is built out of tree so the repository stays clean.
install_emp() {
    step "2/7  emp-toolkit (from src/emp-tool and src/emp-ot)"

    if [ "${FORCE}" = "0" ] \
       && [ -f "${PREFIX}/include/emp-ot/emp-ot.h" ] \
       && [ -f "${PREFIX}/cmake/emp-tool-config.cmake" ]; then
        ok "already installed in ${PREFIX}; skipping"
        return 0
    fi

    TMPDIR_SETUP="$(mktemp -d)"
    cp -a "${ROOT}/src/emp-tool" "${ROOT}/src/emp-ot" "${TMPDIR_SETUP}/" \
        || die "cannot stage the emp sources"

    info "building emp-tool"
    ( cd "${TMPDIR_SETUP}/emp-tool" \
      && cmake . -DCMAKE_INSTALL_PREFIX="${PREFIX}" > /dev/null \
      && make -j"${JOBS}" emp-tool > /dev/null ) || die "emp-tool build failed"
    ${INSTALL_SUDO} cmake --install "${TMPDIR_SETUP}/emp-tool" > /dev/null \
        || die "emp-tool install failed"

    info "installing emp-ot (header-only)"
    ( cd "${TMPDIR_SETUP}/emp-ot" \
      && cmake . -DCMAKE_INSTALL_PREFIX="${PREFIX}" > /dev/null ) \
        || die "emp-ot configure failed"
    ${INSTALL_SUDO} cmake --install "${TMPDIR_SETUP}/emp-ot" > /dev/null \
        || die "emp-ot install failed"

    ldconfig_maybe
    rm -rf "${TMPDIR_SETUP}"; TMPDIR_SETUP=""
    ok "emp-toolkit installed in ${PREFIX}"
}

# --------------------------------------------------------------------------
# 3. ELINA (the native half of ERAN)
# --------------------------------------------------------------------------
# eran/python_helpers loads these .so files through ctypes, straight out of the
# source tree (see eran/ELINA/python_interface/*_imports.py), so ELINA is built
# in place; 'make install' is still needed because each library resolves its
# siblings through the loader.
#
# Every library in this list is on the import path of test_deepzono.py:
#   zonoml   -> DeepZono itself
#   fppoly   -> pulled in by python_helpers/deeppoly_nodes.py
#   fconv    -> pulled in by python_helpers/krelu.py
# hence '-use-deeppoly -use-fconv'. Gurobi is deliberately NOT enabled: it only
# guards the spatial-constraint path in fppoly/compute_bounds.c, which the
# L_infinity certification in this artifact never reaches. That keeps the setup
# free of the ~1 GB Gurobi download and of any licence requirement.
ELINA_LIBS="elina_auxiliary/libelinaux.so
            elina_linearize/libelinalinearize.so
            partitions_api/libpartitions.so
            elina_oct/liboptoct.so
            elina_poly/liboptpoly.so
            elina_zones/liboptzones.so
            elina_zonotope/libzonotope.so
            zonoml/libzonoml.so
            fppoly/libfppoly.so
            fconv/libfconv.so"

elina_built() {
    local lib
    for lib in ${ELINA_LIBS}; do
        [ -f "${ELINA_DIR}/${lib}" ] || return 1
    done
    return 0
}

find_cdd_prefix() {
    local candidate
    for candidate in "${CDD_PREFIX:-}" /usr/include/cddlib /usr/local/include/cddlib /usr/include /usr/local/include; do
        [ -n "${candidate}" ] || continue
        [ -f "${candidate}/setoper.h" ] && { echo "${candidate}"; return 0; }
    done
    return 1
}

build_elina() {
    step "3/7  ELINA"

    if [ "${FORCE}" = "0" ] && elina_built; then
        ok "all ELINA libraries already built; skipping"
        return 0
    fi

    [ -x "${ELINA_DIR}/configure" ] || die "${ELINA_DIR}/configure is missing"

    local cdd_prefix
    cdd_prefix="$(find_cdd_prefix)" \
        || die "cddlib headers (setoper.h) not found -- install libcdd-dev"
    info "cddlib headers: ${cdd_prefix}"

    info "configure (deeppoly + fconv, no gurobi)"
    ( cd "${ELINA_DIR}" \
      && ./configure -prefix "${PREFIX}" -use-deeppoly -use-fconv \
                     -cdd-prefix "${cdd_prefix}" ) > /dev/null 2>&1 \
        || { ( cd "${ELINA_DIR}" && ./configure -prefix "${PREFIX}" -use-deeppoly \
                 -use-fconv -cdd-prefix "${cdd_prefix}" ) | tail -20
             die "ELINA configure failed (output above)"; }

    info "make (this takes a few minutes)"
    make -C "${ELINA_DIR}" -j"${JOBS}" c > /dev/null 2>&1 \
        || { make -C "${ELINA_DIR}" c 2>&1 | tail -20
             die "ELINA build failed (output above)"; }

    info "make install into ${PREFIX}"
    ${INSTALL_SUDO} make -C "${ELINA_DIR}" install > /dev/null 2>&1 \
        || die "ELINA install failed"
    ldconfig_maybe

    elina_built || die "ELINA finished but some libraries are missing"
    ok "ELINA built in place and installed in ${PREFIX}"
}

# --------------------------------------------------------------------------
# 4. The C++ executables
# --------------------------------------------------------------------------
build_azkaban() {
    step "4/7  Azkaban C++ executables"

    cmake -S "${ROOT}" -B "${ROOT}/build" -DCMAKE_PREFIX_PATH="${PREFIX}" > /dev/null \
        || die "cmake configure failed"
    cmake --build "${ROOT}/build" -j"${JOBS}" > /dev/null \
        || { cmake --build "${ROOT}/build" -j"${JOBS}" 2>&1 | tail -20
             die "build failed (output above)"; }

    local binary
    for binary in robustness fairness cleartext_accuracy; do
        [ -x "${ROOT}/build/bin/${binary}" ] || die "build/bin/${binary} was not produced"
    done
    ok "build/bin/{robustness,fairness,cleartext_accuracy}"
}

# --------------------------------------------------------------------------
# 5. Python virtualenv
# --------------------------------------------------------------------------
setup_python() {
    step "5/7  Python virtualenv"

    if [ ! -d "${ROOT}/venv" ]; then
        info "creating ${ROOT}/venv"
        python3 -m venv "${ROOT}/venv" || die "could not create the virtualenv"
    else
        info "reusing ${ROOT}/venv"
    fi
    PY="${ROOT}/venv/bin/python3"

    info "installing eran/requirements.txt (tensorflow is a large download)"
    "${PY}" -m pip install --quiet --upgrade pip || die "pip self-upgrade failed"
    "${PY}" -m pip install --quiet -r "${ROOT}/eran/requirements.txt" \
        || die "pip install -r eran/requirements.txt failed"
    ok "python packages installed"
}

# --------------------------------------------------------------------------
# 6. Environment file sourced by the experiment scripts
# --------------------------------------------------------------------------
write_env_file() {
    step "6/7  Environment file"

    {
        echo "# generated by scripts/setup.sh -- source this file"
        echo "export LD_LIBRARY_PATH=\"${PREFIX}/lib\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}\""
        echo "export PYTHONPATH=\"${ELINA_DIR}/python_interface:${ROOT}/eran/python_helpers\${PYTHONPATH:+:\${PYTHONPATH}}\""
    } > "${ENV_FILE}" || die "cannot write ${ENV_FILE}"
    ok "wrote ${ENV_FILE}"
}

# --------------------------------------------------------------------------
# 7. Loopback shaping (the proof-cost experiment measures a 1 Gbit link)
# --------------------------------------------------------------------------
setup_network() {
    step "7/7  Loopback shaping and passwordless tc"

    if [ "${SKIP_TC}" = "1" ]; then
        warn "--skip-tc given; not shaping lo"
        return 0
    fi

    local tc_bin
    tc_bin="$(command -v tc || echo /usr/sbin/tc)"
    [ -x "${tc_bin}" ] || { warn "tc not found; skipping (install iproute2)"; return 0; }

    if ${SUDO} "${tc_bin}" qdisc show dev lo 2>/dev/null | grep -q netem; then
        ${SUDO} "${tc_bin}" qdisc change dev lo root netem rate 1gbit
    else
        ${SUDO} "${tc_bin}" qdisc add dev lo root netem rate 1gbit
    fi || { warn "could not shape lo (a container needs --cap-add=NET_ADMIN)"; return 0; }
    ok "lo shaped to 1gbit"

    local sudoers_file=/etc/sudoers.d/azkaban-experiments
    if [ -z "${SUDO}" ]; then
        info "running as root; no sudoers rule needed"
    elif [ "${SKIP_SUDOERS}" = "1" ]; then
        warn "--no-sudoers given; the experiments will ask for your password"
    elif [ -f "${sudoers_file}" ]; then
        ok "passwordless tc already configured (${sudoers_file})"
    else
        local tmp_sudoers
        tmp_sudoers="$(mktemp)"
        printf '%s ALL=(root) NOPASSWD: %s\n' "$(id -un)" "${tc_bin}" > "${tmp_sudoers}"
        if ${SUDO} visudo -cf "${tmp_sudoers}" > /dev/null 2>&1; then
            ${SUDO} install -m 0440 -o root -g root "${tmp_sudoers}" "${sudoers_file}"
            ok "wrote ${sudoers_file} (only ${tc_bin}, only for $(id -un))"
            info "undo any time with: sudo rm ${sudoers_file}"
        else
            warn "the generated sudoers rule was rejected by visudo; not installing it"
        fi
        rm -f "${tmp_sudoers}"
    fi
}

# --------------------------------------------------------------------------
# Verification: everything the experiment scripts are about to need
# --------------------------------------------------------------------------
verify() {
    step "Verification"

    local problems=0

    local binary
    for binary in robustness fairness cleartext_accuracy; do
        if [ -x "${ROOT}/build/bin/${binary}" ]; then
            ok "build/bin/${binary}"
        else
            fail "build/bin/${binary} is missing"; problems=$((problems + 1))
        fi
    done

    if elina_built; then
        ok "ELINA libraries present"
    else
        fail "some ELINA libraries are missing"; problems=$((problems + 1))
    fi

    local missing
    missing="$("${PY}" - <<'PYDEPS'
import importlib.util
required = ['numpy', 'onnx', 'tensorflow', 'gurobipy']
print(' '.join(m for m in required if importlib.util.find_spec(m) is None))
PYDEPS
)"
    if [ -z "${missing}" ]; then
        ok "python packages (numpy, onnx, tensorflow, gurobipy)"
    else
        fail "missing python packages: ${missing}"; problems=$((problems + 1))
    fi

    # The real test: the ctypes bindings the ERAN driver imports at start-up.
    # shellcheck disable=SC1090
    source "${ENV_FILE}"
    if "${PY}" -c "import zonoml, fppoly, fconv" > /dev/null 2>&1; then
        ok "ERAN/ELINA python bindings load"
    else
        fail "the ERAN/ELINA python bindings do not load:"
        "${PY}" -c "import zonoml, fppoly, fconv" 2>&1 | tail -5 | sed 's/^/         /'
        problems=$((problems + 1))
    fi

    printf '\n'
    if [ "${problems}" -eq 0 ]; then
        ok "setup complete -- nothing else to run."
        info "next:  scripts/run_all.sh --quick    (both experiments, short version)"
        info "       scripts/run_all.sh --all      (both experiments, full sweep)"
        return 0
    fi
    fail "${problems} problem(s) above; the experiments will not run yet."
    return 1
}

install_packages
install_emp
build_elina
build_azkaban
setup_python
write_env_file
setup_network
verify
