#ifndef LYNX_STARTUP_H
#define LYNX_STARTUP_H
#include <stdint.h>
/* Runs name/AppKey and HTTP setup while serial is in command mode. */
uint8_t startup_run(void);
#endif
