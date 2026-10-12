// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void rumble_reset(void);
void rumble_stop(void);
void rumble_enable(bool enabled);
void rumble_receive(const uint8_t payload[8], uint32_t now);
void rumble_receive_levels(uint8_t weak, uint8_t strong, uint32_t now);
bool rumble_take(uint32_t now, uint8_t *weak, uint8_t *strong);
#ifdef __cplusplus
}
#endif
