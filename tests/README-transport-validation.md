# Transport Validation

## Smoke scripts

Run the automated transport normalization checks from the repository root:

```sh
bash tests/transport_normalize_smoke.sh
bash tests/transport_counters_smoke.sh
```

`tests/transport_normalize_smoke.sh` builds `build/maze-war-server`, launches it with `--port 9101 --zombies 0 --debug`, captures stdout to a temporary server log, and fails unless the log contains `transport accepted slot=0` markers for `primary`, `swapped`, and `extra-41`.

`tests/transport_counters_smoke.sh` replays accepted and dropped DELTA variants against the same debug server contract and fails unless the captured `transport summary slot=0` line includes the expected counter values for `delta_swapped`, `delta_extra_41`, `drop_bad_joy`, `drop_stale_seq`, and `accepted_delta`.

To keep a copy of the server log beside other script output, run the same server command manually and redirect it to a file before sending the payloads over a connected TCP socket:

```sh
build/maze-war-server --port 9101 --zombies 0 --debug > tests/transport-normalize-server.log 2>&1
```

## Manual Atari mixed-session capture

1. Start the server with debug logging and capture stdout to `tests/transport-normalize-server.log`.
2. Connect one Linux client and one Atari or emulator FujiNet client to the same server.
3. Move both players and trigger normal DELTA traffic from each side.
4. Inspect `tests/transport-normalize-server.log` for both `transport accepted slot=` and `transport summary slot=` lines, including the normalized `format=` marker and the per-slot counters for Atari-originated traffic.
5. Keep the captured log with the manual session notes so mixed-session framing behavior can be compared against the automated smoke run.

TCP migration checks: `tcp_stream_smoke.sh` exercises fragmented/coalesced COBS
frames, checksum rejection, overflow recovery, short writes, EINTR, EAGAIN and
fatal sends. `tcp_transport_smoke.sh` exercises the real server's split/combined
input, FIN/RST handoff, full seats, handshake timeout, duplicate bind and restart.
Gameplay scripts use `tcp_frames.recv_frame` solely to recover message boundaries;
their own packet decoders and gameplay assertions still run.

Reliable-event checks: `reliable_event_stream_smoke.sh` exercises the
`0x53`/`0x45` ordered event stream by withholding the first ACK, sending a
duplicate ACK for revision 0, and requiring a byte-identical fast retransmit of
revision 1 before the normal retransmit timer is needed.
