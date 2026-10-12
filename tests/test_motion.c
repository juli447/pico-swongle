// SPDX-License-Identifier: MIT
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "motion.h"
static int16_t read16(const uint8_t *b) { return (int16_t)(b[0]|((uint16_t)b[1]<<8)); }
static unsigned bits(const uint8_t *b,unsigned pos,unsigned n) {
    unsigned result=0;
    for (unsigned i=0;i<n;i++) result|=((b[(pos+i)/8]>>((pos+i)&7))&1u)<<i;
    return result;
}
static void unpack_q(const uint8_t *b,float q[4]) {
    assert(bits(b,48,2)==2);
    unsigned max=bits(b,50,2);
    unsigned packed[3]={bits(b,52,21),bits(b,73,21),bits(b,94,2)|(bits(b,144,19)<<2)};
    float sum=0;
    for (unsigned i=0;i<3;i++) {
        int32_t v=(int32_t)packed[i]; if (v&0x100000) v-=0x200000;
        q[(max+i+1)&3]=v/1048576.0f;
        sum+=q[(max+i+1)&3]*q[(max+i+1)&3];
    }
    assert(sum<=1.00001f); q[max]=sqrtf(fmaxf(0,1-sum));
}
int main(void) {
    uint8_t out[36],again[36];
    motion_reset(0); motion_fill(out,0);
    for (unsigned i=0;i<36;i++) assert(out[i]==0);
    uni_gamepad_t gp={.accel={0,UNI_STANDARD_GRAVITY,0}};
    motion_set_mode(1);
    for (unsigned t=5;t<=15;t+=5) { motion_set_input(&gp,t); motion_poll(t); }
    motion_fill(out,15);
    for (unsigned i=0;i<3;i++) {
        assert(read16(out+i*12)==0 && read16(out+i*12+2)==0 && read16(out+i*12+4)==4096);
        assert(read16(out+i*12+6)==0 && read16(out+i*12+8)==0 && read16(out+i*12+10)==0);
    }
    motion_reset(0); motion_set_mode(1);
    for (unsigned i=1;i<=3;i++) {
        gp.accel[1]=UNI_STANDARD_GRAVITY*i;
        motion_set_input(&gp,i*5); motion_poll(i*5);
    }
    motion_fill(out,15);
    for (unsigned i=0;i<3;i++) assert(read16(out+i*12+4)==(int)(4096*(i+1)));
    motion_reset(0); motion_set_mode(1);
    gp=(uni_gamepad_t){.accel={UNI_STANDARD_GRAVITY,2*UNI_STANDARD_GRAVITY,3*UNI_STANDARD_GRAVITY},.gyro={1,2,3}};
    for (unsigned t=5;t<=15;t+=5) { motion_set_input(&gp,t); motion_poll(t); }
    motion_fill(out,15);
    assert(read16(out)==-12288 && read16(out+2)==-4096 && read16(out+4)==8192);
    assert(abs(read16(out+6)+2455)<=1 && abs(read16(out+8)+818)<=1 && abs(read16(out+10)-1637)<=1);
    // One second of +90 degrees/second yaw must encode a +90 degree attitude, not 180.
    motion_reset(0); motion_set_mode(2);
    gp=(uni_gamepad_t){.accel={0,UNI_STANDARD_GRAVITY,0},.gyro={0,90*UNI_DEG_TO_RAD,0}};
    for (unsigned t=5;t<=1000;t+=5) { motion_set_input(&gp,t); motion_poll(t); }
    motion_fill(out,1000); float q[4]; unpack_q(out,q);
    assert(fabsf(q[0])<0.001f && fabsf(q[1])<0.001f);
    assert(fabsf(q[2]-0.70710678f)<0.001f && fabsf(q[3]-0.70710678f)<0.001f);
    assert(read16(out+4)==4096 && bits(out,271,11)==1000 && bits(out,282,6)==3);
    motion_fill(again,1000); assert(memcmp(out,again,36)==0);
    motion_stop();
    for (unsigned t=1005;t<=1100;t+=5) motion_poll(t);
    motion_fill(again,1100); float stopped[4]; unpack_q(again,stopped);
    for (unsigned i=0;i<4;i++) assert(fabsf(q[i]-stopped[i])<0.00001f);
    // Missing Bluetooth samples must stop rotation automatically, without an explicit disconnect.
    motion_reset(0); motion_set_mode(2); motion_set_input(&gp,0);
    for (unsigned t=5;t<=100;t+=5) motion_poll(t);
    motion_fill(out,100); unpack_q(out,q);
    for (unsigned t=105;t<=200;t+=5) motion_poll(t);
    motion_fill(again,200); unpack_q(again,stopped);
    for (unsigned i=0;i<4;i++) assert(fabsf(q[i]-stopped[i])<0.00001f);
    motion_reset(0); motion_set_mode(1);
    gp=(uni_gamepad_t){.accel={1e30f,INFINITY,-1e30f},.gyro={NAN,1e30f,-1e30f}};
    motion_set_input(&gp,5); motion_poll(5); motion_fill(out,5);
    assert(read16(out+24)==32767 && read16(out+26)==-32768 && read16(out+28)==0);
    assert(read16(out+30)==32767 && read16(out+32)==0 && read16(out+34)==32767);
    motion_set_mode(0); motion_fill(out,10);
    for (unsigned i=0;i<36;i++) assert(out[i]==0);
    puts("PASS: gravity, SI/axis conversion, sample order, quaternion 90-degree yaw, packing stability, stop, saturation and IMU-off");
}
