#ifndef LYNX_SERIAL_TEST_H
#define LYNX_SERIAL_TEST_H
#include <stdint.h>
enum { SER_ERR_OK, SER_ERR_NO_DATA, SER_ERR_OVERFLOW };
uint8_t ser_get(char *byte);
uint8_t ser_put(char byte);
uint8_t ser_status(uint8_t *status);
#endif
