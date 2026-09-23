#!/bin/sh
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"
python3 "$ROOT_DIR/tests/multi_room_isolation_test.py" "$ROOT_DIR"
