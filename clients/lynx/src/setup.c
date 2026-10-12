#include "setup.h"
#include "command.h"
#include <string.h>
#ifndef SERVER_HOST
#define SERVER_HOST "127.0.0.1"
#endif
#ifndef SERVER_PORT
#define SERVER_PORT 9000
#endif
#ifndef LOBBY_BASE
#define LOBBY_BASE "https://lobby.fujinet.online"
#endif
#ifndef LOBBY_PLATFORM
#define LOBBY_PLATFORM "atari"
#endif
#ifndef ROOM_PORT_BASE
#define ROOM_PORT_BASE SERVER_PORT
#endif
#ifndef ROOM_COUNT
#define ROOM_COUNT 4
#endif

typedef char host_fits_setup[(sizeof(SERVER_HOST) <= 63) ? 1 : -1];
typedef char rooms_fit_menu[(ROOM_COUNT > 0 && ROOM_COUNT <= 4) ? 1 : -1];

struct client_setup client_setup;
static uint8_t response[256];
static uint8_t record[189];

void setup_defaults(void)
{
    memset(&client_setup, 0, sizeof(client_setup));
    strcpy(client_setup.host, SERVER_HOST);
    client_setup.port = SERVER_PORT;
}
uint8_t setup_name(char *out, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint8_t n = 0, c, meaningful = 0, ended = 0;
    if (len > 64) return 0;
    for (i = 0; i < len; ++i) {
        c = data[i];
        if (!c) { ended = 1; continue; }
        if (ended) return 0;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == ' ' || c == '-' || c == '.')) continue;
        if (n == 8) return 0;
        out[n++] = c;
        if (c != ' ') meaningful = 1;
    }
    while (n && out[n-1] == ' ') --n;
    out[n] = 0;
    return meaningful;
}
static uint8_t appkey_open(uint16_t creator, uint8_t app, uint8_t mode)
{
    uint8_t p[7];
    p[0] = 0xdc; p[1] = (uint8_t)creator; p[2] = creator >> 8;
    p[3] = app; p[4] = 0; p[5] = mode; p[6] = 0;
    return command_complete(0x70, p, sizeof(p));
}
static void appkey_close(void)
{
    const uint8_t p = 0xdb;
    command_complete(0x70, &p, 1);
}
static uint8_t read_name(uint16_t creator, uint8_t app)
{
    uint8_t ok;
    uint16_t len;
    const uint8_t p = 0xdd;
    if (!appkey_open(creator, app, 0)) return 0;
    /* Lynx replies are raw key bytes; unlike Atari, no inner LE count. */
    ok = command_read(0x70, &p, 1, response, 64, &len) &&
         setup_name(client_setup.name, response, len);
    appkey_close();
    if (!ok) client_setup.name[0] = 0;
    return ok;
}
uint8_t setup_load_name(void)
{
    return read_name(0x3022, 3) || read_name(1, 1);
}
static uint8_t write_name(const uint8_t *p, uint8_t len)
{
    uint8_t ok;
    if (!appkey_open(0x3022, 3, 1)) return 0;
    ok = command_complete(0x70, p, len);
    appkey_close();
    return ok;
}
static uint8_t read_saved(uint16_t *len)
{
    uint8_t ok;
    const uint8_t p = 0xdd;
    if (!appkey_open(0x3022,3,0)) return 0;
    ok = command_read(0x70,&p,1,response,64,len);
    appkey_close();
    return ok;
}
uint8_t setup_save_name(void)
{
    uint8_t p[11], len;
    uint16_t stored;
    len = (uint8_t)strlen(client_setup.name);
    if (!len || len > 8) return 0;
    /* Older Lynx firmware consumes a LE length before the value. A raw
       one-byte name can assert in that firmware, so send the compatible
       shape first. New AppKeyMixin stores these length bytes literally;
       recognize that exact readback and rewrite with its raw-value format.
       Success always means the persisted bytes equal the intended name. */
    p[0] = 0xde; p[1] = len; p[2] = 0;
    memcpy(p+3, client_setup.name, len);
    if (!write_name(p,len+3) || !read_saved(&stored)) return 0;
    if (stored == len && !memcmp(response,client_setup.name,len)) return 1;
    if (stored != len+2 || memcmp(response,p+1,len+2)) return 0;
    memcpy(p+1,client_setup.name,len);
    if (!write_name(p,len+1) || !read_saved(&stored)) return 0;
    return stored == len && !memcmp(response,client_setup.name,len);
}
static uint8_t text_field(const uint8_t *p, uint8_t len)
{
    uint8_t i, ended = 0;
    for (i = 0; i < len; ++i) {
        if (!p[i]) ended = 1;
        else if (ended || p[i] < 32 || p[i] > 126) return 0;
    }
    return ended;
}
uint8_t setup_record(const uint8_t *r, struct lobby_room *room)
{
    const char *url;
    uint8_t i, c;
    uint16_t port = 0, digit;
    if (r[0] != 3 || !text_field(r+1,17) ||
        strcmp((const char *)r+1, "Maze War") || !r[18] || !text_field(r+18,33) ||
        !text_field(r+51,65) || !text_field(r+116,65) ||
        !text_field(r+181,3) || r[184] != 1 ||
        !r[186] || r[186] > 4 || r[185] > r[186]) return 0;
    url = (const char *)r+51;
    if (strncmp(url,"tcp://",6)) return 0;
    url += 6;
    i = (uint8_t)strlen(SERVER_HOST);
    if (strncmp(url, SERVER_HOST, i) || url[i] != ':') return 0;
    url += i+1;
    if (!*url) return 0;
    while ((c = (uint8_t)*url++) != 0) {
        if (c < '0' || c > '9') return 0;
        digit = c-'0';
        if (port > 6553 || (port == 6553 && digit > 5)) return 0;
        port = port*10+digit;
    }
    if (port < ROOM_PORT_BASE || (uint16_t)(port-ROOM_PORT_BASE) >= ROOM_COUNT) return 0;
    memcpy(room->label, r+18,33);
    room->port = port; room->players = r[185]; room->maximum = r[186];
    return 1;
}
static uint8_t status(uint16_t *available, uint8_t *error, uint8_t *connected)
{
    uint16_t len;
    const uint8_t p = 'S';
    if (!command_read(0x71, &p, 1, response, sizeof(response), &len) || len != 4) return 0;
    *available = response[0] | ((uint16_t)response[1] << 8);
    *connected = response[2]; *error = response[3];
    return 1;
}
uint8_t setup_lobby(void)
{
    static const char url[] = "N1:" LOBBY_BASE "/view?bin=1&platform=" LOBBY_PLATFORM "&appkey=3&pagesize=4&page=0";
    uint8_t p[sizeof(url)+3], c, result = LOBBY_ERROR, error, connected;
    uint16_t len, available, i, used = 0, want = 3, received = 0, expected = 3, start;
    client_setup.count = client_setup.records = 0;
    p[0] = 'O'; p[1] = 4; p[2] = 0;
    memcpy(p+3,url,sizeof(url));
    p[sizeof(url)+1] = '0'+client_setup.page;
    start = command_clock();
    if (!command_complete(0x71,p,sizeof(p))) goto close;
    for (;;) {
        if ((uint16_t)(command_clock()-start) >= 1200) goto close;
        if (!status(&available,&error,&connected)) goto close;
        if (error == 170) { result = LOBBY_EMPTY; goto close; }
        if (available) {
            p[0] = 'R';
            if (!command_read(0x71,p,1,response,sizeof(response),&len) || !len) goto close;
            for (i = 0; i < len; ++i) {
                if (received >= expected) goto close;
                c = response[i]; record[used++] = c; ++received;
                if (used == want) {
                    if (want == 3) {
                        if (record[0] > 4 || record[1] || record[2]) goto close;
                        client_setup.records = record[0];
                        expected = 3+(uint16_t)record[0]*189;
                        want = 189;
                    } else if (setup_record(record,&client_setup.rooms[client_setup.count])) {
                        ++client_setup.count;
                    }
                    used = 0;
                }
            }
        } else if (error == 136 || !connected) {
            if (received == expected && received >= 3 && !used)
                result = client_setup.count ? LOBBY_OK : LOBBY_EMPTY;
            goto close;
        } else if (error > 1) goto close;
    }
close:
    p[0] = 'C';
    if (!command_complete(0x71,p,1)) result = LOBBY_ERROR;
    if (result == LOBBY_ERROR) client_setup.count = 0;
    return result;
}
