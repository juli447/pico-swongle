// SPDX-License-Identifier: MIT
// Quaternion integration/packing adapted from switch-pico by Joey Yakimowich-Payne.
// Copyright (c) 2025 Joey Yakimowich-Payne. See licenses/switch-pico.txt.
#include "motion.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

// Match the advertised factory calibration: 4096 counts/g, 13371 counts/936 deg/s.
#define ACCEL_SCALE (4096.0f / UNI_STANDARD_GRAVITY)
#define GYRO_SCALE (13371.0f / (936.0f * UNI_DEG_TO_RAD))
typedef struct { int16_t accel[3], gyro[3]; } sample_t;
static sample_t latest, samples[3];
static unsigned head;
static uint32_t input_time, sample_time;
static bool have_input;
static uint8_t mode;
static float q[4]; // x, y, z, w

static int16_t quantize(float value) {
    if (!isfinite(value)) return 0;
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)lroundf(value);
}
static void write16(uint8_t *out, int16_t value) {
    uint16_t u = (uint16_t)value;
    out[0] = u & 255; out[1] = u >> 8;
}
static void write_bits(uint8_t *out, unsigned pos, unsigned n, uint32_t value) {
    for (unsigned i=0; i<n; i++) {
        uint8_t mask = 1u << ((pos+i) & 7);
        if (value & (1u << i)) out[(pos+i)/8] |= mask;
        else out[(pos+i)/8] &= (uint8_t)~mask;
    }
}
void motion_reset(uint32_t now) {
    latest = (sample_t){.accel={0,0,4096}};
    for (unsigned i=0; i<3; i++) samples[i]=latest;
    head=0; input_time=sample_time=now; have_input=false; mode=0;
    q[0]=q[1]=q[2]=0; q[3]=1;
}
void motion_set_input(const uni_gamepad_t *gp, uint32_t now) {
    // Bluepad32 canonical frame: right, up, back. Switch native: forward, left, up.
    float accel[3]={-gp->accel[2],-gp->accel[0],gp->accel[1]};
    float gyro[3]={-gp->gyro[2],-gp->gyro[0],gp->gyro[1]};
    for (unsigned i=0; i<3; i++) {
        latest.accel[i]=quantize(accel[i]*ACCEL_SCALE);
        latest.gyro[i]=quantize(gyro[i]*GYRO_SCALE);
    }
    have_input=true; input_time=now;
}
void motion_stop(void) {
    memset(latest.gyro,0,sizeof(latest.gyro));
    for (unsigned i=0; i<3; i++) memset(samples[i].gyro,0,sizeof(samples[i].gyro));
    have_input=false; // Hold attitude; do not jump quaternion back to identity on a dropped link.
}
void motion_set_mode(uint8_t requested) {
    if (requested==2 && mode!=2) { q[0]=q[1]=q[2]=0; q[3]=1; }
    mode=requested<=2 ? requested : 0;
}
static void integrate(const sample_t *sample) {
    // Nintendo quaternion coordinates use Y, X, Z sensor order.
    float ax=sample->gyro[1]*(0.005f/GYRO_SCALE);
    float ay=sample->gyro[0]*(0.005f/GYRO_SCALE);
    float az=sample->gyro[2]*(0.005f/GYRO_SCALE);
    float angle=sqrtf(ax*ax+ay*ay+az*az);
    float scale=angle>1e-12f ? sinf(angle*0.5f)/angle : 0.5f;
    float dx=ax*scale, dy=ay*scale, dz=az*scale;
    float dw=angle>1e-12f ? cosf(angle*0.5f) : 1;
    float next[4]={
        q[3]*dx+q[0]*dw+q[1]*dz-q[2]*dy,
        q[3]*dy-q[0]*dz+q[1]*dw+q[2]*dx,
        q[3]*dz+q[0]*dy-q[1]*dx+q[2]*dw,
        q[3]*dw-q[0]*dx-q[1]*dy-q[2]*dz};
    float norm=sqrtf(next[0]*next[0]+next[1]*next[1]+next[2]*next[2]+next[3]*next[3]);
    for (unsigned i=0; i<4; i++) q[i]=next[i]/norm;
}
void motion_poll(uint32_t now) {
    uint32_t elapsed=now-sample_time;
    if (elapsed<5) return;
    // A delayed loop must not fabricate an arbitrarily long history of rotation.
    if (elapsed>30) sample_time=now-5;
    while (now-sample_time>=5) {
        sample_time+=5;
        sample_t sample=latest;
        if (!have_input || now-input_time>=100) memset(sample.gyro,0,sizeof(sample.gyro));
        samples[head]=sample; head=(head+1)%3;
        if (mode==2) integrate(&sample);
    }
}
void motion_fill(uint8_t out[36], uint32_t now) {
    memset(out,0,36);
    if (mode==1) {
        for (unsigned i=0; i<3; i++) {
            const sample_t *s=&samples[(head+i)%3];
            for (unsigned j=0; j<3; j++) {
                write16(out+i*12+j*2,s->accel[j]);
                write16(out+i*12+6+j*2,s->gyro[j]);
            }
        }
    } else if (mode==2) {
        const sample_t *s=&samples[(head+2)%3];
        write16(out,s->accel[1]); write16(out+2,s->accel[0]); write16(out+4,s->accel[2]);
        unsigned max=0;
        for (unsigned i=1; i<4; i++) if (fabsf(q[i])>fabsf(q[max])) max=i;
        uint32_t packed[3];
        float sign=q[max]<0 ? -1 : 1;
        for (unsigned i=0; i<3; i++)
            packed[i]=(uint32_t)(int32_t)floorf(q[(max+i+1)&3]*1048576.0f*sign)&0x1fffff;
        write_bits(out,48,2,2); write_bits(out,50,2,max);
        write_bits(out,52,21,packed[0]); write_bits(out,73,21,packed[1]);
        write_bits(out,94,2,packed[2]&3); write_bits(out,144,19,packed[2]>>2);
        write_bits(out,271,11,now&0x7ff); write_bits(out,282,6,3);
    }
}
