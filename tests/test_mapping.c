// SPDX-License-Identifier: MIT
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "input_mapping.h"

static unsigned x(const switch_state_t *s, unsigned offset) {
    return s->bytes[offset] | ((s->bytes[offset + 1] & 15) << 8);
}
static unsigned y(const switch_state_t *s, unsigned offset) {
    return (s->bytes[offset + 1] >> 4) | (s->bytes[offset + 2] << 4);
}
int main(void) {
    switch_state_t s;
    uni_gamepad_t g = {0};
    switch_state_from_gamepad(&g, &s);
    const uint8_t neutral[9] = {0,0,0,0,8,128,0,8,128};
    assert(memcmp(s.bytes, neutral, 9) == 0);
    const uint16_t input[] = {BUTTON_A,BUTTON_B,BUTTON_X,BUTTON_Y,
        BUTTON_SHOULDER_R,BUTTON_TRIGGER_R,BUTTON_SHOULDER_L,BUTTON_TRIGGER_L,
        BUTTON_THUMB_R,BUTTON_THUMB_L};
    const uint8_t expected[] = {4,8,1,2,64,128,64,128,4,8};
    const unsigned index[] = {0,0,0,0,0,0,2,2,1,1};
    for (unsigned i = 0; i < sizeof(input)/sizeof(input[0]); i++) {
        g = (uni_gamepad_t){.buttons=input[i]};
        switch_state_from_gamepad(&g, &s);
        assert(s.bytes[index[i]] == expected[i]);
    }
    g = (uni_gamepad_t){.dpad=DPAD_UP|DPAD_RIGHT};
    switch_state_from_gamepad(&g, &s);
    assert(s.bytes[2] == 6);
    g = (uni_gamepad_t){.misc_buttons=MISC_BUTTON_SYSTEM|MISC_BUTTON_SELECT};
    switch_state_from_gamepad(&g, &s);
    assert(s.bytes[1] == 32); // Capture chord suppresses Home and Minus.
    g = (uni_gamepad_t){.misc_buttons=MISC_BUTTON_START|MISC_BUTTON_CAPTURE};
    switch_state_from_gamepad(&g, &s);
    assert(s.bytes[1] == 34);
    g = (uni_gamepad_t){.brake=128,.throttle=128};
    switch_state_from_gamepad(&g, &s); assert(s.bytes[0] == 0 && s.bytes[2] == 0);
    g.brake++; g.throttle++;
    switch_state_from_gamepad(&g, &s); assert(s.bytes[0] == 128 && s.bytes[2] == 128);
    g = (uni_gamepad_t){.axis_x=-512,.axis_y=-512,.axis_rx=511,.axis_ry=511};
    switch_state_from_gamepad(&g, &s);
    assert(x(&s,3)==0x200 && y(&s,3)==0xe00);
    assert(x(&s,6)==0xe00 && y(&s,6)==0x200);
    // Both sticks must stay centred inside the new 15% deadzone, including its boundary.
    for (int v=-77; v<=77; v++) {
        g = (uni_gamepad_t){.axis_x=v,.axis_y=v,.axis_rx=v,.axis_ry=v};
        switch_state_from_gamepad(&g, &s);
        assert(x(&s,3)==0x800 && y(&s,3)==0x800);
        assert(x(&s,6)==0x800 && y(&s,6)==0x800);
    }
    g = (uni_gamepad_t){.axis_x=78,.axis_y=-78,.axis_rx=-78,.axis_ry=78};
    switch_state_from_gamepad(&g, &s);
    assert(x(&s,3)>0x800 && x(&s,3)<=0x805);
    assert(y(&s,3)>0x800 && y(&s,3)<=0x805);
    assert(x(&s,6)<0x800 && x(&s,6)>=0x7fb);
    assert(y(&s,6)<0x800 && y(&s,6)>=0x7fb);
    unsigned prev_x=0, prev_y=0xfff;
    for (int v=-512; v<=511; v++) {
        g = (uni_gamepad_t){.axis_x=v,.axis_y=v};
        switch_state_from_gamepad(&g, &s);
        assert(x(&s,3)>=prev_x && y(&s,3)<=prev_y);
        assert(x(&s,3)>=0x200 && x(&s,3)<=0xe00);
        prev_x=x(&s,3); prev_y=y(&s,3);
    }
    g = (uni_gamepad_t){.axis_x=INT_MIN,.axis_y=INT_MAX};
    switch_state_from_gamepad(&g, &s);
    assert(x(&s,3)==0x200 && y(&s,3)==0x200);
    switch_state_neutral(&s);
    assert(memcmp(s.bytes, neutral, 9) == 0);
    puts("PASS: button mapping, capture chord, triggers, stick packing, Y direction, full-range monotonicity and neutral reset");
}
