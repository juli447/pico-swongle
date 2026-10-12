// SPDX-License-Identifier: MIT
// USB protocol and calibration data are adapted from KNfLrPn's 2wiCC.
#include "switch_usb.h"
#include <string.h>
#include "pico/time.h"
#include "tusb.h"
#include "procon_functions.h"
#include "motion.h"
#include "rumble.h"

_Static_assert(sizeof(ControllerDataReport_t) == 63, "USB payload size");
static switch_state_t current_state;
static uint8_t replies[8][64];
static unsigned reply_head, reply_tail;
static bool host_commands, regular_completed;
static uint32_t regular_completed_at;
static bool usb_mounted, any_reports, identify_pending;

bool switch_usb_is_mounted(void) { return usb_mounted; }
bool switch_usb_has_any_reports(void) { return any_reports; }

bool switch_usb_has_host_commands(void) { return host_commands; }
bool switch_usb_reports_flowing(void) {
    return regular_completed &&
        to_ms_since_boot(get_absolute_time()) - regular_completed_at < 1000;
}

static void fill_state(ControllerData_t *out) {
    ControllerDigital_t buttons;
    ControllerAnalog_t sticks;
    memcpy(&buttons, current_state.bytes, 3);
    memcpy(&sticks, current_state.bytes + 3, 6);
    insert_constate_to_condata(out, &buttons, &sticks);
}

void switch_usb_set_state(const switch_state_t *state) { current_state = *state; }
void switch_usb_set_motion(const uni_gamepad_t *gp) {
    motion_set_input(gp,to_ms_since_boot(get_absolute_time()));
}
void switch_usb_stop_motion(void) { motion_stop(); }

void switch_usb_init(void) {
    switch_state_neutral(&current_state);
    motion_reset(to_ms_since_boot(get_absolute_time()));
    rumble_reset();
    get_unique_id(); // Called on the same foreground core as Bluetooth persistence.
    tusb_init();
}

static void reset_usb_session(void) {
    polling_mode = 0;
    imu_enabled = 0;
    motion_set_mode(0);
    rumble_reset();
    reply_head = reply_tail = 0;
    host_commands = regular_completed = false;
    any_reports = false;
    identify_pending = true;
    last_report_time = to_ms_since_boot(get_absolute_time());
}
void tud_mount_cb(void) { reset_usb_session(); usb_mounted = true; }
void tud_umount_cb(void) { reset_usb_session(); usb_mounted = false; }
void tud_suspend_cb(bool wakeup) { (void)wakeup; rumble_stop(); }
void tud_resume_cb(void) { }
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
    (void)instance;
    if (len && report[0] == 0x30) {
        regular_completed = true;
        regular_completed_at = to_ms_since_boot(get_absolute_time());
    }
}
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t type, uint8_t *buffer, uint16_t size) {
    (void)instance; (void)report_id; (void)type; (void)buffer; (void)size;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t type, uint8_t const *buffer, uint16_t len) {
    (void)instance;
    any_reports = true; // Include rejected/unknown reports in transport diagnostics.
    // SwiCC handles protocol packets from both SET_REPORT types and interrupt OUT.
    // Do not discard a valid command solely because the host calls it a feature report.
    if (len == 0 || (type != HID_REPORT_TYPE_OUTPUT && type != HID_REPORT_TYPE_FEATURE)) return;
    uint8_t packet[64] = {0};
    // Interrupt OUT includes report ID; control SET_REPORT supplies it separately.
    if (report_id) {
        if (len > 63) return;
        packet[0] = report_id;
        memcpy(packet + 1, buffer, len);
        len++;
    } else {
        if (len > 64) return;
        memcpy(packet, buffer, len);
    }
    // Rumble-only packets need no reply, so a full reply queue must not discard them.
    if (packet[0]==0x10) {
        if (len>=10) rumble_receive(packet+2,to_ms_since_boot(get_absolute_time()));
        return;
    }
    unsigned next = (reply_head + 1) % 8;
    if (next == reply_tail) return;
    memset(usb_special_buf, 0, 64);
    switch (packet[0]) {
    case 0x80:
        if (len < 2) return;
        output_report_0x80(packet, usb_special_buf);
        host_commands = true;
        // The USB timeout-disable handshake completes wired initialization.
        // Some hosts do not send a separate 0x03 report-mode command afterward.
        if (packet[1] == 0x04 && polling_mode == 0) polling_mode = 0x30;
        break;
    case 0x01:
        if (len < 11) return;
        switch (packet[10]) {
        case 0x10: case 0x11: case 0x12:
            if (len < 16) return;
            // SPI data starts at byte 20 of the reply; leave room in the 64-byte buffer.
            if (packet[15] > 44) packet[15] = 44;
            break;
        case 0x01: case 0x03: case 0x30: case 0x40: case 0x41: case 0x48:
            if (len < 12) return;
            break;
        }
        output_report_0x01(packet, usb_special_buf);
        if (packet[10]==0x48) rumble_enable(packet[11]!=0);
        rumble_receive(packet+2,to_ms_since_boot(get_absolute_time()));
        if (packet[10]==0x40) motion_set_mode(imu_enabled);
        host_commands = true;
        if (polling_mode == 0) polling_mode = 0x30;
        if (usb_special_buf[0] == 0x21)
            fill_state((ControllerData_t *)(usb_special_buf + 1));
        break;
    default:
        return;
    }
    memcpy(replies[reply_head], usb_special_buf, 64);
    reply_head = next;
}

void switch_usb_task(void) {
    motion_poll(to_ms_since_boot(get_absolute_time()));
    tud_task();
    if (!tud_hid_ready()) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (reply_head != reply_tail) {
        uint8_t *reply = replies[reply_tail];
        if (tud_hid_report(reply[0], reply + 1, 63)) {
            reply_tail = (reply_tail + 1) % 8;
            last_report_time = now;
        }
    } else if (polling_mode == 0x30) {
        if (now - last_report_time < (imu_enabled ? 15u : 8u)) return;
        ControllerDataReport_t report = {0};
        fill_state(&report.controller_data);
        motion_fill(report.imu_data,now);
        if (tud_hid_report(0x30, &report, sizeof(report))) last_report_time = now;
    } else if (identify_pending || now - last_report_time > 100) {
        output_mac_addr(NULL, usb_special_buf);
        if (tud_hid_report(usb_special_buf[0], usb_special_buf + 1, 63)) {
            last_report_time = now;
            identify_pending = false;
        }
    }
}
