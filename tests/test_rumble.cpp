// SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdio>
#include "rumble.h"
static void store(uint8_t *out,uint32_t word) {
    for (unsigned i=0;i<4;i++) out[i]=(word>>(i*8))&255;
}
int main() {
    uint8_t payload[8]={},weak,strong;
    rumble_reset(); assert(rumble_take(0,&weak,&strong) && weak==0 && strong==0);
    assert(!rumble_take(1,&weak,&strong));
    // Switch full-state encoding: high amplitude index 32, low amplitude index 127.
    store(payload,(1u<<30)|(127u<<23)|(50u<<16)|(32u<<9)|(90u<<2));
    store(payload+4,0x40400100);
    rumble_receive(payload,20); assert(rumble_take(20,&weak,&strong) && weak==32 && strong==250);
    assert(!rumble_take(30,&weak,&strong));
    assert(rumble_take(70,&weak,&strong) && weak==32 && strong==250);
    rumble_receive(payload,100); assert(rumble_take(120,&weak,&strong));
    rumble_enable(false); assert(rumble_take(121,&weak,&strong) && weak==0 && strong==0);
    rumble_receive(payload,130); assert(!rumble_take(150,&weak,&strong));
    rumble_enable(true); rumble_receive(payload,160);
    assert(rumble_take(160,&weak,&strong) && strong==250);
    // A neutral word must stop immediately, even inside the output rate limit.
    store(payload,0x40400100); rumble_receive(payload,161);
    assert(rumble_take(161,&weak,&strong) && weak==0 && strong==0);
    store(payload,(1u<<30)|(127u<<23)|(50u<<16)|(32u<<9)|(90u<<2));
    rumble_receive(payload,200); assert(rumble_take(200,&weak,&strong));
    assert(rumble_take(499,&weak,&strong) && strong==250);
    assert(rumble_take(500,&weak,&strong) && weak==0 && strong==0);
    assert(!rumble_take(1000,&weak,&strong));
    // Timeout and refresh arithmetic must remain correct across millisecond-counter wraparound.
    rumble_reset(); rumble_take(0xfffffff0,&weak,&strong);
    rumble_receive(payload,0xfffffff0);
    assert(rumble_take(20,&weak,&strong) && strong==250);
    assert(rumble_take(284,&weak,&strong) && strong==0 && weak==0);
    std::puts("PASS: rumble bands, enable/disable, refresh rate, immediate stop, lost-packet timeout and timer wraparound");
}
