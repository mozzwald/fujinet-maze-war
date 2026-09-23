# Stack Research

**Domain:** Atari 8-bit FujiNet networked multiplayer game with authoritative C server and MADS assembly client
**Researched:** 2026-04-07
**Confidence:** MEDIUM-HIGH

## Recommended Stack

### Core Technologies

| Technology | Version | Purpose | Why Recommended | Confidence |
|------------|---------|---------|-----------------|------------|
| MADS | 2.1.5+ pinned to one repo-documented release | Build the Atari assembly client | This repo is already MADS-native, the syntax in `clients/atari/maze-war.asm` is written for it, and current official docs still position MADS as the standard cross-assembler for serious Atari 8-bit assembly work. Do not switch the gameplay client to cc65 unless you are willing to rewrite build assumptions and macro usage. | HIGH |
| FujiNet NetStream handler (`NSENGINE.OBX`) | Pin the handler binary shipped with the repo and record its matching upstream commit/tag | Atari-side transport glue between game code and FujiNet networking | For this project, NetStream is the interoperability boundary that matters. The right pattern is to treat the handler as a versioned binary dependency and keep the game protocol above it stable. That avoids mixing game bugs with handler regressions. | MEDIUM |
| FujiNet firmware PC target, not the old standalone `fujinet-pc` repo | Current `fujinet-firmware` PC build matching the handler/hardware generation you test against | Emulator-side FujiNet implementation for Linux/macOS/Windows | The standalone `fujinet-pc` repository is explicitly deprecated and folded into `fujinet-firmware`. In practice, “FujiNet-PC” is still the right workflow concept, but the upstream source of truth is now the firmware monorepo. | HIGH |
| Altirra emulator | 4.40+ for the primary debug loop | Cycle-aware Atari debugging, breakpoints, memory inspection, and reproducible emulator runs | For assembly-heavy Atari gameplay debugging, Altirra remains the most practical primary debugger. The repo’s own reference material already leans on Altirra hardware behavior, and Altirra custom-device support is part of the standard FujiNet emulator workflow. | MEDIUM |
| `fujinet-emulator-bridge` / NetSIO bridge path | Current upstream bridge scripts/config matching your Altirra setup | Connect Altirra to FujiNet emulation cleanly during desktop testing | This is the standard interop layer when you want emulator-based Atari testing with FujiNet semantics instead of testing every loop on physical hardware. Keep it outside gameplay logic and treat it as lab infrastructure. | MEDIUM |
| Authoritative server in ISO C / POSIX UDP | C17 with `gcc` or `clang`; ship `gcc` builds first | Server simulation, slot assignment, AI zombies, authoritative state broadcast | The repo already uses a small single-process C UDP server. That is the right stack here because the simulation is simple, latency-sensitive, easy to inspect with packet tools, and does not benefit from adding heavier frameworks. | HIGH |
| Linux protocol/test client in C | C17 with `ncurses` 6.x for text mode | Fast local multiplayer testing without the Atari/emulator in every loop | Keep a dumb Linux client that speaks exactly the same wire protocol as the Atari client. It shortens debugging cycles and isolates protocol/server bugs from Atari rendering and timing bugs. | HIGH |

### Supporting Libraries

| Library | Version | Purpose | When to Use | Confidence |
|---------|---------|---------|-------------|------------|
| `ncurses` | 6.x | Text-mode Linux test client UI | Keep this for protocol and gameplay verification on one Linux box. It is the fastest way to exercise joins, movement, shots, respawns, and zombie replacement. | HIGH |
| SDL2 | 2.30+ | Optional richer Linux visual test client | Use this only if you want a maintained graphical Linux client. New work should target SDL2, not SDL 1.2. | MEDIUM |
| Python | 3.11+ | Emulator bridge scripts, packet tooling, replay/test harnesses | Use for harnesses and bridge plumbing, not for the authoritative game server. | MEDIUM |
| `libasan` / `ubsan` | current distro toolchain | Memory and UB detection in server/client debug builds | Turn these on for Linux debug targets because protocol code and buffer framing bugs are common and cheap to catch this way. | HIGH |

