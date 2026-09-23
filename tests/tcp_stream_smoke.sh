#!/bin/sh
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TMP_BIN=$(mktemp)
trap 'rm -f "$TMP_BIN"' EXIT INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$ROOT_DIR/tests/tcp_stream_unit.c" -o "$TMP_BIN"
"$TMP_BIN"
echo 'TCP stream fragmentation, CRC-16, overflow and partial-write checks passed'
