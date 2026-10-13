# Initial Maze War art migration validation

Validated 2026-10-13. The source JSON reproduces the accepted graphics; no
artwork or runtime behavior changes are intended in this migration.

- Compared each original terrain, wizard/dance and explosion encoded array
  against the new generator: all bytes match, including row guards.
- Rebuilt `clients/lynx/build/maze-war-lynx.lnx` for `fujinet.online:5025` and
  compared the whole ROM against the pre-migration cartridge: identical.
  SHA-256: `b931d5b4c186a45390e7b7ce9281f22ddabcae15397b6ee81f80613721d8762c`.
- Layout checker passed: SCBs `$B500`, text `$B400`, 27 bytes before the
  **full reserved 2 KB stack**, unchanged. The JSON and host pixel oracle
  are not stored in runtime RAM.
- `make -C clients/lynx test all SERVER_HOST=fujinet.online SERVER_PORT=5025`
  passed all command, protocol, movement, render/cache, death, round and sound
  checks, plus the Python art tests and JavaScript model tests.
- Generator/browser checks cover invalid dimensions, bindings, palette metadata
  and pens; the Python and JavaScript encoders agree for all 57 images.
- In an isolated `/tmp` copy, changed **every pixel** in every image (incremented
  each pen modulo its allowed pen count), then ran the same test/build command.
  It passed, including automatic header regeneration and layout checks.
  Artifacts: `/tmp/maze-edited-art-1k6d2dm4` (temporary, not a build dependency).
- `python3 tools/charsetter/browser_smoke.py` passed in Chromium using the
  actual page offline: load, all 57 library entries, rectangular painting,
  undo/redo, transforms, P2 recoloring, all four animation frames, JSON export
  and edit preservation. Screenshot/logs: `/tmp/maze-charsetter-ql55jg_j`.
- `python3 clients/lynx/tests/handy_render_smoke.py --hud-checks --round-checks`
  passed with the existing Handy build and a private local test peer: full
  eight-character names, 0/9/10/11 score rollover, spacing, loser smoke,
  winner dance, score screen and restoration after diagnostics.
  Artifacts: `/tmp/maze-suzy-game-7_9u9i3i`.
- `python3 clients/lynx/tests/handy_render_smoke.py --death-checks` passed:
  explosion/smoke, walking through the cloud, player layering, cleanup,
  duplicate-event suppression and final respawn.
  Artifacts: `/tmp/maze-suzy-game-r4nbgovj`.

These emulator checks use localhost peers; the configured live endpoint is
verified but not contacted. No firmware changes or new hardware validation
were needed for the byte-identical initial ROM.

The host CPU oracle uses unencoded source pixels, preserving its independence
from Suzy packet encoding. Tests now compare against the editable artwork
rather than fixed hat/eye/brick pixels, and allow identical custom animation
frames. The round test also supplies an actual open map when authorizing its
post-round fixture, so it verifies restored player art rather than unknown-map
bricks.

## Shawn Jefferson 4×6 font import

The version 2 tileset replaces the 39 HUD glyphs with the exact 4×6 black-pixel
shapes from `shawnjefferson22/lynx-4x6-4x8-font`, revision
`f1dbdd4eb80c9913ab014126c0b6ca8cf3004407`. All glyph rows, including top/spacing
pixels, matched their source bitmaps. All non-font entries were compared against
the local pre-import JSON and preserved, including the user's uncommitted art.
O and 0 retain the supplied shapes in independently editable entries.

- Python art tests, six JavaScript model checks and the complete Lynx host suite
  passed. Tests cover editing O/0 independently and the fourth glyph column.
- Chromium editor test passed with the full 4×6 grid:
  `/tmp/maze-charsetter-2cqazdmx`.
- Handy HUD/round checks passed: eight-character names, single-digit score
  rollover, four-pixel slot gaps, winner/loser presentation and results restored
  after diagnostics. Artifacts: `/tmp/maze-suzy-game-hdaof_ev`.
- Handy startup checks with ACK noise passed: AppKey name resolution, Lobby
  listing/selection, selected endpoint and HELLO/WELCOME/name handoff.
  Artifacts: `/tmp/maze-lynx-startup-dc8wobod`.
- The three diagnostic rows now use y=84/90/96; decoding the rendered 4×6 hex
  fields was verified from Handy's screenshot.
- Built for `fujinet.online:5025`. ROM SHA-256:
  `1bb8dc43bf03e31995a35f7562408afbd3dd76ee3c942a624acc4975239a3521`.
- Graphics layout passed with SCBs `$B500`, text `$B400`, and 27 bytes before
  the unchanged full reserved 2 KB stack. Six-row text uses a 493-byte temporary
  bitmap in the existing phase scratch union, replacing the separate 329-byte
  buffer. Command operations finish before menu text packing; maze drawing
  finishes before HUD packing. The resident TGI sprite call waits for Suzy to
  finish, and text packing/drawing makes no serial callbacks, so those scratch
  lifetimes do not overlap.

The full 95-row maze is retained. HUD text now occupies y=95–100, leaving
its local-player underline at y=101. This import intentionally changes the
previous byte-identical migration ROM. Real-hardware font readability remains
for the user's next test.
