// SPDX-License-Identifier: MIT
#include "input_mapping.h"
#include <stdbool.h>
#include <string.h>

// Bluepad32: +Y points down. Switch: +Y points up.
// Match 2wiCC's factory calibration: centre 0x800, range +/-0x600.
#define STICK_DEADZONE 77 // Approximately 15% of each axis's centre-to-edge range, both sticks.
static uint16_t axis(int32_t v, bool invert) {
    if (v < -512) v = -512;
    if (v > 511) v = 511;
    int32_t magnitude = v < 0 ? -v : v;
    int32_t offset = 0;
    if (magnitude > STICK_DEADZONE) {
        // Start smoothly at the deadzone boundary and retain full travel at the edge.
        offset = (magnitude - STICK_DEADZONE) * 0x600 /
            ((v < 0 ? 512 : 511) - STICK_DEADZONE);
        if (v < 0) offset = -offset;
    }
    return (uint16_t)(0x800 + (invert ? -offset : offset));
}

static void pack_stick(uint8_t *out, uint16_t x, uint16_t y) {
    out[0] = x & 0xff;
    out[1] = ((x >> 8) & 0x0f) | ((y & 0x0f) << 4);
    out[2] = y >> 4;
}

void switch_state_neutral(switch_state_t *s) {
    memset(s, 0, sizeof(*s));
    pack_stick(&s->bytes[3], 0x800, 0x800);
    pack_stick(&s->bytes[6], 0x800, 0x800);
}

void switch_state_from_gamepad(const uni_gamepad_t *g, switch_state_t *s) {
    switch_state_neutral(s);
    // Preserve physical positions: south -> B, east -> A, west -> Y, north -> X.
    if (g->buttons & BUTTON_A) s->bytes[0] |= 0x04;
    if (g->buttons & BUTTON_B) s->bytes[0] |= 0x08;
    if (g->buttons & BUTTON_X) s->bytes[0] |= 0x01;
    if (g->buttons & BUTTON_Y) s->bytes[0] |= 0x02;
    if (g->buttons & BUTTON_SHOULDER_R) s->bytes[0] |= 0x40;
    if ((g->buttons & BUTTON_TRIGGER_R) || g->throttle > 128) s->bytes[0] |= 0x80;
    if (g->buttons & BUTTON_SHOULDER_L) s->bytes[2] |= 0x40;
    if ((g->buttons & BUTTON_TRIGGER_L) || g->brake > 128) s->bytes[2] |= 0x80;
    if (g->buttons & BUTTON_THUMB_R) s->bytes[1] |= 0x04;
    if (g->buttons & BUTTON_THUMB_L) s->bytes[1] |= 0x08;
    if (g->misc_buttons & MISC_BUTTON_START) s->bytes[1] |= 0x02;
    bool home = (g->misc_buttons & MISC_BUTTON_SYSTEM) != 0;
    bool select = (g->misc_buttons & MISC_BUTTON_SELECT) != 0;
    if (home && select) s->bytes[1] |= 0x20; // PS/Xbox + Share/View -> Capture
    else {
        if (home) s->bytes[1] |= 0x10;
        if (select) s->bytes[1] |= 0x01;
    }
    if (g->misc_buttons & MISC_BUTTON_CAPTURE) s->bytes[1] |= 0x20;
    if (g->dpad & DPAD_DOWN) s->bytes[2] |= 0x01;
    if (g->dpad & DPAD_UP) s->bytes[2] |= 0x02;
    if (g->dpad & DPAD_RIGHT) s->bytes[2] |= 0x04;
    if (g->dpad & DPAD_LEFT) s->bytes[2] |= 0x08;
    pack_stick(&s->bytes[3], axis(g->axis_x, false), axis(g->axis_y, true));
    pack_stick(&s->bytes[6], axis(g->axis_rx, false), axis(g->axis_ry, true));
}

void switch_state_set_gamechat(const uni_gamepad_t *gp, bool xbox, switch_state_t *s) {
    s->gamechat = (gp->misc_buttons & MISC_BUTTON_TOUCHPAD) ||
        (xbox && (gp->misc_buttons & MISC_BUTTON_CAPTURE));
    if (xbox && (gp->misc_buttons & MISC_BUTTON_CAPTURE) &&
        !((gp->misc_buttons & MISC_BUTTON_SYSTEM) && (gp->misc_buttons & MISC_BUTTON_SELECT)))
        s->bytes[1] &= (uint8_t)~0x20; // Share -> C; Home+View remains Capture.
}
