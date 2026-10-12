// SPDX-License-Identifier: Apache-2.0
// Adapted from PicoSwitch2 v1.5.0 / v2.5.0 by notsosaelin and contributors.
// See third_party/picoswitch2/{LICENSE,NOTICE}. Integration changes for Pico
// Swongle.
#include "hardware/flash.h"
#include "ns2_ds5_motion.h"
#include "ns2_firmware_profile.h"
#include "ns2_pairing_crypto.h"
#include "pico/time.h"
#include "rumble.h"
#include "switch_usb.h"
#include "tusb.h"
#include <math.h>
#include <string.h>
static const uint8_t ns2_device_desc[] = {
    0x12, // bLength
    0x01, // bDescriptorType (Device)
    0x00,
    0x02, // bcdUSB 2.00
    0xEF, // bDeviceClass (Misc, IAD composite)
    0x02, // bDeviceSubClass
    0x01, // bDeviceProtocol
    0x40, // bMaxPacketSize0 64
    0x7E,
    0x05, // idVendor 0x057E (Nintendo)
    0x69,
    0x20, // idProduct 0x2069 (Switch 2 Pro Controller)
    0x10,
    0x02, // bcdDevice 2.10 — deliberately NOT the real 2.00. Hardware A/B
          // settled it: diag3 (2.10) and diag4 (2.00) BOTH stall the console at
          // the same point, so the console does NOT gate on bcdDevice. On PC,
          // though, 2.00 collides with the user's retail PC2 (also 2.00) in
          // Windows' WinUSB cache (key = VID+PID+bcdDevice) → WinUSB won't bind
          // → no Steam debug loop. 2.10 is a distinct key: console-neutral AND
          // it preserves the PC/WinUSB proxy we use to exercise the vendor
          // protocol.
    0x01, // iManufacturer
    0x02, // iProduct
    0x03, // iSerialNumber
    0x01, // bNumConfigurations
};

static const char *ns2_strings[] = {
    (const char[]){0x09, 0x04}, // 0: language id (en-US)
    "Nintendo",                 // 1: manufacturer
    "Switch 2 Pro Controller",  // 2: product
    "00",                       // 3: serial number
};

// HID report descriptor (97 bytes) — exact from the real PC2.
static const uint8_t ns2_report_desc[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,       // Usage (Game Pad)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x05,       //   Report ID (5)
    0x05, 0xFF,       //   Usage Page (Vendor 0xFF)
    0x09, 0x01,       //   Usage (0x01)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x95, 0x3F,       //   Report Count (63)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x85, 0x09,       //   Report ID (9)
    0x09, 0x01,       //   Usage (0x01)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (0x01)
    0x29, 0x15,       //   Usage Maximum (0x15)  -> 21 buttons
    0x25, 0x01,       //   Logical Maximum (1)
    0x95, 0x15,       //   Report Count (21)
    0x75, 0x01,       //   Report Size (1)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x03,       //   Report Size (3)
    0x81, 0x03,       //   Input (Const) -> 3-bit pad
    0x05, 0x01,       //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x01,       //   Usage (Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x09, 0x33,       //     Usage (Rx)
    0x09, 0x35,       //     Usage (Rz)
    0x26, 0xFF, 0x0F, //     Logical Maximum (4095)
    0x95, 0x04,       //     Report Count (4)
    0x75, 0x0C,       //     Report Size (12)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0xC0,             //   End Collection
    0x05, 0xFF,       //   Usage Page (Vendor 0xFF)
    0x09, 0x02,       //   Usage (0x02)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x95, 0x34,       //   Report Count (52)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x85, 0x02,       //   Report ID (2)
    0x09, 0x01,       //   Usage (0x01)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x02,       //   Output (Data,Var,Abs)
    0xC0,             // End Collection
};
_Static_assert(sizeof(ns2_report_desc) == 97,
               "PC2 HID report descriptor must be 97 bytes");

