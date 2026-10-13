// SPDX-License-Identifier: MIT
#include "controller_rgb.h"
void controller_rgb_init(controller_rgb_t *s, uint32_t now_ms) {
    *s = (controller_rgb_t){.enabled = true, .epoch_ms = now_ms};
}
bool controller_rgb_buttons(controller_rgb_t *s, bool ps, bool touchpad) {
    if (ps && touchpad && !s->chord_consumed) {
        s->chord_consumed = true;
        s->enabled = !s->enabled;
        s->output_known = false;
    }
    bool consumed = s->chord_consumed;
    if (!ps && !touchpad) s->chord_consumed = false;
    return consumed;
}
bool controller_rgb_next(controller_rgb_t *s, uint32_t now_ms,
                         uint8_t *r, uint8_t *g, uint8_t *b) {
    if (s->output_known && (!s->enabled || now_ms - s->sent_ms < 33u))
        return false;
    *r = *g = *b = 0;
    if (s->enabled) {
        // Constant brightness, six continuous colour ramps, 2.4 s per cycle.
        uint32_t phase = ((now_ms - s->epoch_ms) % 2400u) * 1536u / 2400u;
        uint8_t x = (uint8_t)((phase & 255u) * 128u / 255u);
        switch (phase >> 8) {
            case 0: *r=128; *g=x; break;
            case 1: *r=128-x; *g=128; break;
            case 2: *g=128; *b=x; break;
            case 3: *g=128-x; *b=128; break;
            case 4: *r=x; *b=128; break;
            default: *r=128; *b=128-x; break;
        }
    }
    s->output_known = true;
    s->sent_ms = now_ms;
    return true;
}

void controller_rgb_filter_buttons(controller_rgb_t *s, uint32_t now_ms,
    bool ps, bool touchpad, bool audio_chord,
    bool *ps_out, bool *touchpad_out) {
    (void)now_ms;
    bool consumed = controller_rgb_buttons(s, ps, touchpad && !audio_chord);
    // Direct combo masking, just like Home+Share capture. Home retains its
    // complete down/hold/up behaviour; there is no release pulse or timeout.
    *ps_out = ps && !consumed && !audio_chord;
    *touchpad_out = touchpad && !consumed && !audio_chord;
}
