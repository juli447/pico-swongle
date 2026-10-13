// SPDX-License-Identifier: MIT
#include "device/usbd_pvt.h"
#include "headphone_audio.h"
#include "tusb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool speaker;
static bool mute;
static int16_t volume;
static uint8_t *ctrl_data, *speaker_packet;
static uint16_t ctrl_len;
static uint16_t expected_pcm_size = 192;
static unsigned expected_rearmed_transfers = 2;
static unsigned allocs, activations, transfers, pcm_count, io_ticks;
void headphone_audio_service_io(void) { io_ticks++; }
void headphone_audio_set_streams(bool s, bool mic) {
  (void)mic;
  speaker = s;
}
void headphone_audio_set_control(bool m, int16_t v) {
  mute = m;
  volume = v;
}
void headphone_audio_submit_pcm(const uint8_t *p, uint16_t n) {
  assert(p != speaker_packet && n == expected_pcm_size);
  assert(transfers == expected_rearmed_transfers);
  for (unsigned i = 0; i < n; i++) assert(p[i] == 0x5a);
  assert(speaker_packet[0] == 0xcc); // Rearm overwrote destination already.
  pcm_count++;
}
bool usbd_edpt_iso_alloc(uint8_t port, uint8_t ep, uint16_t n) {
  (void)port;
  assert((ep == 3 || ep == 0x83) && n == 192);
  allocs++;
  return true;
}
bool usbd_edpt_iso_activate(uint8_t port, const tusb_desc_endpoint_t *ep) {
  (void)port;
  assert(ep->wMaxPacketSize == 192);
  activations++;
  return true;
}
bool usbd_edpt_xfer(uint8_t port, uint8_t ep, uint8_t *p, uint16_t n) {
  (void)port;
  assert(n == (ep == 3 ? 1536 : 192));
  if (ep == 3) {
    speaker_packet = p;
    memset(p, 0xcc, n); // Model an IRQ overwriting the rearmed receive buffer.
  }
  transfers++;
  return true;
}
bool tud_control_status(uint8_t port, const tusb_control_request_t *r) {
  (void)port;
  (void)r;
  return true;
}
bool tud_control_xfer(uint8_t port, const tusb_control_request_t *r, void *p,
                      uint16_t n) {
  (void)port;
  assert(n <= r->wLength);
  ctrl_data = p;
  ctrl_len = n;
  return true;
}
int main(void) {
  uint8_t count;
  const usbd_class_driver_t *d = usbd_app_driver_get_cb(&count);
  assert(count == 1);
  d->init();
  const uint8_t minimal[] = {9, 4, 2, 0, 0, 1, 1, 0, 0, 9, 4, 3, 0, 0,
                             1, 2, 0, 0, 9, 4, 3, 1, 1, 1, 2, 0, 0};
  assert(d->open(0, (const tusb_desc_interface_t *)minimal, sizeof(minimal)) ==
         sizeof(minimal));
  assert(allocs == 2);
  tusb_control_request_t r = {.bmRequestType = 1,
                              .bRequest = TUSB_REQ_SET_INTERFACE,
                              .wIndex = 3,
                              .wValue = 1};
  assert(d->control_xfer_cb(0, CONTROL_STAGE_SETUP, &r));
  assert(speaker && activations == 1 && transfers == 1);
  memset(speaker_packet, 0x5a, 192);
  assert(d->xfer_cb(0, 3, XFER_RESULT_SUCCESS, 192));
  assert(pcm_count == 1 && transfers == 2 && io_ticks == 1);
  // Isochronous errors must rearm without supplying invalid audio.
  assert(d->xfer_cb(0, 3, XFER_RESULT_FAILED, 0));
  assert(pcm_count == 1 && transfers == 3 && io_ticks == 2);
  r.wValue = 0;
  assert(d->control_xfer_cb(0, CONTROL_STAGE_SETUP, &r));
  assert(!speaker);
  r.wValue = 1;
  assert(d->control_xfer_cb(0, CONTROL_STAGE_SETUP, &r));
  assert(speaker && activations == 1 && transfers == 3);
  r.wValue = 2;
  assert(!d->control_xfer_cb(0, CONTROL_STAGE_SETUP, &r));
  r = (tusb_control_request_t){.bmRequestType = 0x21,
                               .bRequest = 1,
                               .wIndex = 0x0202,
                               .wValue = 0x0100,
                               .wLength = 1};
  assert(d->control_xfer_cb(0, CONTROL_STAGE_SETUP, &r));
  assert(ctrl_len == 1);
  *ctrl_data = 1;
  assert(d->control_xfer_cb(0, CONTROL_STAGE_DATA, &r));
  assert(mute);
  r.wValue = 0x0200;
  r.wLength = 2;
  assert(d->control_xfer_cb(0, CONTROL_STAGE_SETUP, &r));
  ctrl_data[0] = 0;
  ctrl_data[1] = 0xec;
  assert(d->control_xfer_cb(0, CONTROL_STAGE_DATA, &r));
  assert(volume == -5120);
  r.wLength = 1;
  assert(!d->control_xfer_cb(0, CONTROL_STAGE_SETUP, &r));
  headphone_usb_suspend(true);
  assert(!speaker);
  headphone_usb_suspend(false);
  assert(speaker);
  expected_pcm_size = 1536;
  expected_rearmed_transfers = transfers + 1;
  memset(speaker_packet, 0x5a, 1536);
  assert(d->xfer_cb(0, 3, XFER_RESULT_SUCCESS, 1536));
  assert(pcm_count == 2);
  // Oversized/malformed completion lengths never read outside the buffer.
  assert(d->xfer_cb(0, 3, XFER_RESULT_SUCCESS, 1537));
  assert(pcm_count == 2);
  d->reset(0);
  assert(!speaker && !mute && volume == 0);
  puts("UAC1 driver: stream selection, persistent endpoints, PCM, errors, "
       "eight-packet batches, rearm-before-PCM, volume/mute, suspend and reset passed");
}
