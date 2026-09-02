#!/usr/bin/env bash
GUROBI_VERSION="${GUROBI_VERSION:-9.1.2}"
GUROBI_SOVERSION="gurobi91"
GUROBI_TARBALL_URL="https://packages.gurobi.com/9.1/gurobi${GUROBI_VERSION}_linux64.tar.gz"

_sourced=0
[ "${BASH_SOURCE[0]}" != "${0}" ] && _sourced=1

_ERAN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_ROOT="$(dirname "${_ERAN_DIR}")"
_ENV_FILE="${_ERAN_DIR}/gurobi_env.sh"

MODE="setup"
PREFIX="${HOME}/opt"
while [ $# -gt 0 ]; do
    case "$1" in
        --check)    MODE="check" ;;
        --download) MODE="download" ;;
        --prefix)   PREFIX="$2"; shift ;;
        -h|--help)  sed -n '2,28p' "${BASH_SOURCE[0]}"; return 0 2>/dev/null || exit 0 ;;
        *) echo "unknown option: $1" >&2; return 1 2>/dev/null || exit 1 ;;
    esac
    shift
done

_die() { echo "ERROR: $*" >&2; return 1 2>/dev/null || exit 1; }
_ok()  { echo "  [ok]   $*"; }
_warn(){ echo "  [warn] $*"; }

# --------------------------- locate the library ----------------------------
# Two shapes are supported: a full Gurobi distribution (GUROBI_HOME with
# lib/ and include/), or a library-only install (libgurobi91.so on the loader
# path, which is what this machine has).

find_gurobi_home() {
    local candidate
    for candidate in "${GUROBI_HOME}" \
                     "${PREFIX}/gurobi${GUROBI_VERSION//./}/linux64" \
                     /opt/gurobi*/linux64 \
                     "${HOME}"/gurobi*/linux64 \
                     "${HOME}"/*/gurobi*/linux64; do
        [ -n "${candidate}" ] && [ -f "${candidate}/lib/lib${GUROBI_SOVERSION}.so" ] \
            && { echo "${candidate}"; return 0; }
    done
    return 1
}

find_gurobi_lib() {
    local path
    path="$(ldconfig -p 2>/dev/null | awk -v s="lib${GUROBI_SOVERSION}.so" '$1==s {print $NF; exit}')"
    [ -n "${path}" ] && [ -f "${path}" ] && { echo "${path}"; return 0; }
    for path in /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu; do
        [ -f "${path}/lib${GUROBI_SOVERSION}.so" ] && { echo "${path}/lib${GUROBI_SOVERSION}.so"; return 0; }
    done
    return 1
}

download_gurobi() {
    mkdir -p "${PREFIX}" || _die "cannot create ${PREFIX}"
    echo "Downloading Gurobi ${GUROBI_VERSION} into ${PREFIX} ..."
    curl -fL "${GUROBI_TARBALL_URL}" -o "${PREFIX}/gurobi.tar.gz" || _die "download failed"
    tar -xzf "${PREFIX}/gurobi.tar.gz" -C "${PREFIX}" || _die "extraction failed"
    rm -f "${PREFIX}/gurobi.tar.gz"
}

echo "=== Gurobi setup for ERAN (expected runtime: ${GUROBI_VERSION}, lib${GUROBI_SOVERSION}.so) ==="

if [ "${MODE}" = "download" ] && ! find_gurobi_home >/dev/null; then
    download_gurobi
fi

GUROBI_HOME_FOUND="$(find_gurobi_home || true)"
GUROBI_LIB_FOUND="$(find_gurobi_lib || true)"

if [ -n "${GUROBI_HOME_FOUND}" ]; then
    _ok "Gurobi distribution: ${GUROBI_HOME_FOUND}"
    _GUROBI_HOME="${GUROBI_HOME_FOUND}"
    _GUROBI_LIBDIR="${GUROBI_HOME_FOUND}/lib"
elif [ -n "${GUROBI_LIB_FOUND}" ]; then
    _ok "Gurobi library (library-only install): ${GUROBI_LIB_FOUND}"
    _GUROBI_HOME=""
    _GUROBI_LIBDIR="$(dirname "${GUROBI_LIB_FOUND}")"
else
    _warn "no lib${GUROBI_SOVERSION}.so found."
    echo "         Re-run with:  bash ${BASH_SOURCE[0]} --download [--prefix DIR]"
    _die "Gurobi ${GUROBI_VERSION} runtime is required by libfppoly.so"
fi

# ------------------------------- license -----------------------------------
_GRB_LICENSE_FILE="${GRB_LICENSE_FILE:-}"
if [ -z "${_GRB_LICENSE_FILE}" ]; then
    for candidate in "${HOME}/gurobi.lic" /opt/gurobi/gurobi.lic /usr/local/lib/gurobi.lic; do
        [ -f "${candidate}" ] && { _GRB_LICENSE_FILE="${candidate}"; break; }
    done
fi
if [ -n "${_GRB_LICENSE_FILE}" ]; then
    _ok "license file: ${_GRB_LICENSE_FILE}"
else
    _warn "no gurobi.lic found. This is fine for test_deepzono.py (the deepzono"
    echo "         domain never builds a Gurobi model); it is required only for"
    echo "         refinepoly / MILP runs."
fi

# ------------------------------- gurobipy ----------------------------------
PY="${PYTHON:-python3}"
if "${PY}" -c "import gurobipy" >/dev/null 2>&1; then
    _ok "gurobipy importable by ${PY} ($("${PY}" -c 'import gurobipy;print(gurobipy.gurobi.version())' 2>/dev/null))"
