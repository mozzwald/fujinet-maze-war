#!/bin/sh

# A network shot is painted directly into Atari playfield character memory.
# Its ownership must survive actor-effect changes, and it needs a bounded
# expiry because SHOT clear packets are transient rather than reliable events.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SRC="$ROOT_DIR/clients/atari/maze-war.asm"

make -C "$ROOT_DIR" build/maze-war.xex >/dev/null

fail() { echo "FAIL: $*" >&2; exit 1; }

grep -qE '^NET_SHOT_DRAWN[[:space:]]+\.DS[[:space:]]+1' "$SRC" \
  || fail "shot glyph ownership is not separate from ACTFLAG"
grep -qE '^NET_SHOT_TTL[[:space:]]+\.DS[[:space:]]+4' "$SRC" \
  || fail "authoritative shots have no per-slot stale-update watchdog"

apply=$(awk '$1=="NET_SHOT_APPLY"{on=1} on{print} on&&/^NSHOT_DEFER/{exit}' "$SRC")
printf '%s' "$apply" | grep -qE 'LDA[[:space:]]+NET_SHOT_DRAWN' \
  || fail "shot apply still trusts actor action flags to find a drawn glyph"
printf '%s' "$apply" | grep -qE 'JSR[[:space:]]+NET_SHOT_MARK_X' \
  || fail "drawing a shot does not publish renderer ownership"
printf '%s' "$apply" | grep -qE 'LDA[[:space:]]+#NET_SHOT_TTL_MAX' \
  || fail "active authoritative packets do not refresh shot lifetime"
printf '%s' "$apply" | grep -qE 'JSR[[:space:]]+NET_SHOT_CLEAR_X' \
  || fail "authoritative clear does not use ownership-based erase"

clear=$(awk '$1=="NET_SHOT_CLEAR_X"{on=1} on{print} on&&/^[[:space:]]*RTS/{exit}' "$SRC")
printf '%s' "$clear" | grep -qE 'JSR[[:space:]]+ERASHOT' \
  || fail "ownership-based clear does not erase the playfield characters"
printf '%s' "$clear" | grep -qE 'AND[[:space:]]+PLRMSKINV,Y' \
  || fail "ownership-based clear does not release the slot's draw bit"
mark=$(awk '$1=="NET_SHOT_MARK_X"{on=1} on{print} on&&/^[[:space:]]*RTS/{exit}' "$SRC")
printf '%s' "$mark" | grep -qE 'STA[[:space:]]+NET_SHOT_DRAWN' \
  || fail "shot draw helper does not publish renderer ownership"

# A player can enter a projectile's former screen cell before its clear reaches
# this client.  The clear must not blank that player's character-cell body;
# the PM shirt is separate memory and would otherwise remain by itself.
erase=$(awk '$1=="ERASHOT"{on=1} on{print} on&&/^ERSHXIT/{exit}' "$SRC")
printf '%s' "$erase" | grep -qE 'JSR[[:space:]]+ERASHOT_PAIR' \
  || fail "shot erase does not protect occupied character cells"
pair=$(awk '$1=="ERASHOT_PAIR"{on=1} on{print} on&&/^[[:space:]]*RTS/{exit}' "$SRC")
printf '%s' "$pair" | grep -qE 'LDA[[:space:]]+NET_DEAD_MASK' \
  || fail "shot erase can protect hidden rather than live actors"
printf '%s' "$pair" | grep -qE 'LDA[[:space:]]+LOCLO,Y' \
  || fail "shot erase does not compare the actor render pointer"
printf '%s' "$pair" | grep -qE 'CMP[[:space:]]+POINTR0' \
  || fail "shot erase does not compare against its target cell"

# Brick deltas clear their screen pair directly.  Preserve a current render
# cell while still clearing NET_MAP_CELLS, so an actor body owns the display
# until its normal erase exposes the now-empty map cell.
brick=$(awk '$1=="NET_BRICK_DELTA_APPLY"{on=1} on{print} on&&/^NBRK_X/{exit}' "$SRC")
printf '%s' "$brick" | grep -qE 'JSR[[:space:]]+NBF_ACTOR_HERE' \
  || fail "brick delta does not check actor display ownership"
printf '%s' "$brick" | grep -qE 'BCS[[:space:]]+NBRK_X' \
  || fail "brick delta still paints through an actor image"
brick_owner=$(awk '$1=="NBF_ACTOR_HERE"{on=1} on{print} on&&/^NBFA_NX/{exit}' "$SRC")
printf '%s' "$brick_owner" | grep -qE 'LDA[[:space:]]+RNDX,X' \
  || fail "brick delta does not protect an actor's render X cell"
printf '%s' "$brick_owner" | grep -qE 'LDA[[:space:]]+RNDY,X' \
  || fail "brick delta does not protect an actor's render Y cell"

watch=$(awk '$1=="NET_SHOT_WATCH_TICK"{on=1} on{print} on&&/^[[:space:]]*RTS/{exit}' "$SRC")
printf '%s' "$watch" | grep -qE 'DEC[[:space:]]+NET_SHOT_TTL,X' \
  || fail "watchdog does not age active shot refreshes"
printf '%s' "$watch" | grep -qE 'LDA[[:space:]]+NET_ROUND_PHASE' \
  || fail "MATCH_END does not force shot cleanup during intermission"
printf '%s' "$watch" | grep -qE 'LDA[[:space:]]+NET_SHOT_DRAWN' \
  || fail "intermission cleanup is not idempotent after all glyphs are gone"
printf '%s' "$watch" | grep -qE 'JSR[[:space:]]+NET_SHOT_CLEAR_X' \
  || fail "expired or intermission shots are not erased"

vbi_exit=$(sed -n '/^EXIT[[:space:]]/,/JMP[[:space:]]\+XITVBV/p' "$SRC")
printf '%s' "$vbi_exit" | grep -qE 'JSR[[:space:]]+NET_SHOT_WATCH_TICK' \
  || fail "shot watchdog is not owned by the VBI draw path"
printf '%s' "$vbi_exit" | grep -qE 'JSR[[:space:]]+NET_SHOT_APPLY_PEND' \
  || fail "authoritative shot updates are no longer applied after expiry"

echo "shot render lifecycle smoke passed"
