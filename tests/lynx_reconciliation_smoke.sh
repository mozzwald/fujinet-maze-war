#!/bin/sh
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TEST_BIN=$(mktemp /tmp/maze-lynx-reconcile.XXXXXX)
trap 'rm -f "$TEST_BIN"' EXIT INT TERM
${CC:-cc} -O2 -Wall -Wextra -Werror -pthread -o "$TEST_BIN" \
  "$ROOT_DIR/tests/lynx_reconciliation_test.c" \
  "$ROOT_DIR/clients/lynx/src/protocol.c" \
  "$ROOT_DIR/server/lobby_publisher.c" "$ROOT_DIR/server/transport_normalize.c" \
  "$ROOT_DIR/server/transport_stats.c"
"$TEST_BIN"