### Development Tools

| Tool | Purpose | Notes | Confidence |
|------|---------|-------|------------|
| GNU Make | Single-command build orchestration | Keep `make` as the top-level entrypoint. Extend it with `debug`, `sanitize`, `run-server`, and `run-local-match` targets instead of adding a new build system. | HIGH |
| `gcc` | Build server and Linux clients | Use `-std=c17 -Wall -Wextra -Werror` for CI and add a debug flavor with `-O0 -g3 -fsanitize=address,undefined`. | HIGH |
| `gdb` | Server/client debugging | Required once you start chasing desync, stale packet, and zombie handoff bugs. | HIGH |
| `tcpdump` and Wireshark | UDP packet capture and protocol verification | Make packet captures part of the debugging workflow. Atari/FujiNet bugs are often easier to prove on the wire than from symptoms. | HIGH |
| Altirra monitor/debugger | Atari memory/register stepping | Use for frame-loop, player state, and reconciliation debugging. This is the primary Atari-side debugger, not printf-style screen instrumentation. | MEDIUM |
| Physical FujiNet hardware | Final interop validation | Use after emulator-loop fixes are stable. Do not make hardware the default inner-loop test path. | HIGH |

## Installation

```bash
# Debian/Ubuntu base toolchain
sudo apt install build-essential make libncurses-dev libsdl2-dev
sudo apt install gdb valgrind tcpdump wireshark python3 python3-venv

# Optional but recommended for MADS self-builds and tooling around it
sudo apt install fp-compiler

# Repo build
make all

# Recommended debug build pattern for C targets
make clean
CC=gcc CFLAGS='-std=c17 -O0 -g3 -Wall -Wextra -fsanitize=address,undefined' make build/maze-war-server build/maze-war-client
```

Manual installs to document in the roadmap/setup phase:

- MADS pinned to one known-good release in repo docs.
- Altirra pinned to one known-good version for emulator debugging.
- FujiNet firmware PC target / emulator bridge tooling pinned to one known-good upstream revision.

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| MADS for the Atari client | cc65 as the main Atari toolchain | Use cc65 only for small helper code, wrappers, or experiments. It is not the right default for this repo because the current client and NetStream integration are assembly-first. |
| C + raw POSIX sockets for server | C++/Rust/game-server frameworks | Use an alternative only if the server grows far beyond a small deterministic simulation. Right now that would add complexity without solving the real problem. |
| Altirra + FujiNet emulator bridge for the inner loop | Physical Atari + physical FujiNet for all testing | Use hardware-first only for final timing and interoperability checks. It is too slow and too opaque for day-to-day debugging. |
| `ncurses` text client plus packet tools | A full graphical Linux debug client as the primary tool | Use a graphical client only if you need renderer parity checks. For protocol work, text mode stays faster and more scriptable. |

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| Standalone `FujiNetWIFI/fujinet-pc` as the authoritative upstream | Its README says the repository is no longer used and has been folded into `fujinet-firmware`. Building process and fixes will drift if you anchor on the deprecated repo. | Treat the current PC target inside `fujinet-firmware` as the upstream source of truth. |
| SDL 1.2 for new Linux client work | It is legacy-era SDL and not the maintained path for new desktop work in 2026. Keeping existing code is fine short-term; expanding it is not. | Keep the current text client for most testing and use SDL2 if a maintained graphics client is worth it. |
| TCP as the gameplay transport | Head-of-line blocking and retransmission behavior work against low-latency movement/shooting loops. This project already assumes server-authoritative snapshots and input deltas, which fit UDP better. | Keep UDP for game traffic and version the packet format explicitly. |
| Emulator- or bridge-specific hacks in the gameplay protocol | They make hardware validation harder and cause hidden interop bugs when moving between Altirra, FujiNet-PC, and real FujiNet hardware. | Keep one repo-owned packet spec above NetStream and require all clients to follow it. |
| Replacing MADS with a generic 6502 assembler because it is easier to package | The cost shows up immediately in syntax incompatibilities, macro differences, and broken include/build assumptions. | Stay on MADS and document installation clearly. |

