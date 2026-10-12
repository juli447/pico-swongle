// SPDX-License-Identifier: MIT
#include "rumble.h"
#include "switch_usb.h"
#include "tusb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t now;
static uint8_t input[64], rx[64], tx[128], control[64];
static uint32_t available, txlen, limit = 128;
static uint16_t control_len;
uint32_t get_absolute_time(void) { return now; }
uint32_t time_us_32(void) { return now * 1000; }
void flash_get_unique_id(uint8_t *p) { memset(p, 0x12, 8); }
void tusb_init(void) {}
void tud_task(void) {}
bool tud_hid_ready(void) { return true; }
bool tud_hid_report(uint8_t id, const void *p, uint16_t n) {
  assert(n == 63);
  input[0] = id;
  memcpy(input + 1, p, n);
  return true;
}
uint32_t tud_vendor_available(void) { return available; }
uint32_t tud_vendor_read(void *p, uint32_t n) {
  if (n > available)
    n = available;
  memcpy(p, rx, n);
  available = 0;
  return n;
}
uint32_t tud_vendor_write(const void *p, uint32_t n) {
  if (n > limit)
    n = limit;
  assert(txlen + n <= 128);
  memcpy(tx + txlen, p, n);
  txlen += n;
  return n;
}
uint32_t tud_vendor_write_flush(void) { return 0; }
bool tud_control_xfer(uint8_t port, const tusb_control_request_t *r, void *p,
                      uint16_t n) {
  (void)port;
  if (n > r->wLength)
    n = r->wLength;
  memcpy(control, p, n);
  control_len = n;
  return true;
}
bool tud_control_status(uint8_t port, const tusb_control_request_t *r) {
  (void)port;
  (void)r;
  return true;
}
void tud_vendor_rx_cb(uint8_t, const uint8_t *, uint16_t);
void tud_mount_cb(void);
void tud_umount_cb(void);
void tud_suspend_cb(bool);
void tud_hid_set_report_cb(uint8_t, uint8_t, hid_report_type_t, const uint8_t *,
                           uint16_t);
bool tud_vendor_control_xfer_cb(uint8_t, uint8_t,
                                const tusb_control_request_t *);
const uint8_t *tud_descriptor_device_cb(void);
static void command(uint8_t id, uint8_t sub, unsigned n) {
  rx[0] = id;
  rx[2] = 0x42;
  rx[3] = sub;
  available = n;
  txlen = 0;
  tud_vendor_rx_cb(0, NULL, 0);
  now += 8;
  switch_usb_task();
}
int main(void) {
  switch_usb_init();
  tud_mount_cb();
  assert(switch_usb_is_mounted());
  const uint8_t *dev = tud_descriptor_device_cb();
  assert(dev[8] == 0x7e && dev[10] == 0x69 && dev[11] == 0x20);
  tusb_control_request_t r = {
      .bmRequestType = 0xc0, .bRequest = 3, .wLength = 64};
  assert(tud_vendor_control_xfer_cb(0, 0, &r));
  assert(control_len == 64 && control[0] == 1 && control[0x14] == 0x69);
  r.bRequest = 2;
  r.wLength = 16;
  assert(tud_vendor_control_xfer_cb(0, 0, &r));
  assert(control_len == 16 && control[0] == 2);
  r.bmRequestType = 0x40;
  r.bRequest = 4;
  r.wLength = 0;
  assert(tud_vendor_control_xfer_cb(0, 0, &r));
  memset(rx, 0, 64);
  rx[8] = 9;
  command(3, 0x0a, 9);
  assert(txlen == 8 && tx[2] == 0x42 && tx[5] == 0xf8 && input[0] == 9);
  uni_gamepad_t gp = {.buttons = BUTTON_A | BUTTON_SHOULDER_R,
                      .misc_buttons =
                          MISC_BUTTON_TOUCHPAD | MISC_BUTTON_SELECT};
  switch_state_t s;
  switch_state_from_gamepad(&gp, &s);
  switch_state_set_gamechat(&gp, false, &s);
  switch_usb_set_state(&s);
  now += 8;
  switch_usb_task();
  assert(input[3] == 0x11 && input[4] == 0x40 && input[5] == 0x10);
  gp.misc_buttons = MISC_BUTTON_CAPTURE;
  switch_state_from_gamepad(&gp, &s);
  switch_state_set_gamechat(&gp, true, &s);
  assert(s.gamechat && !(s.bytes[1] & 0x20));
  switch_state_from_gamepad(&gp, &s);
  switch_state_set_gamechat(&gp, false, &s);
  assert(!s.gamechat && (s.bytes[1] & 0x20));
  switch_state_neutral(&s);
  switch_usb_set_state(&s);
  now += 8;
  switch_usb_task();
  assert(input[5] == 0);
  memset(rx, 0, 64);
  rx[8] = 80;
  rx[12] = 0x80;
  rx[13] = 0x30;
  rx[14] = 1;
  limit = 13;
  command(2, 4, 16);
  for (unsigned i = 0; i < 10; i++)
    switch_usb_task();
  assert(txlen == 96 && tx[8] == 80 && tx[16] == 1);
  limit = 128;
  memset(rx, 0, 64);
  command(0x15, 4, 24);
  assert(txlen == 0);
  command(0x15, 4, 25);
  assert(txlen == 25 && tx[8] == 1);
  command(0x15, 2, 25);
  assert(txlen == 25 && tx[8] == 1);
  // Malformed commands must not read beyond their payload or enqueue a reply.
  for (unsigned id = 0; id < 256; id++)
    for (unsigned sub = 0; sub < 7; sub++)
      for (unsigned n = 0; n < 17; n++) {
        memset(rx, 0, 64);
        command((uint8_t)id, (uint8_t)sub, n);
      }
  gp = (uni_gamepad_t){.accel = {0, UNI_STANDARD_GRAVITY, 0}};
  for (unsigned i = 0; i < 40; i++) {
    now += 8;
    switch_usb_set_motion(&gp);
  }
  memset(rx, 0, 64);
  rx[8] = 4;
  command(0x0c, 4, 9);
  now += 8;
  switch_usb_task();
  assert(input[15] == 30);
  uint8_t held[28];
  memcpy(held, input + 18, 28);
  uint8_t old_timing[2] = {input[16], input[17]};
  now += 200;
  switch_usb_task();
  assert(memcmp(held, input + 18, 28) ==
         0); // Lost sensor input must hold attitude.
  assert(input[16] != old_timing[0] || input[17] != old_timing[1]);
  rx[8] = 4;
  command(0x0c, 5, 9);
  now += 8;
  switch_usb_task();
  assert(input[15] == 0);
  uint8_t rum[22] = {0};
  rum[2] = 0xfc;
  rum[3] = 0x0f; // amplitude 1023 at packed bit10
  tud_hid_set_report_cb(0, 2, HID_REPORT_TYPE_OUTPUT, rum, 22);
  uint8_t weak, strong;
  assert(rumble_take(now, &weak, &strong) && strong == 255 && weak == 0);
  tud_suspend_cb(false);
  assert(rumble_take(now, &weak, &strong) && !weak && !strong);
  tud_umount_cb();
  assert(!switch_usb_has_host_commands() && !switch_usb_is_mounted());
  puts("Switch 2 handshake, packet bounds, C mapping, calibration, motion "
       "gates, rumble and lifecycle tests passed");
}
