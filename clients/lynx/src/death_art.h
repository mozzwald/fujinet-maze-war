/* Twelve by nine explosion -> smoke -> dissipating wisps. '.' is transparent.
   W white, Y yellow, O orange/peach, L light grey, G grey, D dark grey. */
static const char *const death_art[4][9] = {
    {"............", ".....Y......", "...O.Y.O....", "....YWY.....",
     "..YYWWWYY...", "....YWY.....", "...O.Y.O....", ".....Y......", "............"},
    {"....L.L.....", "..LLWWWLL...", ".LOWWYWWOL..", ".LWWWWWYWL..",
     "LLWWYWYWLL..", ".LOWWWWWOL..", "..LLWWLL....", "...L..L.....", "............"},
    {"...LL..L....", "..LWWLLWL...", ".LWWWWWWWL..", ".LWWLWWWL...",
     "..LWWWWWWL..", ".LLWWWLWL...", "..LLWWLL....", "...L..L.....", "............"},
    {"..G...G.....", ".....L..G...", ".G..L.......", "...G..L..G..",
     "..L.....G...", "....G..L....", ".G....G.....", "...D....D...", "............"}
};