#define NS2_CONFIG_LEN 80
static const uint8_t ns2_config_desc[] = {
    0x09,
    0x02,
    (NS2_CONFIG_LEN & 0xFF),
    (NS2_CONFIG_LEN >> 8),
    0x02,
    0x01,
    0x00,
    0xC0,
    0xFA,
    // IAD + Interface 0: HID
    0x08,
    0x0B,
    0x00,
    0x01,
    0x03,
    0x00,
    0x00,
    0x00,
    0x09,
    0x04,
    0x00,
    0x00,
    0x02,
    0x03,
    0x00,
    0x00,
    0x00,
    0x09,
    0x21,
    0x11,
    0x01,
    0x00,
    0x01,
    0x22,
    0x61,
    0x00, // HID desc, report len 97
    0x07,
    0x05,
    0x81,
    0x03,
    0x40,
    0x00,
    0x01, // EP 0x81 interrupt IN, bInterval 1 = 1000Hz FS max
    0x07,
    0x05,
    0x01,
    0x03,
    0x40,
    0x00,
    0x01, // EP 0x01 interrupt OUT, bInterval 1
    // IAD + Interface 1: vendor bulk
    0x08,
    0x0B,
    0x01,
    0x01,
    0xFF,
    0x00,
    0x00,
    0x00,
    0x09,
    0x04,
    0x01,
    0x00,
    0x02,
    0xFF,
    0x00,
    0x00,
    0x00,
    0x07,
    0x05,
    0x02,
    0x02,
    0x40,
    0x00,
    0x00, // EP 0x02 bulk OUT
    0x07,
    0x05,
    0x82,
    0x02,
    0x40,
    0x00,
    0x00, // EP 0x82 bulk IN
};
#define FACTORY_BASE 0x13000u
#define FACTORY_SIZE 0x160u
static uint8_t factory[FACTORY_SIZE];

// Identity + info blocks the console fetches over EP0 vendor control BEFORE it
// will touch the bulk command channel (verified from ndeadly's USB capture, PC2
// = device 7; see tud_vendor_control_xfer_cb). ns2_ctrl_identity is the first
// 64 B of factory memory (0x13000: 01 00, serial, VID, PID, 01 06 01, colours)
// with a 0xFF tail; built in ns2_factory_init once `factory` is populated.
// ns2_ctrl_info is the fixed 16-B reply to vendor request 0x02
// (firmware/version + a per-unit id). The opaque middle and per-unit bytes
// remain verbatim from our capture; only the documented leading firmware
// triplet advances to the updated retail version.
static uint8_t ns2_ctrl_identity[64];
static uint8_t ns2_ctrl_info[16];

// Command 0x10/0x01 response payload:
// firmware[3], controller type, Bluetooth patch[3], pad, DSP[3], pad.
static uint8_t ns2_firmware_info[12];
static uint8_t unit_id[6];

static void fac(uint32_t addr, const uint8_t *d, size_t n) {
  uint32_t o = addr - FACTORY_BASE;
  if (o + n <= FACTORY_SIZE)
    memcpy(&factory[o], d, n);
}

