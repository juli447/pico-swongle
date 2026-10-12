// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include "controller/uni_gamepad.h"
void motion_reset(uint32_t now);
void motion_set_input(const uni_gamepad_t *gp, uint32_t now);
void motion_stop(void);
void motion_set_mode(uint8_t mode);
void motion_poll(uint32_t now);
void motion_fill(uint8_t out[36], uint32_t now);
