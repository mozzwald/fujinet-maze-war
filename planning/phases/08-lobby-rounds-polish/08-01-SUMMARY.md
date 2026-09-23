# 08-01 — Atari presentation-space reclamation

Status: hardware regression found and corrected locally; do not mark MEM-01
complete or begin 08-02 until the replacement Atari/FujiNet test is accepted.

## Hardware regression and correction (2026-09-11)

The first hardware test found a vertical trail of player-shirt fragments after
an actor moved up a maze column. Normal connection, movement, firing,
death/respawn vaporization, HUD names, and shirt-colour markers otherwise
worked.

This was a real 08-01 memory-reclamation regression. `SETSUIT` copies nine PM
bytes (`Y=8..0`) from each eight-byte `SUITS` animation frame. For every frame
except the last, byte nine is the following frame's leading zero. The final
`MOVEST=3`, up-facing frame had only seven explicit bytes and then used two
bytes after `SUITS`: unreachable `WINPLYR` happened to start with two zeros.
Removing it exposed `SMOKE`'s first byte, `$1C`, which `SETSUIT` wrote into
player PM memory. Repeated upward moves accumulated shirt-colour fragments
below the actor.

The final frame is now complete and `SUITS_PAD` provides the required ninth
zero between `SUITS` and `SMOKE`. `tests/pm_init_smoke.sh` assembles the XEX
and proves the sentinel is at `SUITS+$80`, immediately before `SMOKE`, and
contains `$00`. This makes the formerly implicit table-overrun dependency
visible and guarded. The fixed `$8000` display-data origin leaves the UI reserve
unchanged at 430 bytes.

## Removed unreachable code and data

Repository searches found no live reference to the net-only client’s blocking
`GAMEOVR` path, `WINPLYR`, `TITLDISP`, `TITLES`, or the old title-only setup
strings. The net main loop remains `NET_POLL` only; restart goes directly to
`START`.

Removed the unreachable game-over block, its PM graphic and result text, and
the unreachable title display/data. Retained `EVAPRTE`, which the live VBI
death path calls, along with `HOSTDISP`, `GAME`, `MAZEDAT`, HUD data, the
embedded charset, and all display buffers.

## Measured layout

| Item | Before | After |
|---|---:|---:|
| Atari XEX | 13,007 bytes | 12,294 bytes |
| NetStream XEX | 14,231 bytes | 13,518 bytes |
| Removed loaded bytes | — | 713 bytes |
| Reclaimed UI range | — | `$8042–$81EF` (430 bytes) |
| Loaded core end | — | `$6D8F` |
| Zero-page end | — | `$00E9` |
| NetStream state end | — | `$7EAF` |

`MAZEDAT` remains at `$81F0`; no existing maze pointer or screen timing moves.
The UI range is intentionally empty until a later feature has a concrete layout.

## Lifetime policy

The reclaimed range is available only for persistent UI/result strings and
tables. It must not be used for temporary AppKey or 189-byte Lobby-record
storage without a later proof that the mainline and VBI cannot access it at the
same time. Existing frame buffers are separate allocations and do not form a
contiguous 189-byte reserve. Frozen result data will remain live during network
receive, so it cannot alias receive staging.

## Verification

- Forced `make clean && make all` passed; Atari assembly completed in three
  passes.
- `tests/memory_layout_smoke.sh` now enforces live display-list placement,
  the empty UI reserve, loaded-core headroom, zero-page margin before handler
  `$EE`, NetStream-state headroom, display-buffer safety, and non-overlapping
  XEX segments.
- `tests/font_coverage_smoke.sh` was updated after title deletion and passed.
- Full host-network `make test` passed all 32 smokes.
- The managed Atari800/FujiNet-PC BIN-loader limitation recorded in 06-01
  remains: it cannot activate the embedded NetStream handler without a bootable
  ATR/container. It cannot serve as the normal-play regression check for this
  XEX change.

## Required user checkpoint

Please build the replacement commit and test normal boot/connect, movement,
firing, death/respawn vaporization, HUD names, colour markers, and repeated
upward player movement on real Atari/FujiNet against an emulator or Linux
client. Confirm no player-shirt fragments or vertical trails remain and no
accepted local-network feel/display behavior regressed.

After your acceptance, next is **08-02 — `gpt-5.6-sol`, high reasoning** for
the room-ownership and poll-loop isolation refactor. Pause for the user’s model
switch before beginning it.
