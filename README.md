# Maze War for Atari 8-Bit with FujiNet NETStream Support

Original game Maze War, by Mark Price, from A.N.A.L.O.G. Magazine #36 (November 1985)

Networked Maze War with:
- an authoritative UDP server (`server/main.c`)
- an Atari client (`clients/atari/maze-war.asm`)
- a Linux test client (`clients/linux/main.c`)
- a Linux SDL1 graphics client (`clients/linux/sdl_main.c`)

The packet format is documented in [doc/protocol.md](doc/protocol.md).

## Prerequisites

- `make`
- `mads` (Atari assembler) in `PATH`
- `gcc`
- `ncurses` development package (for Linux client link: `-lncurses`)
- `SDL 1.2` development package (for SDL graphics client link: `-lSDL`)

On Debian/Ubuntu, Linux build deps are typically:

```bash
sudo apt install build-essential libncurses-dev
sudo apt install libsdl1.2-dev
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
- `build/maze-war-server`: Linux UDP authoritative server
- `build/maze-war-client`: Linux ncurses/evdev client
- `build/maze-war-client-sdl`: Linux SDL1 graphics client

Clean:

```bash
make clean
```

## Linux Server Usage

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

## Linux Client Usage

```text
build/maze-war-client [--host IP] [--port PORT] [--pid N] --input /dev/input/eventX [--debug]
```

Defaults:
- `--host 127.0.0.1`
- `--port 9000`
- `--pid` optional (server also communicates player id in snapshots)

Example:

```bash
./build/maze-war-client --host 127.0.0.1 --port 9000 --input /dev/input/event3
```

Controls (Linux client):
- Movement: arrow keys, `WASD`, or keypad arrows
- Fire: `Space`
- Respawn request: `R`
- Quit: `Esc`

Notes:
- Client input uses evdev (`/dev/input/eventX`), not terminal keypress input.
- You may need appropriate permissions for `/dev/input/eventX` (group membership
  or root).

## Linux SDL Client Usage

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

Controls (Linux SDL client):
- Movement: Arrow keys
- Fire: `Space`
- Quit: `Esc`

## Networking Overview

- Transport: UDP
- Default server port: `9000`
- Max players: 4 total slots
- Server tick: fixed rate (`--tick-hz`, default 10 Hz)
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
