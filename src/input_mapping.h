// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "controller/uni_gamepad.h"

typedef struct { uint8_t bytes[9]; bool gamechat; } switch_state_t;
void switch_state_neutral(switch_state_t *state);
void switch_state_from_gamepad(const uni_gamepad_t *gp, switch_state_t *state);

void switch_state_set_gamechat(const uni_gamepad_t *gp, bool xbox, switch_state_t *state);
