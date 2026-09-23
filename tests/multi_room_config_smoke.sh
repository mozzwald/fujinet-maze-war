#!/bin/sh
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SERVER="$ROOT_DIR/build/maze-war-server"

must_reject() {
    if "$SERVER" "$@" >/dev/null 2>&1; then
        echo "FAIL: accepted invalid options: $*" >&2
        exit 1
    fi
}

must_reject --room-count 0
must_reject --room-count 65
must_reject --room-count 2 --port 9100
must_reject --port 9100 --port-base 9200
must_reject --room-count 2 --port-base 65535
must_reject --room-count 2 --room-zombies 1
must_reject --room-count 2 --room-zombies 1,4
must_reject --zombies 1 --room-zombies 1
must_reject --tick-hz 0
must_reject --tick-hz 1001
must_reject --lag-ms -1

"$SERVER" --help 2>&1 | grep -F -- '--room-count' >/dev/null
"$SERVER" --help 2>&1 | grep -F -- '--room-zombies' >/dev/null

echo "multi-room config smoke passed"
