#ifndef MAZE_SOUND_HOST_H
#define MAZE_SOUND_HOST_H
#include <stdint.h>
struct sound_registers { uint8_t volume,feedback,dac,shiftlo,reload,control,count,other; };
extern struct sound_registers sound_channels[4];
extern uint8_t sound_stereo, sound_panning;
#endif
