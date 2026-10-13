// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include "input_mapping.h"
void switch_usb_init(void);
void switch_usb_task(void);
void switch_usb_set_state(const switch_state_t *state);
bool switch_usb_has_host_commands(void);
bool switch_usb_reports_flowing(void);
bool switch_usb_is_mounted(void);
bool switch_usb_has_any_reports(void);
void switch_usb_set_motion(const uni_gamepad_t *gp);
void switch_usb_stop_motion(void);

// Native Switch 2 service, without re-entering the USB event dispatcher.
void switch_usb_service_io(void);
