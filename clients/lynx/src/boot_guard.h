#ifndef MAZE_LYNX_BOOT_GUARD_H
#define MAZE_LYNX_BOOT_GUARD_H
#include <stdint.h>
/* Check immutable program RAM. On failure, CPU-only white bars show stage. */
#if defined(LYNX_BOOT_GUARD) && LYNX_BOOT_GUARD
void __fastcall__ boot_guard(uint8_t stage);
#else
#define boot_guard(stage) ((void)0)
#endif
#endif
