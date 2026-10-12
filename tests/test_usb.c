// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "switch_usb.h"
#include "tusb.h"
#include "procon_functions.h"
#include "rumble.h"
static uint32_t now;
static uint8_t sent[64];
static unsigned sends;
uint32_t get_absolute_time(void) { return now; }
void flash_get_unique_id(uint8_t *id) { memset(id, 0x12, 8); }
void tusb_init(void) { }
void tud_task(void) { }
bool tud_hid_ready(void) { return true; }
bool tud_hid_report(uint8_t id, const void *buffer, uint16_t len) {
    assert(len == 63); sent[0] = id; memcpy(sent + 1, buffer, len); sends++; return true;
}
void tud_hid_set_report_cb(uint8_t, uint8_t, hid_report_type_t, const uint8_t *, uint16_t);
void tud_mount_cb(void);
void tud_umount_cb(void);
void tud_hid_report_complete_cb(uint8_t, const uint8_t *, uint16_t);
static void send_packet(uint8_t *p, unsigned n) {
    now += 8;
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, p, n);
    switch_usb_task();
}
int main(void) {
    switch_usb_init();
    assert(!switch_usb_is_mounted());
    tud_mount_cb();
    assert(switch_usb_is_mounted());
    switch_usb_task(); // Identify immediately, not after a 250 ms delay.
    assert(sent[0]==0x81 && sent[1]==1 && !switch_usb_has_any_reports());
    uint8_t unknown[] = {0x7f};
    tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_OUTPUT,unknown,1);
    assert(switch_usb_has_any_reports() && !switch_usb_has_host_commands());
    uint8_t p[64] = {0x80, 0x02};
    send_packet(p, 2); assert(sent[0] == 0x81 && sent[1] == 2);
    // Same handshake through a control request, with the ID outside the payload.
    p[0] = 3;
    tud_hid_set_report_cb(0, 0x80, HID_REPORT_TYPE_OUTPUT, p, 1);
    switch_usb_task(); assert(sent[0] == 0x81 && sent[1] == 3);
    assert(switch_usb_has_host_commands());
    // A feature report must be handled too: this was discarded in version 0.1.0.
    p[0] = 4;
    tud_hid_set_report_cb(0, 0x80, HID_REPORT_TYPE_FEATURE, p, 1);
    switch_usb_task(); assert(sent[0] == 0x81 && sent[1] == 4);
    assert(polling_mode == 0x30); // No explicit report-mode request required.
    now += 8; switch_usb_task(); assert(sent[0] == 0x30);
    assert(!switch_usb_reports_flowing()); // Queuing is not proof of delivery.
    tud_hid_report_complete_cb(0, sent, 64);
    assert(switch_usb_reports_flowing());
    now += 1001; assert(!switch_usb_reports_flowing());
    tud_umount_cb(); tud_mount_cb();
    assert(!switch_usb_has_host_commands());
    // Binary MAC length and identity must agree in initialization and device-info replies.
    memset(p,0,64); p[0]=0x80; p[1]=1; send_packet(p,2);
    uint8_t mac[6]; memcpy(mac,sent+4,6);
    assert(mac[0]==0x12 && sent[10]==0 && sent[15]==0);
    memset(p,0,64); p[0]=1; p[10]=2;
    tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_FEATURE,p,11);
    switch_usb_task(); assert(sent[0]==0x21 && memcmp(mac,sent+19,6)==0);
    memset(p, 0, 64); p[0]=1; p[10]=0x10; p[11]=0x3d; p[12]=0x60; p[15]=255;
    send_packet(p, 16);
    assert(sent[0]==0x21 && sent[13]==0x90 && sent[14]==0x10 && sent[19]==44);
    // Reply preserves current buttons and sticks, then normal polling uses them too.
    uni_gamepad_t gp = {.buttons=BUTTON_A,.axis_x=511,.axis_y=-512};
    switch_state_t state; switch_state_from_gamepad(&gp, &state); switch_usb_set_state(&state);
    memset(p,0,64); p[0]=1; p[10]=3; p[11]=0x30;
    send_packet(p,12); assert(sent[0]==0x21 && sent[3]==4);
    now += 8; switch_usb_task(); assert(sent[0]==0x30 && sent[3]==4 && sent[6]==state.bytes[3]);
    switch_state_neutral(&state); switch_usb_set_state(&state);
    now += 8; switch_usb_task(); assert(sent[3]==0 && sent[6]==0 && sent[7]==8 && sent[8]==128);
    tud_umount_cb(); tud_mount_cb(); assert(polling_mode==0);
    switch_usb_task(); // Consume the immediate identification report before rejection tests.
    unsigned before=sends;
    send_packet(p,0); send_packet(p,1); send_packet(p,10);
    assert(sends==before);
    // Exercise motion enable/disable through the actual Switch USB subcommand path.
    gp=(uni_gamepad_t){.accel={0,UNI_STANDARD_GRAVITY,0},.gyro={0,90*UNI_DEG_TO_RAD,0}};
    switch_usb_set_motion(&gp);
    memset(p,0,64); p[0]=1; p[10]=0x40; p[11]=1;
    send_packet(p,12); assert(sent[0]==0x21 && sent[14]==0x40 && imu_enabled==1);
    // Populate three five-millisecond samples, then send one complete motion report.
    for (unsigned i=0;i<3;i++) { now+=5; switch_usb_set_motion(&gp); switch_usb_task(); }
    assert(sent[0]==0x30 && sent[17]==0 && sent[18]==0x10); // native accel Z = +1g
    assert(sent[23]==0x06 && sent[24]==0x05); // native gyro Z = +1286 counts
    // Switch 2's quaternion mode must produce its format tag instead of raw sensor bytes.
    p[11]=2; send_packet(p,12); assert(imu_enabled==2);
    for (unsigned i=0;i<3;i++) { now+=5; switch_usb_set_motion(&gp); switch_usb_task(); }
    assert(sent[0]==0x30 && (sent[19]&3)==2);
    p[11]=0; send_packet(p,12); assert(imu_enabled==0);
    now+=8; switch_usb_task(); assert(sent[0]==0x30);
    for (unsigned i=13;i<49;i++) assert(sent[i]==0);
    // The advertised calibration now has zero origin on every acceleration axis.
    memset(p,0,64); p[0]=1; p[10]=0x10; p[11]=0x20; p[12]=0x60; p[15]=24;
    send_packet(p,16);
    for (unsigned i=20;i<26;i++) assert(sent[i]==0);
    // Rumble-only OUT must decode its eight bytes at offset 2 and produce no protocol ACK.
    uint8_t weak,strong;
    rumble_take(now,&weak,&strong);
    memset(p,0,64); p[0]=0x10;
    uint32_t effect=(1u<<30)|(127u<<23)|(50u<<16)|(32u<<9)|(90u<<2);
    for (unsigned i=0;i<4;i++) p[2+i]=(effect>>(8*i))&255;
    p[6]=0; p[7]=1; p[8]=0x40; p[9]=0x40;
    now+=20;
    before=sends;
    tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_OUTPUT,p,10);
    assert(sends==before && rumble_take(now,&weak,&strong) && weak==32 && strong==250);
    // Disabling vibration through the Switch command must stop and suppress subsequent effects.
    memset(p,0,64); p[0]=1; p[10]=0x48; p[11]=0;
    send_packet(p,12);
    assert(sent[0]==0x21 && sent[14]==0x48 && rumble_take(now,&weak,&strong) && strong==0);
    p[11]=1; send_packet(p,12);
    for (unsigned i=0;i<4;i++) p[2+i]=(effect>>(8*i))&255;
    p[6]=0; p[7]=1; p[8]=0x40; p[9]=0x40; p[10]=2;
    now+=20; send_packet(p,11);
    assert(rumble_take(now,&weak,&strong) && strong==250); // embedded rumble in subcommand report
    tud_umount_cb();
    assert(rumble_take(now,&weak,&strong) && weak==0 && strong==0);
    tud_mount_cb();
    // Exercise truncation, unexpected IDs and all subcommands under sanitizers.
    for (unsigned sub=0; sub<256; sub++) {
        for (unsigned len=0; len<=64; len++) {
            memset(p,0xff,64); p[0]=1; p[10]=sub;
            send_packet(p,len);
        }
    }
    puts("PASS: USB handshake, feature reports, MAC, motion modes 0/1/2, IMU calibration, SPI bounds and malformed packets");
}