## Stack Patterns by Variant

**If you are doing the fast inner gameplay/debug loop:**
- Use MADS + Altirra + FujiNet emulator bridge + FujiNet PC target + localhost C server + Linux `ncurses` client.
- Because this is the shortest path to inspect Atari state, capture packets, and compare Atari behavior with a known-good Linux client.

**If you are validating real interoperability:**
- Use the same MADS-built `.xex`, the same C server, and physical FujiNet hardware on a real Atari.
- Because emulator success is necessary but not sufficient; the handler, timing, and network edge behavior still need one hardware pass.

**If you are debugging only server/protocol bugs:**
- Use the Linux server, Linux text client, packet capture, and optional replay scripts without the Atari in the loop.
- Because this isolates server authority, AI zombies, packet ordering, and slot lifecycle from Atari frame/render noise.

## Version Compatibility

| Package A | Compatible With | Notes |
|-----------|-----------------|-------|
| MADS 2.1.5+ | Existing repo assembly source | Pin exactly one release in docs/CI so macro and directive behavior stays stable across machines. |
| Repo `NSENGINE.OBX` | Repo Atari client and current protocol assumptions | Treat the handler as a versioned artifact. If it changes, revalidate packet timing and stream framing. |
| FujiNet PC target from `fujinet-firmware` | Altirra bridge workflow and NetStream testing | Do not assume the deprecated standalone `fujinet-pc` repo reflects current behavior. |
| UDP packet protocol in `doc/protocol.md` | Atari client, Linux client, authoritative server | This file should remain the source of truth for wire compatibility across all clients. |
| Existing SDL1 client | Current repo build only | Keep it only as a legacy tool until replaced or removed; do not expand dependence on SDL1.2. |

## Prescriptive Recommendation

The standard 2026 stack for this repo is:

1. Keep the Atari client in MADS assembly.
2. Keep the authoritative multiplayer server in plain C over UDP.
3. Keep a Linux text-mode protocol client in C as a first-class test tool.
4. Standardize the emulator workflow on Altirra plus the FujiNet emulator bridge and the current FujiNet PC target from `fujinet-firmware`.
5. Treat NetStream and the packet protocol as explicit compatibility boundaries with pinned versions.
6. Use hardware FujiNet validation as a final gate, not as the main development loop.

That is the stack most likely to keep this project interoperable, debuggable, and faithful to Atari/FujiNet reality without introducing unnecessary modern-tooling churn.

## Sources

- Repo source: `/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/Makefile` and `/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/README.md` and `/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md` and `/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/ref/netstream_api.md` — verified current repo stack and interoperability assumptions. Confidence: HIGH.
- Mad-Assembler docs: https://mads.atari8.info/mad-assembler-mkdocs/en/ — verified current MADS docs and latest documented release line. Confidence: HIGH.
- Mad-Assembler source README: https://raw.githubusercontent.com/tebe6502/Mad-Assembler/master/README.md — verified MADS remains the upstream Atari-focused cross-assembler. Confidence: HIGH.
- FujiNet firmware README: https://raw.githubusercontent.com/FujiNetWIFI/fujinet-firmware/master/README.md — verified current upstream source of truth for FujiNet and Atari `N:`/network support. Confidence: HIGH.
- FujiNet-PC README: https://raw.githubusercontent.com/FujiNetWIFI/fujinet-pc/master/README.md — verified the standalone repo is deprecated and folded into `fujinet-firmware`. Confidence: HIGH.
- Altirra Hardware Reference Manual already vendored in repo: `/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/ref/Altirra_Hardware_Reference_Manual.md` — supports recommending Altirra-centered hardware/debug workflow. Confidence: MEDIUM.

---
*Stack research for: Atari 8-bit FujiNet multiplayer stack*
*Researched: 2026-04-07*
