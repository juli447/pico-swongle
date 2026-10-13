// SPDX-License-Identifier: MIT
#include "opus.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
extern const uint8_t swongle_audio_tone[2][200];
int main(void){int err;OpusDecoder *d=opus_decoder_create(48000,2,&err);if(err)return 1;int16_t pcm[960];double lo=1e9,hi=0;for(int f=0;f<1000;f++){int n=opus_decode(d,swongle_audio_tone[f%2],200,pcm,480,0);if(n!=480)return 2;double e=0;for(int i=0;i<960;i++)e+=(double)pcm[i]*pcm[i];double rms=sqrt(e/960);if(f>20){if(rms<lo)lo=rms;if(rms>hi)hi=rms;}}printf("Repeated tone, 10 seconds: RMS min %.1f / max %.1f; ratio %.3f\n",lo,hi,hi/lo);opus_decoder_destroy(d);return lo>100 && hi/lo<1.15?0:3;}
