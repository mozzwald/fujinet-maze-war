/* Test cartridge only: compare the old loop with production CRC on 65C02. */
#include <lynx.h>
#include <tgi.h>
#include <serial.h>
#include "../src/frame.h"
#include "../src/netstream.h"
#include "bench.h"
static uint8_t data[56], report[16];
static volatile uint16_t result;
static uint16_t bitwise(const uint8_t *p,uint8_t n)
{
    uint16_t crc=0xffff;
    uint8_t bit, i;
    for(i=0;i<n;++i) {
        crc^=(uint16_t)p[i]<<8;
        for(bit=0;bit<8;++bit)
            crc=(crc&0x8000) ? (uint16_t)((crc<<1)^0x1021) : (uint16_t)(crc<<1);
    }
    return crc;
}
static void put16(uint8_t *p,uint16_t n) { p[0]=n; p[1]=n>>8; }
static void put32(uint8_t *p,unsigned long n)
{ put16(p,(uint16_t)n); put16(p+2,(uint16_t)(n>>16)); }
static void send(const uint8_t *p,uint8_t n)
{
    uint8_t wire[FRAME_WIRE_MAX], i, len=frame_encode(p,n,wire,sizeof(wire));
    for(i=0;i<len;++i) while(ser_put((char)wire[i])!=SER_ERR_OK) {}
}
void main(void)
{
    uint8_t lengths[3]={5,21,56}, phase, i;
    unsigned long start;
    tgi_install(lynx_160_102_16_tgi); tgi_init();
    while(tgi_busy()) {}
    tgi_setframerate(60); bench_clock_init();
    if(!netstream_start("benchmark",19125)) for(;;) {}
    while(link_state!=LINK_WELCOME) netstream_poll();
    for(i=0;i<sizeof(data);++i) data[i]=(uint8_t)(i*37);
    for(phase=0;phase<3;++phase) {
        report[0]=0x7a; report[1]=lengths[phase]; put16(report+2,128);
        start=bench_now();
        for(i=0;i<128;++i) result=bitwise(data,lengths[phase]);
        put32(report+4,(bench_now()-start)&0xffffffUL); put16(report+12,result);
        start=bench_now();
        for(i=0;i<128;++i) result=frame_crc16(data,lengths[phase]);
        put32(report+8,(bench_now()-start)&0xffffffUL); put16(report+14,result);
        send(report,sizeof(report));
        netstream_poll();
    }
    report[0]=0x7f; send(report,1);
    for(;;) netstream_poll();
}
