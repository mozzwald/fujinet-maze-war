# 08-06 — Build configuration and reachable title/direct-connect UI

Status: implementation complete on 2026-09-12; physical Atari/FujiNet
acceptance is pending. CONF-01 and plan 08-06 remain open until that checkpoint
passes.

## Hardware regression follow-up

The first hardware trial found occasional body-less actors, particularly after
a respawn, plus remote movement that appeared to snap forward more often. A
first repair attributed the body loss to stale vacant-seat erases running after
a forced respawn redraw. It moved those erases ahead of redraw. The next real
hardware test showed that both symptoms persisted, disproving that diagnosis;
the added pre-redraw scan has been removed instead of retained as unexplained
VBI work.

The second review followed the earlier Phase 4 pointer-corruption evidence and
found another foreground/VBI scratch collision. `NET_RESP_APPLY_WRK` runs from
the VBI but saved its flags in `NET_RX_TMP0`. Foreground packet processing uses
the same byte while calculating per-slot shot/respawn buffer offsets and while
building a screen pointer for brick deltas. An NMI between a foreground store
and reload could therefore make it publish into the wrong packet slot or blank
an unrelated playfield cell. The PM shirt lives in separate player-missile RAM,
so such a bad playfield write could leave a shirt-only actor. Respawn flags now
live in the VBI-owned `NET_RESP_FLAGS` byte, and the ownership rule is guarded
by `death_render_smoke.sh`.

The movement review found that the Phase 4 cadence increase and its recovery
policy no longer agreed. Rendering now has capacity for roughly 15 cells/s
against 10 Hz authority, but `REMOTE_FOLLOW` still counted every successful
multi-cell recovery toward a forced snap and immediately snapped any gap of
three cells. Hardware stream batching can create that gap without corrupt
state. Legal recovery now walks every gap below the existing ten-cell
catastrophic guard and clears the failed-recovery count; only a blocked route
counts toward the three-attempt guard. This retains collision checks and all
four animation phases while removing the routine snap-forward path.

The targeted render, lifecycle, movement, and memory-layout checks pass after
the second repair.

### Follow-up physical result

The next real Atari/FujiNet test accepts the remote-follow portion: movement
lagginess is fixed. The shirt-only actor remains, so the scratch collision was
a valid safety fix but was not sufficient to explain the display defect. The
body can disappear while an actor is stationary after a stop, a respawn, a
corner stop, or firing, then returns as soon as that actor moves. The PM shirt
continues to display and no reliable trigger is known.

This regression was discovered during 08-06 testing but is now tracked as a
Phase 4 stationary playfield/body ownership issue. The next repair should first
trace the affected `GAMESCR` cells with actor `LOC`/`RND` state and attribute
the clearing write among stationary drawing, movement erasure, shots/bricks,
or respawn redraw. Do not add a continuous stationary redraw workaround before
the source is identified.

After `bdf4bbc` protected actor cells from stale shot cleanup and brick-delta
clears, a four-round mixed real Atari/FujiNet and emulator test without Zombies
showed one remaining shirt-only actor at a new-round spawn. See
`ref/screenshots/Screenshot from 2026-09-13 08-30-57_new-round-spawn.png`.
This is deferred to the Phase 4 trace work; it is not evidence to reopen the
accepted movement repair or to change 08-06 behavior without a reproducible
write sequence.

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