elif [ "${MODE}" = "check" ]; then
    _warn "gurobipy is NOT importable by ${PY}"
else
    echo "  installing gurobipy==${GUROBI_VERSION} for ${PY} ..."
    "${PY}" -m pip install "gurobipy==${GUROBI_VERSION}" || \
        _warn "pip install failed; install gurobipy manually (or run setup.py from ${_GUROBI_HOME})"
fi

# --------------------------- ELINA build config ----------------------------
# Only matters if ELINA is rebuilt: Makefile.config hard-codes GUROBI_HOME.
_MAKEFILE_CONFIG="${_ERAN_DIR}/ELINA/Makefile.config"
if [ -f "${_MAKEFILE_CONFIG}" ] && [ -z "${_GUROBI_HOME}" ]; then
    _current="$(awk -F'[?]?=' '/^GUROBI_HOME[ ?]*=/{gsub(/^[ \t]+|[ \t]+$/,"",$2); print $2; exit}' "${_MAKEFILE_CONFIG}")"
    if [ ! -d "${_current}" ]; then
        _warn "ELINA/Makefile.config GUROBI_HOME=${_current} does not exist."
        echo "         Harmless for running the prebuilt libfppoly.so, but a REBUILD of"
        echo "         ELINA needs the headers from a full distribution: re-run with"
        echo "         --download, then run this script again to patch the path."
    fi
elif [ -f "${_MAKEFILE_CONFIG}" ] && [ -n "${_GUROBI_HOME}" ]; then
    _current="$(awk -F'[?]?=' '/^GUROBI_HOME[ ?]*=/{gsub(/^[ \t]+|[ \t]+$/,"",$2); print $2; exit}' "${_MAKEFILE_CONFIG}")"
    if [ "${_current}" != "${_GUROBI_HOME}" ]; then
        if [ "${MODE}" = "check" ]; then
            _warn "ELINA/Makefile.config GUROBI_HOME=${_current} (stale; would be ${_GUROBI_HOME})"
        else
            cp "${_MAKEFILE_CONFIG}" "${_MAKEFILE_CONFIG}.bak"
            sed -i "s|^GUROBI_HOME[ ?]*=.*|GUROBI_HOME ?= ${_GUROBI_HOME}|" "${_MAKEFILE_CONFIG}"
            _ok "ELINA/Makefile.config GUROBI_HOME -> ${_GUROBI_HOME} (backup: .bak)"
        fi
    else
        _ok "ELINA/Makefile.config GUROBI_HOME already correct"
    fi
fi

# ------------------------------ environment --------------------------------
_LD="${_GUROBI_LIBDIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

if [ "${MODE}" != "check" ]; then
    {
        echo "# generated by eran/setup_gurobi.sh -- source this file"
        [ -n "${_GUROBI_HOME}" ] && echo "export GUROBI_HOME=\"${_GUROBI_HOME}\""
        [ -n "${_GUROBI_HOME}" ] && echo "export PATH=\"\${GUROBI_HOME}/bin:\${PATH}\""
        echo "export LD_LIBRARY_PATH=\"${_GUROBI_LIBDIR}\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}\""
        [ -n "${_GRB_LICENSE_FILE}" ] && echo "export GRB_LICENSE_FILE=\"${_GRB_LICENSE_FILE}\""
    } > "${_ENV_FILE}"
    _ok "wrote ${_ENV_FILE}"
fi

# ------------------------------ verification -------------------------------
echo "--- verification ---"
_FPPOLY="$(ldconfig -p 2>/dev/null | awk '$1=="libfppoly.so"{print $NF; exit}')"
[ -z "${_FPPOLY}" ] && [ -f "${_ERAN_DIR}/ELINA/fppoly/libfppoly.so" ] && _FPPOLY="${_ERAN_DIR}/ELINA/fppoly/libfppoly.so"
if [ -n "${_FPPOLY}" ]; then
    if LD_LIBRARY_PATH="${_LD}" ldd "${_FPPOLY}" | grep -q "lib${GUROBI_SOVERSION}.so => /"; then
        _ok "libfppoly.so resolves lib${GUROBI_SOVERSION}.so"
    else
        _warn "libfppoly.so cannot resolve lib${GUROBI_SOVERSION}.so:"
        LD_LIBRARY_PATH="${_LD}" ldd "${_FPPOLY}" | grep -i gurobi
    fi
else
    _warn "libfppoly.so not found; build/install ELINA first"
fi

if LD_LIBRARY_PATH="${_LD}" "${PY}" -c "import gurobipy" >/dev/null 2>&1; then
    _ok "python: import gurobipy"
else
    _warn "python: import gurobipy FAILED for ${PY}"
fi

echo
if [ "${_sourced}" = "1" ]; then
    [ -n "${_GUROBI_HOME}" ] && export GUROBI_HOME="${_GUROBI_HOME}" && export PATH="${_GUROBI_HOME}/bin:${PATH}"
    export LD_LIBRARY_PATH="${_LD}"
    [ -n "${_GRB_LICENSE_FILE}" ] && export GRB_LICENSE_FILE="${_GRB_LICENSE_FILE}"
    echo "Environment exported into the current shell."
elif [ -f "${_ENV_FILE}" ]; then
    echo "Run this to load the environment into your shell:"
    echo "    source ${_ENV_FILE}"
else
    echo "Re-run without --check to write ${_ENV_FILE}."
fi
