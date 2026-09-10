# Maze War for Atari 8-Bit with FujiNet NETStream Support

Original game Maze War, by Mark Price, from A.N.A.L.O.G. Magazine #36 (November 1985)

Networked Maze War with:
- an authoritative TCP server (`server/main.c`)
- an Atari client (`clients/atari/maze-war.asm`)
- a Linux/macOS terminal test client (`clients/linux/main.c`)
- a Linux/macOS SDL1 graphics client (`clients/linux/sdl_main.c`)

The packet format is documented in [doc/protocol.md](doc/protocol.md).

## Prerequisites

- `make`
- `mads` (Atari assembler) in `PATH`
- `gcc`
- `ncurses` development package (for terminal client link: `-lncurses`)
- `SDL 1.2` development package (for SDL graphics client; discovered with `sdl-config` when available)

On Debian/Ubuntu, Linux build deps are typically:

```bash
sudo apt install build-essential libncurses-dev
sudo apt install libsdl1.2-dev
```

On macOS with Homebrew, the host test clients can be built with:

```bash
brew install sdl12-compat ncurses
```

`mads` is not part of standard distro toolchains; install it separately and
ensure the `mads` binary is available in `PATH`.

## Build

Build everything:

```bash
make all
```

Or just:

```bash
make build/maze-war-net.xex
make build/maze-war-server
make build/maze-war-client
make build/maze-war-client-sdl
```

Output artifacts:
- `build/maze-war.xex`: raw Atari client program assembled from `maze-war.asm`
- `build/maze-war-net.xex`: `NSENGINE.OBX` + `maze-war.xex` concatenated
- `build/maze-war-server`: TCP authoritative server
- `build/maze-war-client`: terminal client with optional Linux evdev input
- `build/maze-war-client-sdl`: SDL1 graphics client

Clean:

```bash
make clean
```

## Server Usage

```text
build/maze-war-server [--port PORT] [--tick-hz N] [--zombies N] [--brick PATH] [--debug]
```

Defaults:
- `--port 9000`
- `--tick-hz 10`
- `--zombies 1` (0..3 accepted)
- `--brick server/brick_layout.txt`

Examples:

```bash
# default settings
./build/maze-war-server

# verbose output, custom tick and no AI zombies
./build/maze-war-server --port 9000 --tick-hz 15 --zombies 0 --debug
```

## Terminal Client Usage

```text
build/maze-war-client [--host IP] [--port PORT] [--pid N] [--input /dev/input/eventX] [--debug]
```

Defaults:
- `--host 127.0.0.1`
- `--port 9000`
- `--pid` optional (server also communicates player id in snapshots)
- `--input` optional Linux evdev path; when omitted, keyboard input is read from the terminal

Example:

```bash
./build/maze-war-client --host 127.0.0.1 --port 9000

# Linux evdev input is still available
./build/maze-war-client --host 127.0.0.1 --port 9000 --input /dev/input/event3
```

Controls (terminal client):
- Movement: arrow keys, `WASD`, or keypad arrows
- Fire: `Space`
- Respawn request: `R`
- Quit: `Esc`

Notes:
- On Linux, `--input` uses evdev (`/dev/input/eventX`) for key press/release events.
- Without `--input`, terminal input is portable but less precise for simultaneous held keys.
- You may need appropriate permissions for `/dev/input/eventX` (group membership
  or root).

## SDL Client Usage

```text
build/maze-war-client-sdl [--port PORT] [--host HOST] [--pid N] [--scale N] [--debug]
```

Defaults:
- `--port 9000`
- `--host 127.0.0.1` (used as pre-filled prompt text; client asks for hostname at startup)
- `--pid` optional
- `--scale 4`

Example:

```bash
./build/maze-war-client-sdl --port 9000 --scale 4
```

Controls (SDL client):
- Movement: Arrow keys
- Fire: `Space`
- Quit: `Esc`

## Networking Overview

- Transport: TCP (Atari `NET_FLAGS=$05`, unchanged 57600 baud)
- Default server port: `9000`
- Max players: 4 total slots
- Server tick: fixed rate (`--tick-hz`, default 10 Hz)
- A disconnected Linux client exits; restart it to reconnect. The Atari returns
  to its host prompt after its silence watchdog expires.
- Sequence numbers: 8-bit packet seq for ordering/duplicate filtering

Core packet flow:
1. Client sends `DELTA` input packets (`0x41`) with joystick+trigger state.
2. Server applies input to authoritative world state.
3. Server broadcasts periodic `SNAPSHOT` packets (`0x40`) with all player
   positions, joystick state, and scores.
4. Server sends `BRICK_FULL` (`0x50`) on connect, then `BRICK_DELTA` (`0x51`)
   for destroyed bricks.
5. Server sends `SHOT` (`0x42`) and `RESPAWN` (`0x52`) events as gameplay
   changes occur.

See [doc/protocol.md](doc/protocol.md) for byte-level packet layout.

## Transport Debug Workflow

Run both transport smoke checks before manual Atari or FujiNet validation:

```bash
bash tests/transport_normalize_smoke.sh
bash tests/transport_counters_smoke.sh
```

During mixed-session debugging, start the server with `--debug` and capture both `transport accepted slot=` and `transport summary slot=` lines so transport framing failures can be separated from later gameplay or reconciliation faults. See [tests/README-transport-validation.md](tests/README-transport-validation.md) for the capture flow.

## How Client/Server Work Together

- The server is authoritative for movement, shots, bricks, collisions, and
  scoring.
- Clients are input/render frontends: they send control state and render what
  the server publishes.
- On join, a client is assigned a slot (player id), receives full brick state,
  then follows snapshots/events.
- AI zombies are simulated on the server in unused slots (configurable with
  `--zombies`).
- The Atari and Linux clients speak the same protocol, so both can connect to
  the same server instance.
