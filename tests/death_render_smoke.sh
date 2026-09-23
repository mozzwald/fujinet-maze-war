#!/bin/sh

# How the Atari client takes an actor off the board.
#
# Two reported bugs turned out to be one line. ERASMAN returned with Y = 0, from
# its player-missile clear loop, and two callers reload the slot's mask bit
# through Y *after* the call. Both were therefore always clearing bit 0,
# whatever slot had actually been erased:
#
#   - Slots 1..3 never had their erase bit cleared, so a hidden slot was
#     re-erased every frame. An unoccupied slot sits at its placeholder cell
#     (1, slot+1) forever, so a live player standing on (1,2), (1,3) or (1,4)
#     had its characters blanked every frame and rendered as a sliver.
#   - Slot 0's erase request was destroyed by any other slot's erase earlier in
#     the same pass -- the pass runs 3 down to 0 -- so when slot 0 died with any
#     other slot hidden, which with fewer than four participants is always, the
#     erase never ran and the corpse stayed on the death cell.
#
# And the death animation: EVAPRTE was still here, but the only path to it hung
# off CHKSHOT, which the net client short-circuits to DONXTMN because shots are
# server-authoritative. So a death you detected yourself puffed into smoke and a
# death the server told you about just blinked out.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SRC="$ROOT_DIR/clients/atari/maze-war.asm"
TAB=$(printf '\t')

make -C "$ROOT_DIR" build/maze-war.xex >/dev/null

fail() { echo "FAIL: $1" >&2; exit 1; }

# --- ERASMAN preserves Y -----------------------------------------------------
erasman=$(sed -n '/^ERASMAN/,/^;$/p' "$SRC")
printf '%s' "$erasman" | grep -A2 -E "^ERASMAN" | grep -qE "TYA" ||
    fail "ERASMAN no longer saves Y; its callers read a slot mask through it"
printf '%s' "$erasman" | grep -B3 -E "^${TAB}RTS" | grep -qE "PLA" ||
    fail "ERASMAN no longer restores Y before returning"

# The contract only matters because these callers depend on it. If either stops
# reading a mask through Y after the call, this test is measuring nothing.
sed -n '/^CKMV_NEV/,/^CKMVSKP/p' "$SRC" | grep -qE "JSR${TAB}ERASMAN" ||
    fail "the move loop no longer erases hidden actors here"
sed -n '/^CKMV_NEV/,/^CKMVSKP/p' "$SRC" \
    | grep -A3 -E "JSR${TAB}ERASMAN" | grep -qE "PLRMSKINV,Y" ||
    fail "the move loop no longer clears the erase bit through Y after ERASMAN"
sed -n '/^NAR_OK/,/^$/p' "$SRC" \
    | grep -A12 -E "JSR${TAB}ERASMAN" | grep -qE "PLRMSKINV,Y" ||
    fail "NET_AUTH_REPOS no longer clears its masks through Y after ERASMAN"

# --- a server-reported death animates ---------------------------------------
pend=$(sed -n '/^NRW_PEND/,/^NRW_X/p' "$SRC")
printf '%s' "$pend" | grep -qE "JSR${TAB}ERASMAN" ||
    fail "a pending respawn no longer takes the wizard off the board"
printf '%s' "$pend" | grep -qE "ORA${TAB}#\\\$02" ||
    fail "a pending respawn no longer starts the evaporate"
printf '%s' "$pend" | grep -qE "LDA${TAB}#9" ||
    fail "the evaporate counter is not set from a pending respawn"
# It must NOT hide the actor here: the dead branch skips the animation entirely,
# so setting the hide would cancel the smoke before its first frame. ENDEVAP
# sets it when the smoke clears.
printf '%s' "$pend" | grep -E "ORA${TAB}PLRMSK,Y" | grep -q . &&
    fail "a pending respawn sets a mask bit directly; that cancels the animation"

# Foreground packet decode and the VBI may interrupt one another at any
# instruction. Respawn flags once lived in NET_RX_TMP0, which foreground RX
# also uses while calculating packet-buffer offsets and screen pointers.
respawn_apply=$(sed -n '/^NET_RESP_APPLY_WRK/,/^; Pending respawn/p' "$SRC")
printf '%s' "$respawn_apply" | grep -qE "STA${TAB}NET_RESP_FLAGS" ||
    fail "VBI respawn apply no longer saves flags in its owned byte"
if printf '%s' "$respawn_apply" | grep -qE "NET_RX_TMP0"; then
    fail "VBI respawn apply aliases foreground RX scratch again"
fi

# The animation has to be driven from the net frame loop, because the original
# path to it through CHKSHOT is short-circuited in this client.
grep -qE "^CHKSHOT${TAB}JMP${TAB}DONXTMN" "$SRC" ||
    fail "CHKSHOT is no longer short-circuited; re-check how EVAPRTE is reached"
sed -n '/^CKMV_NGU/,/^CKMV_NEV/p' "$SRC" | grep -qE "JSR${TAB}EVAPRTE" ||
    fail "nothing drives EVAPRTE, so a server-reported death will not animate"
sed -n '/^CKMV_NGU/,/^CKMV_NEV/p' "$SRC" | grep -qE "RTCLOK" ||
    fail "the evaporate runs every frame; COLESCE ran the effects every other"

# A respawn must not let a half-played evaporate follow the actor to its new
# cell, and a slot changing hands must not inherit one either.
sed -n '/^NRW_POSOK/,/^NRW_X/p' "$SRC" | grep -qE "AND${TAB}#\\\$FD" ||
    fail "a final respawn does not end a running evaporate"
sed -n '/^NET_ROLE_RESET/,/^;/p' "$SRC" | grep -qE "AND${TAB}#\\\$FD" ||
    fail "a slot handoff can inherit the previous occupant's evaporate"

# --- an evaporating actor is off the board for collision too -----------------
sed -n '/^NAF_OLP/,/^NAF_ONX/p' "$SRC" | grep -qE "AND${TAB}#\\\$02" ||
    fail "local collision still blocks on an actor the server has already killed"

echo "death render smoke passed"
