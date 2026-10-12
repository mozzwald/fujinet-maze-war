; Benchmark-only 24-bit microsecond clock. Timers 1 -> 3 -> 5 are unused by
; the client's video (0/2) and serial (4) drivers. No timer interrupts.
.export _bench_clock_init, _bench_now
.importzp sreg
.include "lynx.inc"
.code
_bench_clock_init:
    stz $fd04+1
    stz $fd0c+1
    stz $fd14+1
    lda #$ff
    sta $fd04
    sta $fd04+2
    sta $fd0c
    sta $fd0c+2
    sta $fd14
    sta $fd14+2
    stz $fd04+3
    stz $fd0c+3
    stz $fd14+3
    lda #$1f
    sta $fd14+1
    sta $fd0c+1
    lda #$18
    sta $fd04+1
    rts

_bench_now:
@retry:
    lda $fd14+2
    sta sreg
    ldx $fd0c+2
    lda $fd04+2
    cpx $fd0c+2
    bne @retry
    ldy $fd14+2
    cpy sreg
    bne @retry
    eor #$ff
    pha
    txa
    eor #$ff
    tax
    lda sreg
    eor #$ff
    sta sreg
    stz sreg+1
    pla
    rts
