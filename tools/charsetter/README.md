# Maze War Lynx Charsetter

A static, offline browser editor for the graphics built into the Lynx client.
Open [index.html](index.html) directly in your browser; no web server, package
installation or internet connection is required.

## Edit and rebuild

1. Click **Load Tileset** and select
   [`clients/lynx/art/maze-war-tiles.json`](../../clients/lynx/art/maze-war-tiles.json).
2. Select an image from the library. Search by name or filter by category.
   Each animation frame is independently editable: **Dance 1–4** and
   **Explosion 1–4** (explosion, smoke, cloud, dissipating wisps).
3. Left click/drag to paint; right click to pick a pen. Keys `0`–`9` and `a`–`f`
   select valid pens. Use the arrow/mirror/clear tools and undo/redo buttons;
   Ctrl/Cmd+Z undoes and Ctrl/Cmd+Shift+Z redoes.
4. **Save Tileset** downloads the edited JSON. Replace the file above with
   that download, preserving its filename. Browsers usually save in Downloads
   and may add `(1)` to duplicate names; saving does not overwrite the source
   file automatically. A text editor can also edit the JSON directly.
5. From the repository root, rebuild for the live server:

   ```sh
   make -C clients/lynx SERVER_HOST=fujinet.online SERVER_PORT=5025
   ```

   Test `clients/lynx/build/maze-war-lynx.lnx` in Handy or on hardware. The build
   automatically regenerates graphics from your saved JSON. It does not load
   art files at runtime. Keep the JSON and regenerated headers together when
   committing artwork changes.

## Images and pen rules

The file has 57 entries. Rows are strings of hexadecimal pixel values, with
one digit per pixel. Image IDs, order, dimensions, modes and category bindings
are fixed renderer contracts, validated by both the editor and build. Names
are descriptive labels and may be changed in the JSON. Do not add/delete or
reorder frames. Pixel edits keep the same encoded size and runtime memory use.

| Category | Size | Entries / behavior |
| --- | --- | --- |
| Terrain | 8×5 | Floor, Brick even, Brick odd, Border wall; all 16 pens opaque |
| Player | 8×5 | Right, left, local right, local left; each a separate image |
| Dance | 8×5 | Dance 1–4; 8 VBL ticks (133 ms) per frame |
| Explosion | 12×9 | Explosion 1–4; same frame timing, clipped against maze walls |
| Effects | 2×2 / 2×1 | Projectile / Muzzle flash |
| HUD font | 3×4 | A–Z, 0–9, dash, dot, slash; blank space is implicit |

Player/dance pens are **roles**, not direct palette indices:

| Pen | Meaning |
| --- | --- |
| 0 | Transparent |
| 1 | Robe: P1 green, P2 blue, P3 pink, P4 yellow |
| 2 | Face (palette index 8) |
| 3 | White eye/local marker (index 15) |
| 4 | Shoes (index 6) |

The player selector previews recoloring without changing pixels. Player left
and right images are independent, as are their local-player marker variants:
editing one does not automatically mirror/update another. There is no separate
walking animation in the current client. Up/down movement uses the right
image, matching the existing renderer.

Terrain, explosion and effects use the game's fixed 16-color palette. Pen 0
is transparent for explosion/effects but **opaque for terrain**. The checkerboard
shows transparency; it is not included in the game. HUD glyphs use pens 0/1
(background/foreground), colored by the text renderer. The palette metadata is
read-only: changing it in JSON is rejected rather than silently ignored.

The large editing canvas preserves rectangular pixels. Neighbours repeats the
selected image 3×3 for checking seams; the small preview is enlarged 8× for
readability. Selecting a Dance or Explosion frame plays the whole group in the
animation panel; Pause lets you inspect its current frame. The actual game
plays explosions once and repeats the dance during its winner phase.

Sprite sizes and animation timings stay fixed. Maze collision, overlay cleanup,
clipping and placement still belong to the game renderer. Larger sprites or
additional frames require renderer changes, rather than just editing this file.
The built-in menu/results text font is part of TGI and is not in this tileset.

## Generated data and checks

`clients/lynx/tools/generate_art.py` converts the JSON into:

- `src/maze_sprites.h`: counted terrain packets, literal player/dance frames,
  projectile and muzzle constants;
- `src/death_sprites.h`: explosion/smoke packets;
- `src/font_art.h`: compact HUD glyph rows;
- `src/art_pixels.h`: unencoded pixels for host renderer test expectations;
  this header is not included in the ROM.

All literal rows retain the zero guards needed by real Suzy, and terrain uses
counted packets to prevent padding from painting outside a cell. Do not edit
generated headers. `make art` regenerates them; `make check-art` verifies them
without writing. Normal builds regenerate missing or stale outputs automatically.

```sh
make -C clients/lynx test
python3 tools/charsetter/browser_smoke.py
python3 clients/lynx/tests/handy_render_smoke.py --hud-checks --round-checks
python3 clients/lynx/tests/handy_render_smoke.py --death-checks
```

The first command needs Python 3, Node.js and a host C compiler. The browser
smoke test needs Chromium and writes its temporary profile, screenshot and logs
to `/tmp/maze-charsetter-*`. Handy tests use the existing local emulator setup;
see the Lynx test documentation. Their visual sampling assumes recognizable
player features and font glyphs; the host tests compare against editable pixels.

See [VALIDATION.md](VALIDATION.md) for the initial test results and artifacts.

The initial migration reproduces the previous ROM byte for byte. Generator
checks compare Python and JavaScript encoding, and host tests exercise caching,
wall clipping and cleanup against unencoded source pixels.

## Credits

Adapted from the FujiRealm Lynx tile editor in
[`mozzwald/fujirealm-game-demo`](https://github.com/mozzwald/fujirealm-game-demo),
`tools/tile-editor/lynx.html`, `lynx.js`, `lynx-model.js`, `style.css` and
`icons.css`. Its workflow and UI are retained here with Maze War bindings,
rectangular images, recoloring and animation previews.

FujiRealm's editor is built on [Charsetter](https://www.atari.org.pl/charsetter/)
by the **4Coloreditor Team**, used with their kind permission as recorded in
FujiRealm's credits. The original editor remains a useful Atari font/map tool.

The local Bootstrap Icons v1.11.3 subset is MIT licensed; see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). No CDN resources are loaded.
