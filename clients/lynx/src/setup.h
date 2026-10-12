#ifndef LYNX_SETUP_H
#define LYNX_SETUP_H
#include <stdint.h>
#define LOBBY_ROOMS 4
struct lobby_room {
    char label[33];
    uint16_t port;
    uint8_t players, maximum;
};
struct client_setup {
    char name[9];
    char host[63];
    uint16_t port;
    struct lobby_room rooms[LOBBY_ROOMS];
    uint8_t count, page, records;
};
extern struct client_setup client_setup;
enum lobby_result { LOBBY_OK, LOBBY_EMPTY, LOBBY_ERROR };
uint8_t setup_name(char *out, const uint8_t *data, uint16_t len);
uint8_t setup_load_name(void);
uint8_t setup_save_name(void);
void setup_defaults(void);
uint8_t setup_lobby(void);
/* Pure fixed-record validation, shared with host fixtures. */
uint8_t setup_record(const uint8_t *record, struct lobby_room *room);
#endif
