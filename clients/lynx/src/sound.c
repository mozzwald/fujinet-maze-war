#include <stdint.h>
#include <string.h>
#include "protocol.h"
#include "sound.h"
#ifdef LYNX_SOUND_HOST
#include "sound_host.h"
#define CHANNELS sound_channels
#define STEREO sound_stereo
#define PANNING sound_panning
#else
#include <lynx.h>
#define CHANNELS (&MIKEY.channel_a)
#define STEREO MIKEY.mstereo
#define PANNING MIKEY.panning
#endif

enum { SILENT, STEP, SHOT, IMPACT, SPAWN, DEATH };
/* Eight envelope steps. At AUD_8 a square wave is 62500/(reload+1) Hz.
   POKEY's shuffle, descending shot, warbling coalescence and evaporation
   shape the effects; these are Lynx voices, not POKEY register copies. */
static const uint8_t pitch[5][8] = {
    {160,160,160,160,160,160,160,160}, /* low footstep noise, about 5x slower */
    {14,18,23,29,36,44,54,66},
    {32,40,50,64,80,100,128,160},
    {180,26,152,22,120,18,84,14},
    {10,10,12,14,18,24,32,44}
};
static const uint8_t volume[5][8] = {
    {24,0,0,0,0,0,0,0},
    {64,64,56,48,40,32,20,8},
    {56,56,48,40,32,24,16,8},
    {32,40,48,56,48,40,24,12},
    {64,72,72,64,48,32,20,8}
};
static const uint8_t period[6] = {0,2,1,2,2,4};
struct voice { uint16_t started; uint8_t effect; };
static struct voice voices[4];
static uint8_t epoch, stage, initialized;

static void silence(void)
{
    uint8_t i;
    for (i=0; i<4; ++i) { CHANNELS[i].volume=0; voices[i].effect=SILENT; }
}
void sound_init(void)
{
    uint8_t i;
    memset(voices,0,sizeof(voices));
    for (i=0; i<4; ++i) {
        CHANNELS[i].control=0;
        CHANNELS[i].volume=0;
        CHANNELS[i].feedback=1;
        CHANNELS[i].other=0;
        CHANNELS[i].shiftlo=1;
        CHANNELS[i].reload=32;
        CHANNELS[i].count=32;
        CHANNELS[i].control=0x1b; /* count, reload, 8us; no linked timer */
    }
    STEREO=0; PANNING=0; /* enable all voices on both sides, no attenuation */
    initialized=0; stage=ROUND_NONE;
}
static void play(uint8_t channel, uint8_t effect)
{
    if (effect < voices[channel].effect) return;
    voices[channel].effect=effect;
    voices[channel].started=game.input_clock;
    CHANNELS[channel].volume=0;
    CHANNELS[channel].feedback=(effect==SHOT || effect==SPAWN) ? 1 : 0x3f;
    CHANNELS[channel].other=0;
    CHANNELS[channel].shiftlo=1;
}
void sound_update(void)
{
    uint8_t i, effect, at, level, events, impact=0, next=game_round_stage(&game);
    uint16_t elapsed;
    if (!initialized || epoch!=game.round_id) {
        silence(); epoch=game.round_id; initialized=1;
        stage=ROUND_NONE;
    }
    /* Expire before arbitration so an old death cannot suppress a respawn.
       Direct elapsed time skips missed envelope steps, never catches up. */
    for (i=0; i<4; ++i) {
        effect=voices[i].effect;
        if (effect && (uint16_t)(game.input_clock-voices[i].started)>=8*period[effect]) {
            voices[i].effect=SILENT; CHANNELS[i].volume=0;
        }
    }
    if (next!=stage) {
        if (next==ROUND_LOSERS || next==ROUND_WINNER_SMOKE) {
            silence();
            for (i=0; i<4; ++i)
                if (((game.seat_mask|game.zombie_mask)&(1u<<i)) &&
                    !game.players[i].respawning && game.players[i].x<20 && game.players[i].y<18 &&
                    ((next==ROUND_LOSERS) ? i!=game.winner_pid : i==game.winner_pid))
                    play(i,DEATH);
        } else if (next==ROUND_RESULTS) silence();
        stage=next;
    }
    for (i=0; i<4; ++i) {
        events=game.sound_events[i]; game.sound_events[i]=0;
        if (game.phase || !game_ready()) continue;
        if (events&GAME_SOUND_STEP) play(i,STEP);
        if (events&GAME_SOUND_SHOT) {
            /* Sound actual projectile creation, including direction pressed
               while fire is already held. A bare trigger is not a shot. */
            play(i,SHOT);
        }
        if ((events&GAME_SOUND_IMPACT) && !(events&GAME_SOUND_SHOT)) {
            play(i,IMPACT); impact=1;
        }
        if (events&GAME_SOUND_SPAWN) play(i,SPAWN);
        if (events&GAME_SOUND_DEATH) play(i,DEATH);
    }
    if (game.sound_brick && !impact && !game.phase && game_ready()) play(3,IMPACT);
    game.sound_brick=0;
    for (i=0; i<4; ++i) {
        effect=voices[i].effect;
        if (!effect) continue;
        elapsed=(uint16_t)(game.input_clock-voices[i].started);
        at=elapsed/period[effect];
        level=volume[effect-1][at];
        if (i!=game.local_pid) level>>=1;
        CHANNELS[i].reload=pitch[effect-1][at];
        CHANNELS[i].volume=level;
    }
}