static void ns2_factory_init(void) {
  static const uint8_t blk[40] = {
      0x01, 0xAD, 0xD9, 0x9A, 0x55, 0x56, 0x65, 0xA0, 0x00, 0x0A,
      0xA0, 0x00, 0x0A, 0xE2, 0x20, 0x0E, 0xE2, 0x20, 0x0E, 0x9A,
      0xAD, 0xD9, 0x9A, 0xAD, 0xD9, 0x0A, 0xA5, 0x50, 0x0A, 0xA5,
      0x50, 0x2F, 0xF6, 0x62, 0x2F, 0xF6, 0x62, 0x0A, 0xFF, 0xFF};
  memset(factory, 0, sizeof(factory));
  ns2_firmware_build_ep0_info(ns2_ctrl_info, unit_id);
  ns2_firmware_build_command_info(0x02,
                                  ns2_firmware_info); // 0x02 = Pro Controller
  fac(0x13000, (const uint8_t[]){0x01, 0x00}, 2);
  uint8_t serial[16] = {'S', 'W'};
  static const char hex[] = "0123456789ABCDEF";
  for (unsigned i = 0; i < 6; i++) {
    serial[2 + i * 2] = hex[unit_id[i] >> 4];
    serial[3 + i * 2] = hex[unit_id[i] & 15];
  }
  fac(0x13002, serial, sizeof(serial));
  fac(0x13012, (const uint8_t[]){0x7E, 0x05}, 2); // VID
  fac(0x13014, (const uint8_t[]){0x69, 0x20}, 2); // PID
  fac(0x13016, (const uint8_t[]){0x01, 0x06, 0x01}, 3);
  uint8_t body_color[3];
  memcpy(body_color, (uint8_t[]){0x32, 0x32, 0x32}, 3);
  fac(0x13019, body_color, sizeof(body_color)); // configured body colour
  fac(0x1301C, (const uint8_t[]){0xA0, 0xA0, 0xA0}, 3); // button colour
  fac(0x1301F, (const uint8_t[]){0xE6, 0xE6, 0xE6}, 3); // highlight
  fac(0x13022, (const uint8_t[]){0x32, 0x32, 0x32}, 3); // grip
  fac(0x13040,
      (const uint8_t[]){0x3B, 0xE0, 0xD3, 0x41, 0xC6, 0x60, 0x6A, 0xBC, 0x4D,
                        0xD7, 0xA2, 0xBB, 0x71, 0x1E, 0xDD, 0x37},
      16);
  // 0x13060..0x1307F (32 B) reads back erased/unprogrammed (0xFF) on a real
  // unit, not the previously-hardcoded `4C 09 00 00` (source
  // unknown/unannotated). Confirmed by TWO independent real-capture sources
  // (2026-07-12): ndeadly's raw USB capture (a 32-byte read at this address,
  // all 0xFF) and Dycool/NS-PC-Control's own factory-table comment ("reads back
  // erased (0xFF) on the real unit — Captured read: addr=0x13060 len=0x20").
  // The factory[] array default-zero-fills anything not explicitly populated
  // (see the memset above), so this region must be set to 0xFF explicitly, not
  // just left unpopulated.
  fac(0x13060,
      (const uint8_t[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
      32);
  fac(0x13080, blk, 40);
  fac(0x130A8,
      (const uint8_t[]){0x00, 0x06, 0x60, 0x00, 0x08, 0x80, 0x00, 0x06, 0x60},
      9); // L stick cal
  fac(0x130C0, blk, 40);
  fac(0x130E8,
      (const uint8_t[]){0x00, 0x08, 0x80, 0x00, 0x06, 0x60, 0x00, 0x06, 0x60},
      9); // R stick cal
  // Magnetometer bias (zero, unit never calibrated) + accelerometer bias, both
  // float32 x,y,z — decoded 2026-07-10 (docs/switch2/report-0x09-motion.md
  // "Factory motion calibration") from this repo's own SPI dump but never wired
  // into the served memory table until 2026-07-12, when a fresh independent
  // decode of a real console-side USB capture (ndeadly's
  // rumble-procon-gccon.pcapng, a DIFFERENT physical unit) confirmed the
  // console actually reads this exact address (0x02/04, len 0x18) during init
  // and expects non-zero data there — this repo was previously returning
  // zero-fill (memset default) for any read at 0x13100, a real gap between
  // documented-and-decoded vs. actually-served. Exact bytes are this repo's own
  // unit's SPI dump (`dumps/SPI/2069_spi_dump_2026-07-10_1422.bin`), for
  // internal consistency with the surrounding factory-block entries above.
  fac(0x13100,
      (const uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0xC1, 0xF9, 0x23, 0x3E,
                        0x51, 0xAC, 0x8C, 0xBD, 0x21, 0x0F, 0x26, 0x41},
      24);
  fac(0x13140,
      (const uint8_t[]){0x00, 0xD7, 0xA3, 0xBC, 0x41, 0xD7, 0xA3, 0xBC, 0x41},
      9);

  // Identity block for the EP0 vendor 0x03 request = first 0x25 B of factory
  // (01 00, serial, VID, PID, 01 06 01, colours) then 0xFF fill to 64 (matches
  // the capture).
  memset(ns2_ctrl_identity, 0xFF, sizeof(ns2_ctrl_identity));
  memcpy(ns2_ctrl_identity, factory, 0x25);
}

static void ns2_mem_read(uint8_t subcommand, uint32_t addr, uint8_t len,
                         uint8_t *out) {
  ns2_firmware_diagnostics_record_read(subcommand, addr, len);
  for (uint8_t i = 0; i < len; i++) {
    uint32_t a = addr + i;
    if (a >= FACTORY_BASE && a < FACTORY_BASE + FACTORY_SIZE)
      out[i] = factory[a - FACTORY_BASE];
    else if (a == 0x1FA000)
      out[i] = 0x00; // Bluetooth pairing entry count = 0 (no bonds over USB)
    else {
      // Sparse, observed post-update state supplied by the selected profile.
      if (!ns2_firmware_profile_flash_byte(a, &out[i]))
        out[i] = 0xFF; // uninitialised flash reads as 0xFF
    }
  }
}

//--------------------------------------------------------------------+

static switch_state_t current_state;
static switch_pro_input_t sensor;
static ns2_ds5_motion_state_t orientation;
static bool mounted, any_reports, host_commands, streaming, imu_enabled,
    completed;
static uint8_t report_id = 9, ltk[16];
static uint32_t counter;
static uint32_t sent_at, completed_at, sensor_at;
static uint16_t previous_motion_tick;
static bool motion_clock_valid;
static const uint8_t public_b1[16] = {0x5c, 0xf6, 0xee, 0x79, 0x2c, 0xdf,
                                      0x05, 0xe1, 0xba, 0x2b, 0x63, 0x25,
                                      0xc4, 0x1a, 0x5f, 0x10};
// Preserve complete multi-packet replies when the USB TX FIFO is busy.
static uint8_t replies[8][128];
static uint16_t reply_size[8], reply_offset;
static unsigned head, tail;
static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static void reset_session(void) {
  any_reports = host_commands = streaming = imu_enabled = completed = false;
  head = tail = reply_offset = 0;
  motion_clock_valid = false;
  report_id = 9;
  counter = 0;
  memset(ltk, 0, sizeof(ltk));
  rumble_reset();
}
void switch_usb_init(void) {
  switch_state_neutral(&current_state);
  memset(&sensor, 0, sizeof(sensor));
  ns2_ds5_motion_reset(&orientation);
  uint8_t uid[8];
  flash_get_unique_id(uid);
  memcpy(unit_id, uid, 6);
  unit_id[0] = (unit_id[0] & 0xfc) | 2;
  ns2_factory_init();
  reset_session();
  tusb_init();
}
void switch_usb_set_state(const switch_state_t *s) { current_state = *s; }
static int16_t quantize(float v) {
  if (!isfinite(v))
    return 0;
  if (v > 32767)
    return 32767;
  if (v < -32768)
    return -32768;
  return (int16_t)lroundf(v);
}
void switch_usb_set_motion(const uni_gamepad_t *gp) {
  // Sony/Bluepad Y-up sensor frame -> Pro2 pitch/roll/yaw: X,-Z,Y.
  const float a[3] = {gp->accel[0], -gp->accel[2], gp->accel[1]};
  const float g[3] = {gp->gyro[0], -gp->gyro[2], gp->gyro[1]};
  for (unsigned i = 0; i < 3; i++) {
    sensor.accel[i] = quantize(a[i] * 4096.0f / UNI_STANDARD_GRAVITY);
    sensor.gyro[i] = quantize(g[i] * 16.384f / UNI_DEG_TO_RAD);
  }
  sensor.has_motion = 1;
  sensor.motion_sequence++;
  sensor_at = now_ms();
  ns2_ds5_motion_update(&orientation, &sensor, time_us_32());
}
void switch_usb_stop_motion(void) {
  memset(sensor.gyro, 0, sizeof(sensor.gyro));
}
bool switch_usb_is_mounted(void) { return mounted; }
bool switch_usb_has_any_reports(void) { return any_reports; }
bool switch_usb_has_host_commands(void) { return host_commands; }
bool switch_usb_reports_flowing(void) {
  return completed && now_ms() - completed_at < 1000;
}
void tud_mount_cb(void) {
  reset_session();
  mounted = true;
}
void tud_umount_cb(void) {
  reset_session();
  mounted = false;
}
void tud_suspend_cb(bool wakeup) {
  (void)wakeup;
  rumble_stop();
}
void tud_resume_cb(void) {}
void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *r,
                                uint16_t n) {
  (void)instance;
  if (n && (r[0] == 9 || r[0] == 5)) {
    completed = true;
    completed_at = now_ms();
  }
}
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t id,
                               hid_report_type_t type, uint8_t *b, uint16_t n) {
  (void)instance;
  (void)id;
  (void)type;
  (void)b;
  (void)n;
  return 0;
}
static uint8_t motor_amp(const uint8_t *p) {
  uint64_t v = 0;
  for (unsigned i = 0; i < 5; i++)
    v |= (uint64_t)p[i] << (8 * i);
  unsigned a = (v >> 10) & 1023, b = (v >> 30) & 1023;
  return (uint8_t)((a > b ? a : b) >> 2);
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type,
                           const uint8_t *b, uint16_t n) {
  (void)instance;
  any_reports = true;
  if (type != HID_REPORT_TYPE_OUTPUT && type != HID_REPORT_TYPE_FEATURE)
    return;
  if (!id) {
    if (!n)
      return;
    id = *b++;
    n--;
  }
  if (id == 2 && n >= 22)
    rumble_receive_levels(motor_amp(b + 17), motor_amp(b + 1), now_ms());
}

