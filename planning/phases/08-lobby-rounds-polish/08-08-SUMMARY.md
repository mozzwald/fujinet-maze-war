# 08-08 — Asynchronous QA Lobby publisher

Status: complete and accepted against the live QA Lobby, physical
Atari/FujiNet, and emulation on 2026-09-13. LOBY-01 and plan 08-08 are closed.

## Implementation

`server/lobby_publisher.[ch]` owns an opt-in publisher worker. The simulation
passes it only copied room metadata and the latest human occupancy. There is no
unbounded event queue: each room has one coalesced state slot. The worker owns
all `curl` child processes and uses a monotonic deadline to terminate a request
that is stuck in DNS, TLS, HTTP, or the remote endpoint. The main poll/tick
loop never waits for it.

`--lobby-enabled` defaults off and requires the explicit base URL, AppKey,
Atari client URL, and public host. Other controls choose game name, two-letter
region, per-room display names, refresh interval, request deadline, and
shutdown budget. Every room upserts its own `tcp://<public-host>:<port>` record
with `maxplayers: 4` and completed-human-only `curplayers`. Successful states
refresh before expiry; errors back off from one second to a capped thirty
seconds and do not interrupt gameplay.

On SIGINT/SIGTERM the server exits its listener loop normally, rejects further
online publisher work, and gives the worker only the configured bounded budget
to publish `status: offline` with zero players for each room. It then closes
the game sockets regardless of endpoint health. A delayed online request cannot
re-publish after shutdown begins.

## Pinned contract and validation

`doc/lobby.md` records the `POST /server` upsert body and requires the
official `201 Created` result. It also records the QA/production identity rule:
the temporary `$2A` AppKey and LAN endpoints used for private tests are not a
registration or authorization to publish publicly.

`tests/lobby_publisher_smoke.sh` runs a local fake endpoint and proves:

- normal two-room registration, distinct URLs, human-only occupancy update,
  periodic refresh, and bounded offline publication;
- no request when the publisher is not enabled;
- retry/logging after an HTTP 500 or malformed HTTP reply;
- an endpoint stalled for one second neither delays a 10 Hz HELLO/WELCOME nor
  keeps shutdown beyond its request and shutdown deadlines.

Focused validation passed:

- `make build/maze-war-server`
- `bash tests/lobby_publisher_smoke.sh`
- `bash tests/multi_room_isolation_smoke.sh`
- `bash tests/seat_occupancy_smoke.sh`

## Live QA and physical acceptance

The user published two rooms to `qalobby.fujinet.online` with Maze War AppKey
`3`. Both appeared in the QA Lobby, and its website tracked human player counts
correctly across joins and leaves. Launching through the Lobby client booted
the game and connected directly to the selected room on both physical
Atari/FujiNet hardware and emulation. No client regression was observed.

Next: **08-09: `gpt-5.6-sol`, high reasoning** for the bounded binary Lobby
parser and Atari menu memory work. The user has selected the model and
authorized the step.
