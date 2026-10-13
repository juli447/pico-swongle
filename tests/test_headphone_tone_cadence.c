// SPDX-License-Identifier: MIT
// Reuse the transport/core/queue mocks, but drive the firmware's actual clock
// deterministically. No real-time sleeping is used for packet cadence checks.
#define main retained_bridge_test_main
#define time_us_32 retained_real_clock
#include "test_headphone_bridge.c"
#undef main
#undef time_us_32
static uint32_t virtual_us;
uint32_t time_us_32(void) { return virtual_us; }
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
  assert(headphone_audio_rumble(&d, 50, 100));
  virtual_us = 100000;
  headphone_audio_task(); // Route activation.
  virtual_us += 30000;
  headphone_audio_task(); // Disable microphone.
  headphone_audio_task(); // First tone pair, even without USB streaming.
  assert(stream_count == 1);
  unsigned count = stream_count;
  uint32_t start = virtual_us;
  for (unsigned i = 1; i <= 3000; i++) {
    unsigned interval = i % 3 == 0 ? 21334 : 21333;
    virtual_us += interval - 1;
    headphone_audio_task();
    assert(stream_count == count);
    virtual_us++;
    headphone_audio_task();
    assert(stream_count == ++count);
    if (i == 100) {
      headphone_audio_set_streams(true, false);
      headphone_audio_set_streams(false, false);
    }
  }
  assert(virtual_us - start == 64000000);
  input[55] = 0;
  headphone_audio_input(&d, input, sizeof(input));
  virtual_us += 100000;
  headphone_audio_task();
  assert(stream_count == count);
  puts("Diagnostic tone: 3000 exact 64/3-ms intervals, no cumulative drift, "
       "USB stream independence and headset removal passed");
  return 0;
}
