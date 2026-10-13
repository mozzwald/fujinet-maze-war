; All game SCBs fit within one aligned page. In particular, their palettes
; can never start at $xxFA (Suzy palette-load erratum). BSS is cleared by crt0.
        .export _maze_scbs, _maze_text_scb
        .segment "BSS"
        .align 256
_maze_scbs:     .res 8 * 23
_maze_text_scb: .res 23
        .assert * - _maze_scbs <= 256, error, "SCBs exceed their aligned page"
