; Startup diagnostic: no Suzy access, heap, or extra BSS. Keep IRQs running
; during the scan so command-mode receive can still drain into its driver.
        .export _boot_guard, _boot_expected
        .import __CODE_RUN__, __RODATA_RUN__, __RODATA_SIZE__
        .importzp ptr1, ptr2

        .data
_boot_expected: .word 0          ; sealed from the linked ROM by the build

        .code
_boot_guard:
        pha                     ; stage, preserved across the checksum
        lda #<__CODE_RUN__
        sta ptr1
        lda #>__CODE_RUN__
        sta ptr1+1
        stz ptr2
        stz ptr2+1
        ldy #0
scan:   lda (ptr1),y
        clc
        adc ptr2
        sta ptr2
        bcc next
        inc ptr2+1
next:   inc ptr1
        bne compare
        inc ptr1+1
compare:
        lda ptr1
        cmp #<(__RODATA_RUN__ + __RODATA_SIZE__)
        bne scan
        lda ptr1+1
        cmp #>(__RODATA_RUN__ + __RODATA_SIZE__)
        bne scan
        lda ptr2
        cmp _boot_expected
        bne fault
        lda ptr2+1
        cmp _boot_expected+1
        bne fault
        pla
        tax
        ; Small top-edge ticks mark the last successful checkpoint on both
        ; pages, including freezes that do not change the checksum. These
        ; CPU writes stay far below the hardware-register overlays.
        lda #0
        ldy #15
clear_mark:
        sta $C038,y
        sta $E018,y
        dey
        bpl clear_mark
        ldy #0
        lda #$FF
mark:   sta $C038,y
        sta $E018,y
        iny
        iny
        dex
        bne mark
        rts

; Program/constant corruption: bypass the TGI driver and Suzy entirely.
; Fill only the CPU-addressable video page at $C038, then display stage
; white bars. This deliberately stops rather than continuing damaged code.
fault:  pla
        sei
        asl
        asl
        asl
        sta ptr2                ; eight bytes per bar (16 screen pixels)
        lda #$38
        sta ptr1
        sta $FD94
        lda #$C0
        sta ptr1+1
        sta $FD95
        stz $FDA0
        stz $FDB0
        lda #$0F
        sta $FDA1
        lda #$FF
        sta $FDB1
row:    ldy #79
pixel:  lda #0
        cpy ptr2
        bcs store
        tya
        and #7
        beq store
        lda #$11
store:  sta (ptr1),y
        dey
        bpl pixel
        clc
        lda ptr1
        adc #80
        sta ptr1
        bcc endrow
        inc ptr1+1
endrow: lda ptr1+1
        cmp #$E0
        bne row
        lda ptr1
        cmp #$18
        bne row
        lda #$0D
        sta $FD92
stop:   bra stop
