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
