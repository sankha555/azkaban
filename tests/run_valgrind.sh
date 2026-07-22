#!/usr/bin/env bash
#
# Run a two-party emp-zk test binary under valgrind to check for memory leaks.
#
# emp-zk programs run as two communicating parties (ALICE=1, BOB=2). This
# script launches BOTH parties on 127.0.0.1 -- each under its own valgrind
# instance -- and writes a separate log file per party.
#
# Usage:
#   run_valgrind.sh <log-dir> <test-binary> [extra-args-passed-to-both-parties...]
#
# Environment:
#   VALGRIND_OPTS   Override the default valgrind options.
#
# The script exits non-zero if valgrind reports errors for either party
# (thanks to --error-exitcode=1 in the default options).

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <log-dir> <test-binary> [extra-args...]" >&2
    exit 2
fi

LOG_DIR="$1"
BIN="$2"
shift 2
EXTRA_ARGS=("$@")

if [ ! -x "$BIN" ]; then
    echo "error: test binary not found or not executable: $BIN" >&2
    exit 2
fi

NAME="$(basename "$BIN")"
mkdir -p "$LOG_DIR"

: "${VALGRIND_OPTS:=--leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1}"
# Word-split VALGRIND_OPTS into an array of arguments.
# shellcheck disable=SC2206
VG_ARGS=(${VALGRIND_OPTS})

ALICE_LOG="${LOG_DIR}/valgrind_${NAME}_alice.log"
BOB_LOG="${LOG_DIR}/valgrind_${NAME}_bob.log"

echo "==> Running ${NAME} under valgrind (ALICE=1 background, BOB=2 foreground) on 127.0.0.1"

valgrind "${VG_ARGS[@]}" --log-file="$ALICE_LOG" "$BIN" 1 "${EXTRA_ARGS[@]}" &
ALICE_PID=$!

valgrind "${VG_ARGS[@]}" --log-file="$BOB_LOG" "$BIN" 2 "${EXTRA_ARGS[@]}"
BOB_STATUS=$?

wait "$ALICE_PID"
ALICE_STATUS=$?

echo "==> valgrind logs:"
echo "    ALICE: $ALICE_LOG"
echo "    BOB:   $BOB_LOG"

if [ "$ALICE_STATUS" -ne 0 ] || [ "$BOB_STATUS" -ne 0 ]; then
    echo "==> valgrind reported errors (alice exit=$ALICE_STATUS, bob exit=$BOB_STATUS)" >&2
    exit 1
fi

echo "==> valgrind clean: no leaks/errors detected"
