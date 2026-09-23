#!/bin/sh

# Render position and simulation position must stay separate.
#
# LOCX/LOCY used to be the drawn position, the collision position and the shot
# origin all at once, and it was not even a cell coordinate: INITMVE applied a
# pre-move offset and the end of the animation applied a post-move offset, so
# LOCX committed at the START of a left/up move and at the END of a right/down
# one. Any gameplay decision taken mid-animation therefore read a position that
# was a cell off, in a direction that depended on which way the actor faced.
#
# Now LOCX/LOCY is the simulation cell, stepped once per move at INITMVE, and
# RNDX/RNDY is the drawn position that keeps the staggered behaviour. This test
# pins both halves of that split.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
LAB="$ROOT_DIR/build/maze-war.lab"
XEX="$ROOT_DIR/build/maze-war.xex"

make -C "$ROOT_DIR" >/dev/null

fail() { echo "FAIL: $*" >&2; exit 1; }

grep -qE "^RNDX[[:space:]]+\.DS[[:space:]]+4" "$ATARI_SRC" \
  || fail "RNDX is not declared; there is no render-only position to draw from"
grep -qE "^RNDY[[:space:]]+\.DS[[:space:]]+4" "$ATARI_SRC" \
  || fail "RNDY is not declared"

# --- gameplay decisions read simulation state only ---------------------
# If any of these ever reads RNDX/RNDY it is deciding on a position that is a
# cell off for part of every move, which is the bug this split removes.
# RENDER_CHASE is deliberately absent: it is the bridge between the two and
# must read both.
for r in NET_AHEAD_FREE CKMV_AOK NLRC_IDLE; do
    # stop at the routine separator, or the body runs on into whatever
    # routine happens to follow and the check reports on the wrong code
    body=$(awk -v r="$r" '
        $1==r {on=1; next}
        on && /^;$/ {exit}
        on {print}
    ' "$ATARI_SRC" | head -60)
    if printf '%s' "$body" | grep -qE "RND[XY],"; then
        fail "$r reads render state; gameplay must decide on LOCX/LOCY"
    fi
    printf '%s' "$body" | grep -qE "LOC[XY]," \
      || fail "$r no longer reads LOCX/LOCY at all -- did the split invert?"
done


# --- remote actors interpolate through render state only -----------------
remote=$(awk '$1=="REMOTE_FOLLOW"{on=1} on{print} on&&/^RF_DONE/{seen=1} seen&&/JMP[[:space:]]+CHKSHOT/{exit}' "$ATARI_SRC")
printf '%s' "$remote" | grep -qE "RND[XY],X" \
  || fail "REMOTE_FOLLOW does not read render position; remote smoothing is not presentation-only"
printf '%s' "$remote" | grep -qE "JSR[[:space:]]+NET_AHEAD_FREE_RND" \
  || fail "REMOTE_FOLLOW does not path from the rendered cell"
printf '%s' "$remote" | grep -qE "STA[[:space:]]+NET_RCHASE_STEP" \
  || fail "REMOTE_FOLLOW can reach INITMOVE without marking the step render-only"
printf '%s' "$remote" | grep -qE "CMP[[:space:]]+LOCX,X" \
  || fail "REMOTE_FOLLOW does not chase authoritative simulation X"
printf '%s' "$remote" | grep -qE "CMP[[:space:]]+LOCY,X" \
  || fail "REMOTE_FOLLOW does not chase authoritative simulation Y"

commit=$(awk '$1=="NET_STAGE_COMMIT"{on=1} on{print} on&&/^NSC_NG/{exit}' "$ATARI_SRC")
printf '%s' "$commit" | grep -qE "STA[[:space:]]+LOCX,X" \
  || fail "NET_STAGE_COMMIT does not put remote authoritative X into simulation truth"
printf '%s' "$commit" | grep -qE "STA[[:space:]]+LOCY,X" \
  || fail "NET_STAGE_COMMIT does not put remote authoritative Y into simulation truth"
printf '%s' "$commit" | grep -qE "CPX[[:space:]]+NET_LOCAL_PID" \
  || fail "NET_STAGE_COMMIT remote simulation update is not guarded away from the local predicted slot"


respawn=$(awk '$1=="NET_RESP_APPLY_WRK"{on=1} on{print} on&&/^NRAW_X/{exit}' "$ATARI_SRC")
printf '%s' "$respawn" | grep -qE "JSR[[:space:]]+NET_AUTH_REPOS" \
  || fail "final respawn does not snap render+simulation to the spawn cell"
printf '%s' "$respawn" | grep -qE "JSR[[:space:]]+SETSTIL" \
  || fail "final respawn does not redraw a still actor at the spawn cell"

# Round reset erases all PM graphics before the first new-round snapshot.
# An unchanged spawn coordinate therefore still needs an explicit VBI redraw;
# coordinate-difference staging cannot infer that the pixels are gone.
grep -qE "^NET_REDRAW_MASK[[:space:]]+\.DS[[:space:]]+1" "$ATARI_SRC" \
  || fail "round redraw mask is not declared"
grep -qE "^NET_STAGE_REVEAL[[:space:]]+\.DS[[:space:]]+1" "$ATARI_SRC" \
  || fail "snapshot staging has no interrupt-safe round reveal flag"
commit_reveal=$(awk '$1=="NET_STAGE_COMMIT"{on=1} on{print} on&&/^NSC_NOREVEAL/{exit}' "$ATARI_SRC")
printf '%s' "$commit_reveal" | grep -qE "STA[[:space:]]+NET_REDRAW_MASK" \
  || fail "forced redraw is published before the VBI commits snapshot coordinates"
snap_apply=$(awk '$1=="NET_SNAP_APPLY"{on=1} on{print} on&&/^NSNAP_EXIT/{exit}' "$ATARI_SRC")
printf '%s' "$snap_apply" | grep -qE "STA[[:space:]]+NET_REDRAW_MASK" \
  && fail "mainline snapshot apply can race the VBI by publishing redraw early"
redraw=$(awk '$1=="VBI_RDR_LP"{on=1} on{print} on&&/BPL[[:space:]]+VBI_RDR_LP/{exit}' "$ATARI_SRC")
printf '%s' "$redraw" | grep -qE "LDA[[:space:]]+NET_DEAD_MASK" \
  || fail "round redraw does not skip dead/vacant slots"
printf '%s' "$redraw" | grep -qE "JSR[[:space:]]+NET_AUTH_REPOS" \
  || fail "round redraw does not reset render and simulation to authority"
printf '%s' "$redraw" | grep -qE "JSR[[:space:]]+SETSTIL" \
  || fail "round redraw does not restore the PM actor image"
grep -qE "^NET_ROUND_HUD" "$ATARI_SRC" \
  && fail "temporary WIN HUD marker still overlaps the shirt-colour missile"

# --- drawing reads render state only -----------------------------------
# ERASMAN is the one that has bitten hardest: it blanks two characters at the
# actor's cell, so it must bound-check and blank the cell that was DRAWN.
erasman=$(awk '$1=="ERASMAN"{on=1} on{print} on&&/ERMNXIT/&&++n>4{exit}' "$ATARI_SRC")
printf '%s' "$erasman" | grep -qE "RND[XY]," \
  || fail "ERASMAN does not bound-check the render cell; erase can miss the drawn cell"
printf '%s' "$erasman" | grep -qE "LOC[XY]," \
  && fail "ERASMAN still reads the simulation cell; draw and erase would disagree"

awk '$1=="NET_CALC_LOC"{on=1} on{print} on&&/RTS/{exit}' "$ATARI_SRC" \
  | grep -qE "RND[XY]," \
  || fail "NET_CALC_LOC builds the screen pointer from simulation state"

# --- the two render halves must sum to the simulation whole step -------
# This is the invariant that keeps RNDX/RNDY meeting LOCX/LOCY at rest. It is
# checked against the assembled bytes, not the source, because these tables are
# written as deliberately overlapping .BYTE runs and are easy to mis-edit.
python3 - "$LAB" "$XEX" <<'PYEOF'
import sys
lab, xex = sys.argv[1], sys.argv[2]
addr = {}
for line in open(lab):
    f = line.split()
    if len(f) >= 3:
        addr[f[2]] = int(f[1], 16)

d = open(xex, 'rb').read()
i = 2 if d[:2] == b'\xff\xff' else 0
segs = []
while i + 4 <= len(d):
    s = d[i] | (d[i+1] << 8); e = d[i+2] | (d[i+3] << 8)
    if s == 0xFFFF:
        i += 2; continue
    n = e - s + 1
    segs.append((s, e, d[i+4:i+4+n])); i += 4 + n


def tbl(name):
    a = addr[name]
    for s, e, b in segs:
        if s <= a and a + 3 <= e:
            return [x - 256 if x > 127 else x for x in b[a-s:a-s+4]]
    raise SystemExit("FAIL: %s not found in any segment" % name)


prvx, aftx, dirx = tbl("PRVXADD"), tbl("AFTXADD"), tbl("DIRXADD")
prvy, afty, diry = tbl("PRVYADD"), tbl("AFTYADD"), tbl("DIRYADD")

for ax, prv, aft, whole in (("X", prvx, aftx, dirx), ("Y", prvy, afty, diry)):
    got = [p + a for p, a in zip(prv, aft)]
    if got != whole:
        raise SystemExit(
            "FAIL: %s render halves %s+%s=%s do not sum to the simulation "
            "step %s; RNDX/RNDY would drift from LOCX/LOCY by a cell per move"
            % (ax, prv, aft, got, whole))

# dirs are 0=right 1=down 2=left 3=up
if dirx != [1, 0, -1, 0] or diry != [0, 1, 0, -1]:
    raise SystemExit("FAIL: whole-cell step tables are wrong: X=%s Y=%s"
                     % (dirx, diry))
print("  step tables: PRV+AFT == DIR for both axes, from the assembled bytes")
PYEOF

# --- the animation stagger belongs to render, the whole step to sim ----
initmve=$(awk '$1=="INITMVE"{on=1} on{print} on&&/MOVEST,X/{exit}' "$ATARI_SRC")
printf '%s' "$initmve" | grep -qE "ADC[[:space:]]+DIRXADD,Y" \
  || fail "INITMVE does not step the simulation a whole cell"
printf '%s' "$initmve" | grep -qE "STA[[:space:]]+RNDX,X" \
  || fail "INITMVE does not apply the pre-move offset to the render position"

udt=$(awk '$1=="UDTLOCS"{on=1} on{print} on&&/MOVSND/{exit}' "$ATARI_SRC")
printf '%s' "$udt" | grep -qE "LOC[XY],X" \
  && fail "UDTLOCS still moves the simulation position at the end of the \
animation; the whole step belongs at INITMVE, where the server applies it"

# --- the correction mechanism itself ------------------------------------
# LOCAL_FOLLOW walked LOCX/LOCY toward authority one cell per tick, which meant
# collision, occupancy and shot origin were all knowingly wrong for several
# ticks so the picture could catch up gently. With a render position to walk
# instead, that trade is gone and neither the walk nor its latch should remain.
# (The name still appears in a comment explaining the history; only code
# references are a failure.)
if grep -nE "^LOCAL_FOLLOW|JMP[[:space:]]+LOCAL_FOLLOW|JSR[[:space:]]+LOCAL_FOLLOW" \
     "$ATARI_SRC" >/dev/null; then
    fail "LOCAL_FOLLOW is still reachable; the simulation still walks to authority"
fi
grep -qE "NET_GLIDE_ON" "$ATARI_SRC" \
  && fail "NET_GLIDE_ON survives; the old glide latch is still live"

# The correction must land on the simulation immediately...
arm=$(awk '$1=="NET_RCHASE_ARM"{on=1} on{print} on&&/^NRA_X/{exit}' "$ATARI_SRC")
printf '%s' "$arm" | grep -qE "STA[[:space:]]+LOCX,X" \
  || fail "NET_RCHASE_ARM does not put the authoritative cell into the simulation"
printf '%s' "$arm" | grep -qE "RND[XY],X" \
  && fail "NET_RCHASE_ARM touches the render position; the actor would jump on \
screen, which is the snap this whole phase exists to remove"

# ...and the walk that follows must move the picture only. INITMVE has to
# honour that, or a chase step would advance the simulation a second time and
# march the actor away from authority one cell per tick.
initmve2=$(awk '$1=="INITMVE"{on=1} on{print} on&&/MOVEST,X/{exit}' "$ATARI_SRC")
printf '%s' "$initmve2" | grep -qE "LDA[[:space:]]+NET_RCHASE_STEP" \
  || fail "INITMVE does not honour NET_RCHASE_STEP; a render-chase step would \
step the simulation again"

chase=$(awk '$1=="RENDER_CHASE"{on=1} on{print} on&&/^RC_SNAP/{exit}' "$ATARI_SRC")
printf '%s' "$chase" | grep -qE "STA[[:space:]]+NET_RCHASE_STEP" \
  || fail "RENDER_CHASE does not mark its step as render-only"
printf '%s' "$chase" | grep -qE "CMP[[:space:]]+LOCX,X" \
  || fail "RENDER_CHASE does not chase the simulation cell"

# The walk must respect bricks. Nothing collides with the render position, but
# ERASMAN blanks the characters it walks onto and the map only repaints on a
# server delta, so a chase across a brick erases it from the screen while it
# goes on stopping the player.
printf '%s' "$chase" | grep -qE "JSR[[:space:]]+NET_AHEAD_FREE_RND" \
  || fail "RENDER_CHASE does not test for bricks; the walk would erase any it \
crosses and leave an invisible wall"
grep -qE "^NET_AHEAD_FREE_RND" "$ATARI_SRC" \
  || fail "NET_AHEAD_FREE_RND is gone; the chase would have to test from the \
simulation cell, which is not where it is walking"

echo "render state separation smoke passed"
