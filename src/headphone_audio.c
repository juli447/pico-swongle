// SPDX-License-Identifier: MIT
// Bridge integration for Bluepad32. Packet/resampler references are Apache-2.0.
// CELT framing/configuration follows PicoSwitch2 v2.5.0 and DS5Dongle.
#include "headphone_audio.h"
#include "celt.h"
#include "ds5_audio_packet.h"
#include "ds5_audio_resample.h"
#include "opus.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "uni_hid_device.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t generation;
  int16_t samples[1024];
} pcm_block_t;
typedef struct {
  uint32_t generation;
  uint8_t data[200];
} encoded_frame_t;
static queue_t pcm_queue, frame_queue;
static pcm_block_t producer;
static unsigned producer_samples;
static uint32_t generation;
static volatile bool worker_registered, codec_ready;
static bool speaker_active, muted;
static bool controller_speaker_selected, output_chord_consumed;
static uint8_t volume = 100, headset, weak_level, strong_level;
static uni_hid_device_t *device;
static bool headphone_path_latched, status_sent, control_known;
static uint8_t previous_volume, control_stage, packet_counter;
static bool previous_muted;
static uint32_t activation_at, rumble_at;
static uint32_t __attribute__((aligned(8))) codec_stack[48 * 1024 / 4];
static encoded_frame_t pending[4];
static bool stream_started;
static uint32_t packet_deadline;
static uint8_t packet_fraction;
static unsigned pending_count;
// Hold current + next HID packet until HCI credits arrive, as in PicoSwitch2.
// This queue is core-0 only; callbacks may run synchronously when requesting
// CAN_SEND_NOW, so transport draining must also tolerate re-entry.
typedef struct { uint16_t len; uint8_t data[548]; } transport_packet_t;
static transport_packet_t transport[2];
static unsigned transport_head, transport_count;
static bool transport_busy;
extern uint8_t swongle_ds5_next_sequence(uni_hid_device_t *d);
// Observe stages without flash writes, printf, USB diagnostics or blocking.
// Codes mark where delay was observed, not a proven physical root cause.
static uint32_t diagnostic_flags;
static uint32_t last_pcm_us, last_stream_us, blocked_since_us;
static bool pcm_seen, stream_seen, blocked;
static void note_audio_delay(unsigned code) {
#ifdef SWONGLE_AUDIO_DIAGNOSTICS
  __atomic_fetch_or(&diagnostic_flags, 1u << code, __ATOMIC_RELAXED);
#else
  (void)code;
#endif
}
bool headphone_audio_diagnostic_led(uint32_t now_ms, bool *led) {
#ifndef SWONGLE_AUDIO_DIAGNOSTICS
  (void)now_ms; (void)led;
  return false;
#else
  static unsigned displayed;
  static uint32_t shown_at;
  uint32_t flags = __atomic_load_n(&diagnostic_flags, __ATOMIC_RELAXED);
  if (!flags) { displayed = 0; return false; }
  unsigned code = 1;
  while (code < 5 && !(flags & (1u << code))) code++;
  if (code != displayed) { displayed = code; shown_at = now_ms; }
  uint32_t age = now_ms - shown_at;
  if (age >= 20000) {
    __atomic_store_n(&diagnostic_flags, 0, __ATOMIC_RELAXED);
    displayed = 0;
    return false;
  }
  // Groups of 1..5 short ON flashes separated by a clear dark interval.
  uint32_t phase = age % 3500u;
  *led = phase < code * 400u && phase % 400u < 150u;
  return true;
#endif
}

