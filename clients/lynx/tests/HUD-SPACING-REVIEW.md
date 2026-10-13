# Lynx HUD spacing review

The initial investigation below preceded the approved single-digit layout.
See the implementation section for the current behavior.

## Current pixel budget

`render.c` gives each player 40 of the 160 screen pixels. The 3x4 font has
a four-pixel character pitch. Each entry contains eight name characters and
two score digits, consuming all ten pitches. The map occupies y=0..94; text
occupies y=97..100, with the local-player underline at y=101.

Within a slot, the name uses visible columns 0..30, column 31 is blank,
the score uses 32..38, and column 39 is blank. A literal extra space would
require 44 pixels per player (176 total). Appending a separator character
has the same problem. Full eight-character names must be preserved.

## Options

| Option | Fit and cost | Tradeoff |
| --- | --- | --- |
| Thin vertical separators | Draw at x=39,79,119 in the existing blank columns; preserve all name and score pixels | Easiest first experiment. Makes boundaries explicit, but adds no blank breathing room and can touch the next name's first stroke. |
| Single-digit ordinary scores with redistributed space | Live scores are 0..9: server kill limit is at most 10. Eight name glyphs plus one score glyph use 35 visible pixels at the existing pitch, leaving five pixels to distribute between name/score and entries | Better actual spacing. A winner can reach 10 during the celebration, before the results screen, so it needs an explicit two-digit exception (temporarily narrower gaps) rather than truncation. Vacant `--` also needs a deliberate representation. |
| Two HUD lines | Needs at least nine pixels for two four-pixel text rows and a gap, versus seven currently available | More room, but requires changing maze geometry or shrinking its cells; larger scope than a HUD separator. |
| Narrower score font | Two narrower digits could recover pixels while preserving 00..10 | Extra font/drawing work, with worse digit legibility. Avoid shrinking the already small name font. |

Recommendation: try the three one-pixel separators first, preserving the
current full-name/two-digit layout and maze. If they still look crowded,
use single-digit ordinary scores and dedicate the recovered width to blank
spacing, with an explicit 10-point winner exception.

Any separator should be drawn through the existing Suzy/TGI path inside
`draw_scores_if_changed`, after the HUD background clear, on both cached
display pages. It should redraw only with that HUD, not every gameplay frame.
Keep the local underline and playfield untouched. At most three tiny extra
sprites per HUD refresh should have little cost; that is an expectation,
not a measured benchmark. Review full eight-character names, all four occupied
slots, vacant slots, scores 0/9/10 and the round transition in Handy before
selecting a layout.

## Color update validation

Player palette order is now green/blue/pink/yellow (indices 10,14,3,9),
shared by sprites, HUD and round results. Existing four-player raster checks
were updated; all host tests and graphics placement/2 KB stack checks pass.
Installed Handy smoke passes; before.png visibly confirms all four colors
in sprites and HUD. Artifacts: /tmp/maze-suzy-game-qxv_0gjo. Live-endpoint ROM
SHA-256: 6f4ac0935a07361c2afb25667a59ac28f4dc022897ded40210bf9b8cc096a21c.
No HUD layout change was implemented or measured.

## Implemented single-digit HUD

The user selected single-digit scores with modulo-10 rollover, including the
winning score: 0=0, 9=9, 10=0, 11=1. The underlying score stays unchanged.
Each 40-pixel slot now contains eight name glyphs plus one score glyph, using
visible columns 0..34. Columns 35..39 are blank, providing a five-pixel gap
before the next player's name. Vacant entries use one `-`. The local underline,
maze geometry and existing cached Suzy drawing path are unchanged.

Round results still show both digits for 10 and above, right-aligned, with a
blank tens position for 0..9 instead of a preceding zero. This changes only
score presentation; it does not raise the current server/client kill-limit
validation of 10. Gameplay display already handles higher snapshot scores.

Handy verification command:

```sh
python3 clients/lynx/tests/handy_render_smoke.py --hud-checks --round-checks
```

Checks exercise 0/9/10/11 in each score position, preserve the eighth name
character, verify the inter-entry blank columns, retain the winning tens
digit on the results screen and reject leading zeroes on single-digit results.

Focused HUD/round Handy checks passed on the rebuilt live-endpoint ROM.
Artifacts: /tmp/maze-suzy-game-umz1z7ej (hud-rollover-0/1.png and
round-scores.png). Current ROM SHA-256:
b931d5b4c186a45390e7b7ce9281f22ddabcae15397b6ee81f80613721d8762c.
Graphics placement still passes with the full reserved 2 KB stack.
