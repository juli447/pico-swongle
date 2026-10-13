// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool enabled, chord_consumed, output_known;
    uint32_t epoch_ms, sent_ms;
} controller_rgb_t;
void controller_rgb_init(controller_rgb_t *s, uint32_t now_ms);
bool controller_rgb_buttons(controller_rgb_t *s, bool ps, bool touchpad);
bool controller_rgb_next(controller_rgb_t *s, uint32_t now_ms,
                         uint8_t *r, uint8_t *g, uint8_t *b);

// Like PS+Share capture: pass single buttons normally, consume the combination.
void controller_rgb_filter_buttons(controller_rgb_t *s, uint32_t now_ms,
    bool ps, bool touchpad, bool audio_chord,
    bool *ps_out, bool *touchpad_out);
