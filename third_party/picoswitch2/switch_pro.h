// SPDX-License-Identifier: MIT
// Minimal normalized input shared with the unmodified upstream motion translator.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define SWITCH_MOTION_TS_DS5_THIRD_US 0u
#define SWITCH_MOTION_TS_100US_16 1u
typedef struct {
 int16_t accel[3], gyro[3];
 uint32_t motion_sequence, motion_timestamp;
 uint8_t motion_timestamp_valid, motion_timestamp_unit, has_motion;
} switch_pro_input_t;
