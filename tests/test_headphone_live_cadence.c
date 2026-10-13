// SPDX-License-Identifier: MIT
// Verify restored low-latency live pacing and nonblocking LED diagnosis.
#define main retained_bridge_test_main
#define time_us_32 retained_real_clock
#include "test_headphone_bridge.c"
#undef main
#undef time_us_32
static uint32_t virtual_us;
uint32_t time_us_32(void) { return virtual_us; }
static void feed_frame(void) {
  uint8_t pcm[192] = {0};
  for (unsigned i = 0; i < 10; i++) headphone_audio_submit_pcm(pcm, 192);
  headphone_audio_submit_pcm(pcm, 128);
  for (unsigned i = 0; i < 20; i++) {
    headphone_audio_task();
    sleep_us(1000);
  }
}
int main(void) {
  headphone_audio_init();
  uni_hid_device_t d = {.controller_type = 1, .conn.interrupt_cid = 42};
  headphone_audio_connect(&d);
  uint8_t input[78] = {0x31};
  input[55] = 1;
  headphone_audio_input(&d, input, sizeof(input));
  for (unsigned n = 0; n < 1000 && !headphone_audio_headset(); n++)
    sleep_us(1000);
  assert(headphone_audio_headset());
  headphone_audio_set_streams(true, false);
  virtual_us = 100000;
  headphone_audio_task();
  virtual_us += 30000;
  headphone_audio_task();
  feed_frame(); feed_frame();
  assert(stream_count == 0);
  feed_frame();
  assert(stream_count == 1); // Three startup frames, not eight.
  feed_frame(); feed_frame();
  assert(stream_count == 2); // Producer-paced even with frozen wall clock.
  bool led = false;
  assert(!headphone_audio_diagnostic_led(1000, &led));
  // Delayed USB delivery must be diagnosed, with USB priority over the
  // consequential late output packet. No extra audio buffering is introduced.
  virtual_us += 60000;
  feed_frame(); feed_frame();
  assert(stream_count == 3);
  assert(headphone_audio_diagnostic_led(1000, &led) && led);
  assert(headphone_audio_diagnostic_led(1150, &led) && !led);
  assert(headphone_audio_diagnostic_led(1400, &led) && !led); // Only one flash.
  assert(headphone_audio_diagnostic_led(4500, &led) && led); // Repeated group.
  assert(!headphone_audio_diagnostic_led(21000, &led)); // Expires after 20 s.
  // A distinct episode blocked at L2CAP must produce three flashes.
  tx_ready = false;
  feed_frame(); feed_frame();
  virtual_us += 31001;
  headphone_audio_task();
  assert(headphone_audio_diagnostic_led(22000, &led) && led);
  assert(headphone_audio_diagnostic_led(22400, &led) && led);
  assert(headphone_audio_diagnostic_led(22800, &led) && led);
  assert(headphone_audio_diagnostic_led(23200, &led) && !led);
  tx_ready = true;
  headphone_audio_can_send_now(42);
  headphone_audio_disconnect(&d);
  assert(!headphone_audio_diagnostic_led(23201, &led));
  puts("Live diagnostic: three-frame low-latency startup, producer pacing, "
       "USB/Bluetooth gap detection, LED priority/group timing/expiry and disconnect passed");
  return 0;
}
