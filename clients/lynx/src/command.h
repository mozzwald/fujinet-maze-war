#ifndef LYNX_COMMAND_H
#define LYNX_COMMAND_H
#include <stdint.h>
/* Exactly one owner of serial: command transactions until raw activation. */
uint8_t command_open(void);
uint8_t command_complete(uint8_t device, const uint8_t *payload, uint16_t len);
uint8_t command_read(uint8_t device, const uint8_t *payload, uint16_t len,
                     uint8_t *out, uint16_t cap, uint16_t *received);
void command_enter_raw(void);
uint16_t command_clock(void);
/* Last failed transaction: device, command, phase, error, byte, raw status. */
const char *command_error_text(void);
#endif
