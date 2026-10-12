/* Test-only SDL callback capture. Preload into the existing Handy binary;
   no emulator or firmware source changes. Each record is host monotonic ns,
   byte count, then the exact unsigned-8-bit mono output of Handy's callback. */
#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static void (*original_callback)(void *, Uint8 *, int);
static void *original_userdata;
static FILE *capture;
static void callback(void *unused, Uint8 *stream, int len)
{
    struct timespec now;
    uint64_t ns;
    uint32_t size = len;
    (void)unused;
    original_callback(original_userdata,stream,len);
    if (!capture) return;
    clock_gettime(CLOCK_MONOTONIC,&now);
    ns=(uint64_t)now.tv_sec*1000000000+now.tv_nsec;
    fwrite(&ns,sizeof(ns),1,capture);
    fwrite(&size,sizeof(size),1,capture);
    fwrite(stream,1,len,capture);
    fflush(capture);
}
int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    int (*open_audio)(SDL_AudioSpec *,SDL_AudioSpec *);
    SDL_AudioSpec copy=*desired;
    uint32_t header[3]={desired->freq,desired->format,desired->channels};
    const char *path=getenv("HANDY_AUDIO_CAPTURE");
    open_audio=dlsym(RTLD_NEXT,"SDL_OpenAudio");
    if (!open_audio) return -1;
    if (path && desired->format==AUDIO_U8 && desired->channels==1) {
        capture=fopen(path,"wb");
        if (capture) fwrite(header,sizeof(header),1,capture);
        original_callback=desired->callback;
        original_userdata=desired->userdata;
        copy.callback=callback;
        copy.userdata=NULL;
    }
    return open_audio(&copy,obtained);
}