static void reset_pipeline(void) {
  __atomic_add_fetch(&generation, 1, __ATOMIC_RELEASE);
  producer_samples = pending_count = 0;
  pcm_seen = stream_seen = blocked = false;
  transport_head = transport_count = 0;
  control_known = status_sent = false;
  control_stage = 0;
  stream_started = false;
  packet_deadline = 0;
  packet_fraction = 0;
  while (queue_try_remove(&pcm_queue, NULL)) {
  }
  while (queue_try_remove(&frame_queue, NULL)) {
  }
}
#ifdef SWONGLE_AUDIO_TEST_TONE
extern const uint8_t swongle_audio_tone[2][200];
static void __not_in_flash_func(codec_worker)(void) {
  flash_safe_execute_core_init();
  __atomic_store_n(&worker_registered, true, __ATOMIC_RELEASE);
  __atomic_store_n(&codec_ready, true, __ATOMIC_RELEASE);
  for (;;)
    sleep_us(1000);
}
#else
static void __not_in_flash_func(codec_worker)(void) {
  flash_safe_execute_core_init();
  __atomic_store_n(&worker_registered, true, __ATOMIC_RELEASE);
  int bytes = celt_encoder_get_size(2);
  CELTEncoder *encoder = bytes > 0 ? malloc((size_t)bytes) : NULL;
  if (!encoder)
    return;
  int result = celt_encoder_init(encoder, 48000, 2, 0);
  result |= celt_encoder_ctl(encoder, CELT_SET_SIGNALLING(0));
  result |= celt_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(0));
  result |= celt_encoder_ctl(encoder, OPUS_SET_VBR(0));
  result |= celt_encoder_ctl(encoder, OPUS_SET_BITRATE(160000));
  if (result != OPUS_OK) {
    free(encoder);
    return;
  }
  __atomic_store_n(&codec_ready, true, __ATOMIC_RELEASE);
  static pcm_block_t block;
  static int16_t resampled[960];
  static float floats[960];
  static encoded_frame_t frame;
  uint32_t current = UINT32_MAX;
  for (;;) {
    queue_remove_blocking(&pcm_queue, &block);
    uint32_t requested = __atomic_load_n(&generation, __ATOMIC_ACQUIRE);
    if (block.generation != requested)
      continue;
    if (current != requested) {
      celt_encoder_ctl(encoder, OPUS_RESET_STATE);
      current = requested;
    }
    uint32_t encode_start = time_us_32();
    ds5_audio_resample_512_to_480_stereo(block.samples, resampled);
    for (unsigned i = 0; i < 960; i++)
      floats[i] = (float)resampled[i] * (1.0f / 32768.0f);
    frame.data[0] = 0xf4;
    int n =
        celt_encode_with_ec(encoder, floats, 480, frame.data + 1, 199, NULL);
    if (n != 199 || requested != __atomic_load_n(&generation, __ATOMIC_ACQUIRE))
      continue;
    if (time_us_32() - encode_start > 21333u)
      note_audio_delay(2);
    frame.generation = requested;
    // Prefer fresh encoded audio on congestion; never stall the PCM consumer.
    if (!queue_try_add(&frame_queue, &frame)) {
      note_audio_delay(5);
      encoded_frame_t discarded;
      queue_try_remove(&frame_queue, &discarded);
      queue_try_add(&frame_queue, &frame);
    }
  }
}
#endif
void headphone_audio_init(void) {
  queue_init(&pcm_queue, sizeof(pcm_block_t), 8);
  queue_init(&frame_queue, sizeof(encoded_frame_t), 8);
  flash_safe_execute_core_init();
  multicore_launch_core1_with_stack(codec_worker, codec_stack,
                                    sizeof(codec_stack));
  while (!__atomic_load_n(&worker_registered, __ATOMIC_ACQUIRE))
    sleep_us(100);
}
void headphone_audio_connect(uni_hid_device_t *d) {
  if (d->controller_type != CONTROLLER_TYPE_PS5Controller)
    return;
  device = d;
  headset = 0;
  __atomic_store_n(&diagnostic_flags, 0, __ATOMIC_RELAXED);
  controller_speaker_selected = output_chord_consumed = false;
  weak_level = strong_level = 0;
  headphone_path_latched = false;
  control_known = status_sent = false;
  control_stage = packet_counter = 0;
  reset_pipeline();
}
void headphone_audio_disconnect(uni_hid_device_t *d) {
  if (device != d)
    return;
  device = NULL;
  headset = 0;
  __atomic_store_n(&diagnostic_flags, 0, __ATOMIC_RELAXED);
  controller_speaker_selected = output_chord_consumed = false;
  weak_level = strong_level = 0;
  reset_pipeline();
}
bool headphone_audio_input(uni_hid_device_t *d, const uint8_t *r,
                           uint16_t len) {
  if (ds5_audio_is_mic_input_report(r, len))
    return false;
  if (len != 78 || r[0] != 0x31)
    return false;
  if (device == d) {
    uint8_t state = ds5_audio_headset_state(r, len);
    if (state != headset) {
      headset = state;
      reset_pipeline();
    }
  }
  return true;
}
bool headphone_audio_output_buttons(uni_hid_device_t *d, bool touchpad,
                                    bool mute_button) {
  if (d != device)
    return false;
  if (touchpad && mute_button && !output_chord_consumed) {
    output_chord_consumed = true;
    // A physical headphone jack always takes priority. Leave the selected
    // no-headphone output unchanged when the chord is pressed with a jack in.
    if (!headset) {
      controller_speaker_selected = !controller_speaker_selected;
      reset_pipeline();
    }
  }
  bool consumed = output_chord_consumed;
  if (!touchpad && !mute_button)
    output_chord_consumed = false;
  return consumed;
}
uint8_t headphone_audio_headset(void) {
  // Request the console's USB audio for either physical headphones or the
  // selected controller speaker. With neither, the console uses its own output.
  // The upstream microphone remains silent.
  return device && (headset || controller_speaker_selected) &&
         __atomic_load_n(&codec_ready, __ATOMIC_ACQUIRE)
             ? CONTROLLER_HEADSET_HEADPHONES
             : 0;
}
void headphone_audio_set_streams(bool speaker, bool mic) {
  (void)mic;
#ifdef SWONGLE_AUDIO_TEST_TONE
  // Diagnostic playback must not follow host ISO alternate-setting changes.
  (void)speaker;
  return;
#endif
  if (speaker_active != speaker) {
    speaker_active = speaker;
    reset_pipeline();
  }
}
void headphone_audio_set_control(bool value, int16_t db) {
  muted = value;
  int v = 100 + db / 256;
  if (v < 40)
    v = 40;
  if (v > 100)
    v = 100;
  volume = (uint8_t)v;
}
void headphone_audio_submit_pcm(const uint8_t *data, uint16_t len) {
#ifdef SWONGLE_AUDIO_TEST_TONE
  (void)data;
  (void)len;
  return;
#endif
  if (!speaker_active || !headphone_audio_headset() || !data)
    return;
  // PCM packets must contain complete interleaved stereo s16 samples.
  if (len % 4)
    return;
  uint32_t pcm_now = time_us_32();
  if (stream_started && pcm_seen && pcm_now - last_pcm_us > 24000u)
    note_audio_delay(1);
  last_pcm_us = pcm_now;
  pcm_seen = true;
  for (unsigned i = 0; i < len; i += 2) {
    producer.samples[producer_samples++] =
        (int16_t)((uint16_t)data[i] | ((uint16_t)data[i + 1] << 8));
    if (producer_samples == 1024) {
      producer.generation = __atomic_load_n(&generation, __ATOMIC_ACQUIRE);
      if (!queue_try_add(&pcm_queue, &producer)) {
        note_audio_delay(5);
        // A missed PCM block must not restart the whole codec/session.
        queue_try_remove(&pcm_queue, NULL);
        queue_try_add(&pcm_queue, &producer);
      }
      producer_samples = 0;
    }
  }
}
bool headphone_audio_rumble(uni_hid_device_t *d, uint8_t weak, uint8_t strong) {
  if (device != d || !headphone_audio_headset())
    return false;
#ifndef SWONGLE_AUDIO_TEST_TONE
  if (!speaker_active)
    return false;
#endif
  weak_level = weak;
  strong_level = strong;
  rumble_at = time_us_32();
  return true;
}
void headphone_audio_can_send_now(uint16_t cid) {
  if (!device || device->conn.interrupt_cid != cid || transport_busy)
    return;
  transport_busy = true;
  while (transport_count) {
    transport_packet_t *entry = &transport[transport_head];
    if (!l2cap_can_send_packet_now(cid) ||
        l2cap_send(cid, entry->data, entry->len) != ERROR_CODE_SUCCESS) {
      uint32_t now = time_us_32();
      if (!blocked) { blocked = true; blocked_since_us = now; }
      if (now - blocked_since_us > 30000u) note_audio_delay(3);
      // BTstack can invoke the callback inline; the guard prevents recursion.
      l2cap_request_can_send_now_event(cid);
      break;
    }
    uint32_t now = time_us_32();
    if (blocked && now - blocked_since_us > 30000u) note_audio_delay(3);
    blocked = false;
    if (entry->data[1] == 0x39) {
      if (stream_seen && now - last_stream_us > 45000u) note_audio_delay(4);
      last_stream_us = now;
      stream_seen = true;
    }
    transport_head = (transport_head + 1) % 2;
    transport_count--;
  }
  transport_busy = false;
}
static bool send_packet(const uint8_t *p, unsigned len) {
  if (!device || transport_count == 2 || len > sizeof(transport[0].data))
    return false;
  transport_packet_t *entry = &transport[(transport_head + transport_count) % 2];
  memcpy(entry->data, p, len);
  entry->len = (uint16_t)len;
  transport_count++;
  headphone_audio_can_send_now(device->conn.interrupt_cid);
  return true;
}
bool headphone_audio_owns_lightbar(uni_hid_device_t *d) {
  if (device != d || !headphone_audio_headset()) return false;
#ifdef SWONGLE_AUDIO_TEST_TONE
  return true;
#else
  return speaker_active;
#endif
}
bool headphone_audio_lightbar_ready(uni_hid_device_t *d) {
  return device != d ||
         (!transport_count && l2cap_can_send_packet_now(d->conn.interrupt_cid));
}
bool headphone_audio_queue_lightbar(uni_hid_device_t *d, const uint8_t *p, uint16_t len) {
  if (!headphone_audio_owns_lightbar(d) || !headphone_audio_lightbar_ready(d))
    return false;
  return send_packet(p, len);
}
void headphone_audio_task(void) {
  if (!device || !headphone_audio_headset())
    return;
#ifndef SWONGLE_AUDIO_TEST_TONE
  if (!speaker_active)
    return;
#endif
  headphone_audio_can_send_now(device->conn.interrupt_cid);
  if (transport_count == 2)
    return;
  uint32_t now = time_us_32();
  if (!control_known || previous_muted != muted || previous_volume != volume ||
      !headphone_path_latched) {
    uint8_t packet[143];
    packet[0] = 0xa2;
    ds5_audio_build_control_report(swongle_ds5_next_sequence(device), headset != 0,
                                   muted, volume, packet + 1);
    if (send_packet(packet, sizeof(packet))) {
      control_known = headphone_path_latched = true;
      previous_muted = muted;
      previous_volume = volume;
      if (control_stage == 0) {
        control_stage = 1;
        activation_at = now;
      }
    }
    return;
  }
  if (control_stage == 1) {
    if (now - activation_at < 30000)
      return;
    control_stage = 2;
  }
  if (!status_sent) {
    uint8_t packet[143];
    packet[0] = 0xa2;
    ds5_audio_build_mic_status_report(swongle_ds5_next_sequence(device), false,
                                      packet + 1);
    if (send_packet(packet, sizeof(packet)))
      status_sent = true;
    return;
  }
#ifdef SWONGLE_AUDIO_TEST_TONE
  if (stream_started && (int32_t)(now - packet_deadline) < 0)
    return;
  if (stream_started && now - packet_deadline > 50000) {
    pending_count = 0;
    stream_started = false;
  }
  while (pending_count < 4) {
    memcpy(pending[pending_count].data, swongle_audio_tone[pending_count & 1],
           200);
    pending_count++;
  }
#else
  // Restore Test 9's producer-paced path and one-frame startup cushion.
  while (pending_count < 3) {
    encoded_frame_t frame;
    if (!queue_try_remove(&frame_queue, &frame))
      break;
    if (frame.generation != __atomic_load_n(&generation, __ATOMIC_ACQUIRE))
      continue;
    pending[pending_count++] = frame;
  }
#endif
  if (pending_count < 2)
    return;
#ifndef SWONGLE_AUDIO_TEST_TONE
  if (!stream_started && pending_count < 3)
    return;
  stream_started = true;
#endif
#ifdef SWONGLE_AUDIO_TEST_TONE
  if (!stream_started) {
    stream_started = true;
    packet_deadline = now;
    packet_fraction = 0;
  }
#endif
  static uint8_t packet[548];
  packet[0] = 0xa2;
  if (now - rumble_at >= 300000)
    weak_level = strong_level = 0;
  ds5_audio_build_stream_report(swongle_ds5_next_sequence(device),
                                (uint8_t)(packet_counter + 2), false, headset != 0,
                                strong_level, weak_level, 64, pending[0].data,
                                pending[1].data, packet + 1);
  if (send_packet(packet, sizeof(packet))) {
    packet_counter += 2;
    pending_count -= 2;
    for (unsigned i = 0; i < pending_count; i++)
      pending[i] = pending[i + 2];
#ifdef SWONGLE_AUDIO_TEST_TONE
    // The controller consumes nominal 48 kHz Opus at its 45 kHz DAC clock.
    packet_deadline += 21333u;
    if (++packet_fraction == 3) {
      packet_deadline++;
      packet_fraction = 0;
    }
    if ((int32_t)(now - packet_deadline) > 21333)
      packet_deadline = now + 21333u;
#endif
  }
}
