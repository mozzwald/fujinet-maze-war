# Transport Validation

## Smoke script

Run the automated transport normalization check from the repository root:

```sh
bash tests/transport_normalize_smoke.sh
```

The script builds `build/maze-war-server`, launches it with `--port 9101 --zombies 0 --debug`, captures stdout to a temporary server log, and fails unless the log contains accepted `primary`, `swapped`, and `extra-41` DELTA normalization markers for `slot=0`.

To keep a copy of the server log beside other script output, run the same server command manually and redirect it to a file before replaying the UDP payloads:

```sh
build/maze-war-server --port 9101 --zombies 0 --debug > tests/transport-normalize-server.log 2>&1
```

## Manual Atari mixed-session capture

1. Start the server with debug logging and capture stdout to `tests/transport-normalize-server.log`.
2. Connect one Linux client and one Atari or emulator FujiNet client to the same server.
3. Move both players and trigger normal DELTA traffic from each side.
4. Inspect `tests/transport-normalize-server.log` for `transport accepted slot=` lines, including the normalized `format=` marker for Atari-originated traffic.
5. Keep the captured log with the manual session notes so mixed-session framing behavior can be compared against the automated smoke run.
