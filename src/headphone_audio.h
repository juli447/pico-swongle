// SPDX-License-Identifier: MIT
#pragma once
typedef struct uni_hid_device_s uni_hid_device_t;
#include <stdbool.h>
#include <stdint.h>
void headphone_audio_init(void);
void headphone_audio_connect(uni_hid_device_t *d);
void headphone_audio_disconnect(uni_hid_device_t *d);
bool headphone_audio_input(uni_hid_device_t *d, const uint8_t *report,
                           uint16_t len);
void headphone_audio_task(void);
// Returns true while the local output chord must be withheld from the console.
bool headphone_audio_output_buttons(uni_hid_device_t *d, bool touchpad, bool mute_button);
void headphone_audio_can_send_now(uint16_t cid);
uint8_t headphone_audio_headset(void);
bool headphone_audio_rumble(uni_hid_device_t *d, uint8_t weak, uint8_t strong);
void headphone_audio_set_streams(bool speaker, bool mic);
void headphone_audio_submit_pcm(const uint8_t *data, uint16_t len);
void headphone_audio_set_control(bool muted, int16_t volume_db_256);

void headphone_usb_suspend(bool suspended);

void headphone_audio_service_io(void);

// Local test diagnostic: short ON-flash groups for observed audio delays.
bool headphone_audio_diagnostic_led(uint32_t now_ms, bool *led);

// Foreground-only, guarded USB service at Bluetooth input packet boundaries.
void headphone_audio_service_usb(void);

// Cosmetic output must share ordered audio TX and never fill its queue.
bool headphone_audio_owns_lightbar(uni_hid_device_t *d);
bool headphone_audio_lightbar_ready(uni_hid_device_t *d);
bool headphone_audio_queue_lightbar(uni_hid_device_t *d, const uint8_t *p, uint16_t len);
