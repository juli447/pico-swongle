// SPDX-License-Identifier: MIT
#include "rumble.h"
#include "switch_haptics.h"

static SwitchHapticsDecoder decoder;
static bool enabled=true, have_packet, dirty, have_sent;
static uint8_t weak_level, strong_level;
static uint32_t packet_at, sent_at;

void rumble_stop(void) {
    decoder.reset();
    have_packet=false;
    weak_level=strong_level=0;
    dirty=true;
}
void rumble_reset(void) {
    enabled=true;
    have_sent=false;
    rumble_stop();
}
void rumble_enable(bool value) {
    enabled=value;
    if (!enabled) rumble_stop();
}
void rumble_receive(const uint8_t payload[8], uint32_t now) {
    if (!enabled || !payload) return;
    SwitchRumbleOutput value=decoder.decode(payload);
    uint8_t weak=value.high_frequency_magnitude;
    uint8_t strong=value.low_frequency_magnitude;
    rumble_receive_levels(weak,strong,now);
}
void rumble_receive_levels(uint8_t weak,uint8_t strong,uint32_t now) {
    if (!enabled) return;
    if (weak!=weak_level || strong!=strong_level) dirty=true;
    weak_level=weak; strong_level=strong;
    packet_at=now; have_packet=true;
}
bool rumble_take(uint32_t now,uint8_t *weak,uint8_t *strong) {
    if (have_packet && now-packet_at>=300) rumble_stop();
    bool vibrating=weak_level || strong_level;
    // Coalesce rapidly changing effects; refresh held effects to extend Bluepad32's duration timer.
    if (dirty) {
        if (vibrating && have_sent && now-sent_at<20) return false;
    } else if (!vibrating || !have_sent || now-sent_at<50) return false;
    *weak=weak_level; *strong=strong_level;
    dirty=false; have_sent=true; sent_at=now;
    return true;
}
