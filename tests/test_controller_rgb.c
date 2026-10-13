// SPDX-License-Identifier: MIT
#include "controller_rgb.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    controller_rgb_t s;
    uint8_t r,g,b;
    controller_rgb_init(&s, 0);
    assert(controller_rgb_next(&s, 0, &r, &g, &b));
    assert(r == 128 && !g && !b);
    assert(!controller_rgb_next(&s, 32, &r, &g, &b));
    assert(controller_rgb_next(&s, 33, &r, &g, &b));
    // Continuous hue, constant maximum brightness, including cycle wrap.
    uint8_t prev[3] = {128,0,0};
    for (unsigned t=0; t<=4800; t++) {
        s.output_known=false;
        assert(controller_rgb_next(&s,t,&r,&g,&b));
        assert(r<=128 && g<=128 && b<=128);
        assert(r==128 || g==128 || b==128);
        assert(abs((int)r-prev[0])<=1 && abs((int)g-prev[1])<=1 && abs((int)b-prev[2])<=1);
        prev[0]=r;prev[1]=g;prev[2]=b;
    }
    assert(!controller_rgb_buttons(&s,true,false));
    assert(controller_rgb_buttons(&s,true,true));
    assert(!s.enabled);
    assert(controller_rgb_next(&s,4801,&r,&g,&b) && !r && !g && !b);
    assert(controller_rgb_buttons(&s,true,true) && !s.enabled);
    assert(controller_rgb_buttons(&s,false,true) && !s.enabled);
    assert(controller_rgb_buttons(&s,false,false));
    assert(!controller_rgb_buttons(&s,false,false));
    assert(!controller_rgb_next(&s,9000,&r,&g,&b));
    assert(controller_rgb_buttons(&s,true,true) && s.enabled);
    assert(controller_rgb_next(&s,9000,&r,&g,&b));
    controller_rgb_init(&s,UINT32_MAX-20);
    assert(controller_rgb_next(&s,UINT32_MAX-20,&r,&g,&b));
    assert(!controller_rgb_next(&s,10,&r,&g,&b));
    assert(controller_rgb_next(&s,12,&r,&g,&b));
    bool ps_out, touch_out;
    controller_rgb_init(&s,0);
    // Standalone Home passes immediately, throughout a long hold, then up.
    controller_rgb_filter_buttons(&s,0,true,false,false,&ps_out,&touch_out);
    assert(ps_out && !touch_out && s.enabled);
    controller_rgb_filter_buttons(&s,3000,true,false,false,&ps_out,&touch_out);
    assert(ps_out && !touch_out && s.enabled);
    controller_rgb_filter_buttons(&s,3001,false,false,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out);
    // User-requested order: hold GameChat, then press Home. The same input
    // frame suppresses Home; holding does not repeat the RGB toggle.
    controller_rgb_filter_buttons(&s,4000,false,true,false,&ps_out,&touch_out);
    assert(!ps_out && touch_out);
    controller_rgb_filter_buttons(&s,6000,true,true,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out && !s.enabled);
    controller_rgb_filter_buttons(&s,9000,true,true,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out && !s.enabled);
    controller_rgb_filter_buttons(&s,9001,true,false,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out); // No Home leak on partial release.
    controller_rgb_filter_buttons(&s,9002,false,false,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out);
    // Simultaneous shortcut and the opposite partial release remain local.
    controller_rgb_filter_buttons(&s,10000,true,true,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out && s.enabled);
    controller_rgb_filter_buttons(&s,10001,false,true,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out);
    controller_rgb_filter_buttons(&s,10002,false,false,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out);
    // Touchpad alone is forwarded immediately; no synthetic release action.
    controller_rgb_filter_buttons(&s,11000,false,true,false,&ps_out,&touch_out);
    assert(!ps_out && touch_out);
    controller_rgb_filter_buttons(&s,13000,false,true,false,&ps_out,&touch_out);
    assert(!ps_out && touch_out);
    controller_rgb_filter_buttons(&s,13001,false,false,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out);
    // Audio shortcut takes priority and does not toggle RGB or forward Home/C.
    controller_rgb_filter_buttons(&s,14000,false,true,true,&ps_out,&touch_out);
    assert(!ps_out && !touch_out && s.enabled);
    controller_rgb_filter_buttons(&s,14001,false,false,true,&ps_out,&touch_out);
    assert(!ps_out && !touch_out);
    controller_rgb_filter_buttons(&s,14002,false,false,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out);
    // PS+Share capture receives the original Home level (mapping tests cover
    // Share+Home -> Capture suppression separately).
    controller_rgb_filter_buttons(&s,15000,true,false,false,&ps_out,&touch_out);
    assert(ps_out && !touch_out);
    controller_rgb_filter_buttons(&s,15001,false,false,false,&ps_out,&touch_out);
    assert(!ps_out && !touch_out);
    puts("RGB: smooth hue, rate limit, immediate Home/GameChat including long holds, Touchpad-first RGB, partial release, audio/capture and clock wrap passed");
    return 0;
}
