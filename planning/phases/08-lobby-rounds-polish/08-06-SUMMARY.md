# 08-06 — Build configuration and reachable title/direct-connect UI

Status: implementation complete on 2026-09-12; physical Atari/FujiNet
acceptance is pending. CONF-01 and plan 08-06 remain open until that checkpoint
passes.

## Delivered behavior

`make build/maze-war-net.xex` now accepts `HOST`, `ROOM_PORT_BASE`,
`ROOM_COUNT`, `DEFAULT_PORT`, `LOBBY_BASE`, `MAZEWAR_APPKEY`, `KILL_LIMIT`, and
`BUILD_FLAVOR` (`LAN`, `QA`, or `PRODUCTION`).

`scripts/generate_atari_config.py` validates every value before producing the
two MADS includes: constants must be known before source assembly, while the
immutable host, port text, and Lobby base bytes are emitted in the measured
high-code segment. Build values travel to the generator through Make's exported
environment, so command-line values are not interpolated into a shell recipe.
The writer changes a file only when its contents change, allowing Make to leave
the XEX untouched for an identical configuration.

The Atari starts at a 40-column `HOSTDISP/HOSTSCR` title rather than immediately
opening an editor. It shows the username, the configured pre-connect
`FIRST TO n KILLS WINS` default, `RETURN: PLAY`, and `OPTION: SERVER SETUP`.
RETURN starts the existing direct TCP connection using the saved host and
validated numeric port. OPTION opens the existing host/port/name editor;
ESC cancels back to the title, and invalid ports retain the existing useful
message. Failed connection diagnostics return to the title and remain visible.
No AppKey is read or written by this phase, and the authoritative handshake
still replaces the displayed default kill limit once a room is joined.

The configuration data and title flow extend the isolated high-code segment to
`$8BC9` (below the newly enforced `$8C00` bound); core ends at `$6ED5`, zero
page at `$00E9`, and persistent NetStream state at `$7EED`. No zero-page or
per-player state was added.

## Verification

- `atari_build_config_smoke.sh` byte-inspects a QA build with a different host,
  three-port range, default port, Lobby base, appkey, flavor, and kill limit;
  checks unchanged settings do not rebuild the XEX; and rejects invalid host,
  port range/default, kill limit, and appkey inputs.
- `atari_port_prompt_smoke.sh` and `memory_layout_smoke.sh` pass.
- MCP-managed Atari XL execution reached the title loop, OPTION entered the
  editor, and ESC returned to the title. The raw `HOSTSCR` bytes confirmed the
  ROM-character-set title content; the MCP text renderer does not decode that
  character set reliably.

## Required physical checkpoint

1. Build the network XEX with the desired LAN values, for example
   `make HOST=192.168.1.120 ROOM_PORT_BASE=9000 ROOM_COUNT=1 DEFAULT_PORT=9000 build/maze-war-net.xex`.
2. On real Atari/FujiNet, confirm the title is stable and readable, OPTION
   opens setup only once per press, and ESC returns to the title.
3. Edit host, port, and name. Confirm RETURN from the title joins the selected
   direct TCP server, invalid port text stays in setup with its diagnostic, and
   an unavailable server returns to the title with its connection diagnostic.
4. Confirm a normal game OPTION leave still returns safely to this title and a
   later RETURN reconnects without a reset or stale game state.

After acceptance, mark CONF-01 and 08-06 complete. The conditional next step
is **08-07: `gpt-5.6-sol`, high reasoning** for AppKey direct-SIO buffers, URL
validation, and startup recovery. Pause for the user's model switch before
starting it.
