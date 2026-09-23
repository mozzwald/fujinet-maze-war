# 08-07 — Lobby AppKeys, URL validation, and startup routing

Status: complete and accepted on physical Atari/FujiNet and emulation on
2026-09-13. APKY-01, APKY-02, and plan 08-07 are closed.

## Atari AppKey integration

The Atari client now uses direct FujiNet SIO AppKey commands while NetStream
is stopped. It opens Maze War creator `$3022`, app `$03`, reads its username
from key `$00` and room from key `$01`, then falls back read-only to the
official Lobby `$0001/$01` username and application-ID `$03` launch keys. The commands
use device `$70`, unit 1, and always close an opened AppKey session.

The read buffer follows the firmware's count-prefixed layout: two count bytes,
up to 64 payload bytes, and one local terminator byte. Its 67 bytes alias
`NET_BRICK_BUF`, whose NetStream lifetime has not begun during startup. The
validated username, host, and numeric port are copied into persistent
configuration before that buffer is reused. The new state still ends below
the `$7F00` guard.

Username import applies the server's display-name rules: lowercase becomes
uppercase, only letters, digits, spaces, hyphen, and period survive, and the
result must contain 1–8 characters. Missing, malformed, empty, or unusable
values retain the generated/manual name default. Saving a manually edited
name writes a zero-padded 64-byte value to Maze War `$3022/$03/key $00`; a
write failure reports `NAME OK - APPKEY WAS NOT SAVED` without preventing the
current connection.

The selected-room URL parser accepts only the exact generated public endpoint
form `tcp://<configured-host>:<decimal-port>`. It rejects a different scheme
or host, empty or overflowing ports, ports outside the configured contiguous
room range, and every trailing path, query, fragment, or byte. Manual Direct
Connect never writes this public selected-room key; browser selection writes
Maze War `$3022/$03/key $01` only.

## Startup routing and fallback

A valid imported username and selected-room URL produce a one-shot autojoin.
The one-shot flag is consumed before the connection attempt, so an unavailable
stored endpoint returns through the normal diagnostic/title route instead of
forming an automatic retry loop. Missing or invalid AppKeys likewise retain
the normal title and Direct Connect flow.

Holding OPTION during startup bypasses autojoin and enters setup. The console
key is debounced by waiting for release before text input begins. The setup
controller moved to the isolated high-code segment to leave the fixed core
below `$6F00`; high code remains below `$9000` and the BASIC window.

## Configuration and documentation

The shared-Lobby namespace decision was corrected after integration: Maze War
owns AppKeys under creator `$3022`, application `$03`; `$00` is the player
name and `$01` is the selected room. The official Lobby namespace `$0001/$01`
is read-only compatibility input for its username and launch URL.
The Lobby API's `appkey` and its launch key are both Maze War's application ID
`$03`; there is no separate game-type setting. The
generator still accepts the existing `$0000` spelling for compatibility and
emits the byte value `$00`; wider values fail generation. Production builds
must use the registered Maze War game type. QA may use a temporary nonzero
key only with matching private AppKey fixtures.

`doc/lobby.md` records the namespace, direct-SIO commands, returned-data
layout, buffer ownership, sanitization, URL contract, boot behavior, and the
QA-versus-production registration rule.

## Verification completed

- `atari_appkey_smoke.sh` checks the SIO contract, buffer alias and memory
  bounds, exact raw-ASCII URL prefix, username sanitizer, and accepted/rejected
  URL cases.
- Existing generated-config, port-prompt, and memory-layout smokes were updated
  for the one-byte game type and high-code setup controller.
- Managed Atari/FujiNet-PC testing with SD AppKey fixtures loaded `mozz` and
  `tcp://127.0.0.1:9000`, autojoined the live room, and displayed `MOZZ` in the
  HUD. Both read lengths and payloads were confirmed in the FujiNet log.
- With no username AppKey file, AppKey OPEN/READ/CLOSE returned safely to the
  readable title and manual setup route.
- The complete `make test` suite passes.

## Physical acceptance

The user completed the AppKey battery on physical Atari/FujiNet and emulation
with private temporary game key `$2A`. Exact selected-room URLs autojoined both
configured ports, a manually entered username survived reboot, holding OPTION
bypassed autojoin, and an unavailable server returned to the menu. Mixed
gameplay showed no new regression.

The first selected-room attempt was rejected because the manually created
AppKey file contained a trailing newline. Removing it made the same build and
URL pass, confirming the intended exact-value validation and safe fallback.

Next: **08-08: `gpt-5.6-terra`, medium reasoning** for the isolated
asynchronous Lobby publisher. Do not begin it until the user authorizes the
step and switches models.
