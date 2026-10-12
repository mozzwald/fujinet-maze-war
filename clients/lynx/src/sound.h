#ifndef MAZE_LYNX_SOUND_H
#define MAZE_LYNX_SOUND_H
/* Mikey audio only: no interrupts, waits or changes to serial/display timers. */
void sound_init(void);
void sound_update(void);
#endif
