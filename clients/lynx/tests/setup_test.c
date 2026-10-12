/* Real command framing exercised against a deterministic FujiNet peer. */
#include "command.h"
#include "setup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t in[2048], tx[512], body[800], key[64];
static uint16_t read_at, write_at, tx_at, tx_want, body_at, body_len;
static uint16_t ticks;
static uint8_t mode, opens, closes, reads, writes, fail_write, bad_crc;
static uint8_t malformed, no_rooms, truncation, extra, saved_len;
static uint8_t last_command;
static uint16_t clock_calls, quiet_polls;
static uint8_t delayed_tail, noisy_ack;
static uint8_t silent_complete, continuous_noise;
uint16_t command_clock(void) { if (!(++clock_calls & 63)) ++ticks; return ticks; }
uint8_t command_io_open(void) { ++opens; return 1; }
uint8_t command_io_get(uint8_t *b)
{
    if (continuous_noise) { *b = 0xa7; return 1; }
    if (read_at == write_at) {
        ++quiet_polls;
        /* A late byte arrives after the old immediate drain has returned. */
        if (delayed_tail && quiet_polls == 16) {
            delayed_tail = 0; quiet_polls = 0; *b = 0xa7; return 1;
        }
        return 0;
    }
    quiet_polls = 0;
    *b = in[read_at++];
    return 1;
}
static void enqueue(uint8_t b) { assert(write_at < sizeof(in)); in[write_at++] = b; }
static void ack(uint8_t value)
{
    if (noisy_ack) { enqueue(0); enqueue(0xa7); }
    enqueue(value);
}
static void reply(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint8_t ck = 0;
    enqueue(len >> 8); enqueue(len);
    for (i = 0; i < len; ++i) { enqueue(data[i]); ck ^= data[i]; }
    enqueue(ck ^ bad_crc);
}
static void dispatch(void)
{
    uint8_t ck = 0, cmd = tx[3], status[4];
    uint16_t i, n;
    for (i = 3; i < tx_want-1; ++i) ck ^= tx[i];
    assert(ck == tx[tx_want-1]);
    read_at = write_at = 0;
    ack(6); last_command = cmd;
    if (tx[0] == 0x70) {
        if (cmd == 0xdc) {
            assert(tx[7] == 0 && tx[9] == 0); /* key zero and reserved */
            assert((tx[4] == 0x22 && tx[5] == 0x30 && tx[6] == 3) ||
                   (tx[4] == 1 && tx[5] == 0 && tx[6] == 1));
            mode = tx[8]; ack(6);
        } else if (cmd == 0xdd) { ++reads; reply(key, saved_len); }
        else if (cmd == 0xdb) { ++closes; if (!silent_complete) ack(6); }
        else if (cmd == 0xde) {
            assert(mode == 1 && tx_want-5 <= 10);
            ++writes;
            if (!fail_write) { saved_len = tx_want-5; memcpy(key,tx+4,saved_len); }
            ack(fail_write ? 0x15 : 6);
        } else assert(0);
    } else {
        assert(tx[0] == 0x71);
        if (cmd == 'O') {
            assert(tx[4] == 4 && tx[5] == 0);
            assert(strstr((char *)tx+6,"page=0"));
            body_at = 0; ack(6);
        } else if (cmd == 'S') {
            n = body_len-body_at;
            status[0] = n; status[1] = n >> 8;
            status[2] = n != 0; status[3] = no_rooms ? 170 : n ? 1 : 136;
            reply(status,4);
        } else if (cmd == 'R') {
            n = body_len-body_at; if (n > 256) n = 256;
            reply(body+body_at,n); body_at += n;
        } else if (cmd == 'C') { ++closes; ack(6); }
        else assert(0);
    }
}
uint8_t command_io_put(uint8_t b)
{
    /* Reply ACK/NAK happens outside a command frame. */
    if (!tx_at && (b == 6 || b == 0x15)) return 1;
    /* No new command may collide with the delayed previous transaction tail. */
    if (!tx_at) assert(!delayed_tail);
    tx[tx_at++] = b;
    if (tx_at == 3) tx_want = 4+((uint16_t)tx[1]<<8)+tx[2];
    if (tx_at >= 3 && tx_at == tx_want) { dispatch(); tx_at = 0; }
    return 1;
}
static void make_record(uint8_t *r, uint8_t id)
{
    memset(r,0,189); r[0] = 3;
    strcpy((char *)r+1,"Maze War"); strcpy((char *)r+18,"Maze War Humans Only");
    strcpy((char *)r+51,"tcp://fujinet.online:5025"); r[75] += id;
    strcpy((char *)r+116,"tnfs://example/atari.xex"); strcpy((char *)r+181,"us");
    r[184] = 1; r[185] = id; r[186] = 4;
}
static void lobby_body(void)
{
    uint8_t i;
    memset(body,0,sizeof(body)); body[0] = 4;
    for (i = 0; i < 4; ++i) make_record(body+3+(uint16_t)i*189,i);
    body_len = 759;
    if (malformed) body[1] = 1;
    if (truncation) --body_len;
    if (extra) ++body_len;
}
int main(void)
{
    char name[9];
    uint8_t raw[300], p = 'R';
    uint16_t len;
    struct lobby_room room;
    assert(command_open() && command_open() && opens == 1);
    delayed_tail = noisy_ack = 1;
    setup_defaults();
    assert(setup_name(name,(const uint8_t *)"a-b. 12",7) && !strcmp(name,"A-B. 12"));
    assert(!setup_name(name,(const uint8_t *)"         ",9));
    assert(!setup_name(name,(const uint8_t *)"NINECHARS",9));
    assert(!setup_name(name,(const uint8_t *)"A\0B",3));
    assert(!setup_load_name());
    strcpy(client_setup.name,"LYNX ACE"); assert(setup_save_name());
    memset(client_setup.name,0,9); assert(setup_load_name());
    assert(!strcmp(client_setup.name,"LYNX ACE") && saved_len == 8);
    fail_write = 1; assert(!setup_save_name() && !strcmp(client_setup.name,"LYNX ACE"));
    fail_write = 0;
    make_record(raw,0); assert(setup_record(raw,&room) && room.port == 5025);
    memset(raw+1,'X',17); assert(!setup_record(raw,&room));
    make_record(raw,0); raw[184] = 0; assert(!setup_record(raw,&room));
    make_record(raw,0); strcpy((char *)raw+51,"tcp://evil.example:5025"); assert(!setup_record(raw,&room));
    make_record(raw,0); strcpy((char *)raw+51,"tcp://fujinet.online:65536"); assert(!setup_record(raw,&room));
    lobby_body(); assert(setup_lobby() == LOBBY_OK && client_setup.count == 4);
    assert(client_setup.rooms[3].port == 5028 && client_setup.rooms[3].players == 3);
    malformed = 1; lobby_body(); assert(setup_lobby() == LOBBY_ERROR && !client_setup.count);
    malformed = 0; truncation = 1; lobby_body(); assert(setup_lobby() == LOBBY_ERROR);
    truncation = 0; extra = 1; lobby_body(); assert(setup_lobby() == LOBBY_ERROR);
    extra = 0; no_rooms = 1; lobby_body(); assert(setup_lobby() == LOBBY_EMPTY);
    no_rooms = 0;
    lobby_body(); body_at = 0;
    assert(!command_read(0x71,&p,1,raw,8,&len)); /* intact oversized reply drained */
    assert(last_command == 'R');
    p = 'S'; assert(command_read(0x71,&p,1,raw,sizeof(raw),&len) && len == 4);
    bad_crc = 1; assert(!command_read(0x71,&p,1,raw,sizeof(raw),&len));
    assert(strstr(command_error_text(),"P08 E03"));
    bad_crc = 0; assert(command_read(0x71,&p,1,raw,sizeof(raw),&len));
    ticks = 65530; silent_complete = 1; p = 0xdb;
    assert(!command_complete(0x70,&p,1));
    assert(strstr(command_error_text(),"D70 CDB P04 E01"));
    silent_complete = 0;
    continuous_noise = 1;
    assert(!command_complete(0x70,&p,1));
    assert(strstr(command_error_text(),"P01 E01"));
    continuous_noise = 0;
    assert(command_complete(0x70,&p,1));
    command_enter_raw(); assert(!command_open() && !command_complete(0x71,&p,1));
    puts("setup tests passed: AppKeys, Lobby, delayed serial tail, ACK noise, NAK, deadline/wrap, noisy drain timeout, CRC, diagnostics and raw ownership");
    return 0;
}