static void dispatch(const uint8_t *c, uint32_t n) {
  if (n < 8 || (head + 1) % 8 == tail)
    return;
  uint8_t r[128] = {c[0], 1, c[2], c[3], 0, 0xf8, 0, 0}, *d = r + 8;
  uint8_t sub = c[3];
  unsigned dl = 0;
  any_reports = host_commands = true;
  switch (c[0]) {
  case 3:
    if (sub == 3 || sub == 0x0d) {
      d[0] = 1;
      dl = 4;
    } else if (sub == 0x0a) {
      if (n < 9)
        return;
      if (c[8] != 5 && c[8] != 9)
        return;
      report_id = c[8];
      streaming = true;
    }
    break;
  case 7:
    dl = 1;
    break;
  case 0x16:
    dl = 24;
    break;
  case 0x15:
    if (sub == 1) {
      d[0] = 1;
      d[1] = 4;
      d[2] = 1;
      memcpy(d + 3, unit_id, 6);
      dl = 9;
    } else if (sub == 4) {
      if (n < 25)
        return;
      ns2_pairing_derive_ltk(c + 9, public_b1, ltk);
      d[0] = 1;
      memcpy(d + 1, public_b1, 16);
      dl = 17;
    } else if (sub == 2) {
      if (n < 25)
        return;
      d[0] = 1;
      ns2_pairing_challenge(ltk, c + 9, d + 1);
      dl = 17;
    } else if (sub == 3) {
      d[0] = 1;
      dl = 1;
    }
    break;
  case 0x0c:
    if (sub == 1) {
      if (n < 9)
        return;
      uint8_t f = c[8];
      d[4] = (f & 1) ? 7 : 0;
      d[5] = (f & 2) ? 7 : 0;
      d[6] = (f & 4) ? 1 : 0;
      d[7] = (f & 128) ? 1 : 0;
      d[8] = (f & 16) ? 1 : 0;
      d[9] = (f & 32) ? 3 : 0;
      dl = 12;
    } else if (sub == 6) {
      if (n < 13)
        return;
      d[4] = c[12];
      dl = 40;
    } else {
      dl = 4;
      if (sub == 4) {
        if (n < 9)
          return;
        imu_enabled = (c[8] & 4) != 0;
      } else if (sub == 5) {
        if (n < 9)
          return;
        if (c[8] & 4)
          imu_enabled = false;
      } else if (sub == 3)
        imu_enabled = false;
    }
    break;
  case 2: {
    if (n < 16)
      return;
    uint32_t addr = (uint32_t)c[12] | ((uint32_t)c[13] << 8) |
                    ((uint32_t)c[14] << 16) | ((uint32_t)c[15] << 24);
    if (sub == 4 || sub == 1) {
      uint8_t len = sub == 1 ? 64 : c[8];
      if (len > 80)
        len = 80;
      d[0] = len;
      memcpy(d + 4, c + 12, 4);
      ns2_mem_read(sub, addr, len, d + 8);
      dl = 8 + len;
    } else if (sub == 5) {
      memcpy(d + 4, c + 12, 4);
      dl = 8;
    }
    break;
  }
  case 0x10:
    memcpy(d, ns2_firmware_info, 12);
    dl = 12;
    break;
  case 0x0b:
    if (sub == 3) {
      memcpy(d, (uint8_t[]){0xa5, 0x0e, 0, 0}, 4);
      dl = 4;
    } else if (sub == 4) {
      memcpy(d, (uint8_t[]){0x34, 0, 0x83, 0}, 4);
      dl = 4;
    }
    break;
  case 0x11:
    if (sub == 1) {
      d[0] = 3;
      dl = 4;
    } else if (sub == 3) {
      memcpy(d, (uint8_t[]){1,    0xc0, 3,    0,    0,    0xe7, 0xd0, 0x1c,
                            0x3b, 0x79, 0x22, 0xa0, 0x3a, 0x0a, 0xe8, 0x9c,
                            0x42, 0x58, 0xa0, 0x0b, 0x42, 0x0a, 0xe8, 0x9c,
                            0x41, 0x58, 0xa0, 0x0b, 0x41},
             29);
      dl = 29;
    }
    break;
  case 1:
    if (sub == 0x0c) {
      memcpy(d, (uint8_t[]){0x61, 0x12, 0x50, 0x10}, 4);
      dl = 4;
    } else
      r[1] = 4;
    break;
  case 0x18:
    if (sub == 1) {
      memcpy(d, (uint8_t[]){0, 0, 0x40, 0xf0, 0, 0, 0x60, 0}, 8);
      dl = 8;
    } else if (sub == 3) {
      if (n < 9)
        return;
      d[0] = c[8];
      dl = 1;
    }
    break;
  }
  reply_size[head] = (uint16_t)(8 + dl);
  memcpy(replies[head], r, 8 + dl);
  head = (head + 1) % 8;
}
void tud_vendor_rx_cb(uint8_t instance, const uint8_t *buffer, uint16_t size) {
  (void)instance;
  (void)buffer;
  (void)size;
  // Command packets are at most 64 bytes on this endpoint.
  while (tud_vendor_available() && (head + 1) % 8 != tail) {
    uint8_t c[64];
    uint32_t n = tud_vendor_read(c, sizeof(c));
    dispatch(c, n);
  }
}
static void fill_report(uint8_t p[63]) {
  memset(p, 0, 63);
  const uint8_t *s = current_state.bytes;
  if (report_id == 5) {
    uint32_t count = counter++;
    memcpy(p, &count, 4);
    memcpy(p + 4, s, 3);
    if (current_state.gamechat)
      p[5] |= 0x40;
    memcpy(p + 0x0a, s + 3, 6);
    p[0x1f] = 0xa0;
    p[0x20] = 0x0f;
    p[0x21] = 0x20;
    p[0x29] = 1;
    if (imu_enabled && sensor.has_motion) {
      uint32_t us = time_us_32();
      memcpy(p + 0x2a, &us, 4);
      p[0x2e] = 1;
      memcpy(p + 0x30, sensor.accel, 6);
      memcpy(p + 0x36, sensor.gyro, 6);
    }
    return;
  }
  p[0] = counter++;
  p[1] = 0x25; // Wired/full power profile.
  p[2] = ((s[0] & 4) ? 1 : 0) | ((s[0] & 8) ? 2 : 0) | ((s[0] & 1) ? 4 : 0) |
         ((s[0] & 2) ? 8 : 0) | ((s[0] & 64) ? 16 : 0) |
         ((s[0] & 128) ? 32 : 0) | ((s[1] & 2) ? 64 : 0) |
         ((s[1] & 4) ? 128 : 0);
  p[3] = ((s[2] & 1) ? 1 : 0) | ((s[2] & 4) ? 2 : 0) | ((s[2] & 8) ? 4 : 0) |
         ((s[2] & 2) ? 8 : 0) | ((s[2] & 64) ? 16 : 0) |
         ((s[2] & 128) ? 32 : 0) | ((s[1] & 1) ? 64 : 0) |
         ((s[1] & 8) ? 128 : 0);
  p[4] = ((s[1] & 16) ? 1 : 0) | ((s[1] & 32) ? 2 : 0) |
         (current_state.gamechat ? 16 : 0);
  memcpy(p + 5, s + 3, 6);
  p[0x0b] = 0x30;
  if (imu_enabled && sensor.has_motion &&
      ns2_ds5_motion_build(&orientation, p + 0x0f)) {
    p[0x0e] = 30;
    // Wire timing describes emitted samples, including a held attitude on
    // disconnect.
    uint16_t tick = (uint16_t)((time_us_32() / 1250u) & 0x0fffu);
    uint16_t elapsed = motion_clock_valid
                           ? (uint16_t)((tick - previous_motion_tick) & 0x0fffu)
                           : 6u;
    if (elapsed > 15)
      elapsed = 15;
    uint16_t timing = (uint16_t)((elapsed << 12) | tick);
    p[0x0f] = (uint8_t)timing;
    p[0x10] = (uint8_t)(timing >> 8);
    previous_motion_tick = tick;
    motion_clock_valid = true;
  }
}
void switch_usb_task(void) {
  uint32_t now = now_ms();
  if (sensor.has_motion && now - sensor_at >= 100) {
    memset(sensor.gyro, 0, sizeof(sensor.gyro));
    // Hold the last attitude when the Bluetooth sensor stops.
  }
  tud_task();
  if (tail != head) {
    uint32_t n = tud_vendor_write(replies[tail] + reply_offset,
                                  reply_size[tail] - reply_offset);
    reply_offset += (uint16_t)n;
    tud_vendor_write_flush();
    if (reply_offset == reply_size[tail]) {
      tail = (tail + 1) % 8;
      reply_offset = 0;
    }
  }
  if (tud_vendor_available() && (head + 1) % 8 != tail)
    tud_vendor_rx_cb(0, NULL, 0);
  if (streaming && tud_hid_ready() && now - sent_at >= 8) {
    uint8_t p[63];
    fill_report(p);
    if (tud_hid_report(report_id, p, 63))
      sent_at = now;
  }
}
uint8_t const *tud_descriptor_device_cb(void) { return ns2_device_desc; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return ns2_config_desc;
}
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return ns2_report_desc;
}
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t lang) {
  (void)lang;
  static uint16_t out[64];
  if (index == 0) {
    out[1] = 0x0409;
    out[0] = 0x0304;
    return out;
  }
  if (index >= sizeof(ns2_strings) / sizeof(ns2_strings[0]))
    return NULL;
  unsigned n = strlen(ns2_strings[index]);
  if (n > 63)
    n = 63;
  for (unsigned i = 0; i < n; i++)
    out[i + 1] = (uint8_t)ns2_strings[index][i];
  out[0] = (uint16_t)(0x0300 | ((n + 1) * 2));
  return out;
}
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                const tusb_control_request_t *r) {
  if (stage != CONTROL_STAGE_SETUP)
    return true;
  if (r->bmRequestType == 0xc0 && r->bRequest == 3) {
    any_reports = host_commands = true;
    return tud_control_xfer(rhport, r, ns2_ctrl_identity,
                            sizeof(ns2_ctrl_identity));
  }
  if (r->bmRequestType == 0xc0 && r->bRequest == 2) {
    any_reports = host_commands = true;
    return tud_control_xfer(rhport, r, ns2_ctrl_info, sizeof(ns2_ctrl_info));
  }
  if (r->bmRequestType == 0x40 && r->bRequest == 4 && r->wLength == 0)
    return tud_control_status(rhport, r);
  return false;
}
