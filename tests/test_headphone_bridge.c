// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "headphone_audio.h"
#include "opus.h"
#include "pico/util/queue.h"
#include "uni_hid_device.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static bool tx_ready = true;
static uint8_t last_stream[548], last_control[143];
static unsigned stream_count, control_count, status_count, lightbar_count;
static uint8_t next_seq;
static unsigned credit_requests;
uint8_t swongle_ds5_next_sequence(uni_hid_device_t *d) {
  (void)d;
  return next_seq++ & 15;
}
bool l2cap_can_send_packet_now(uint16_t c) {
  assert(c == 42);
  return tx_ready;
}
uint8_t l2cap_request_can_send_now_event(uint16_t cid) {
  assert(cid == 42);
  credit_requests++;
  // Exercise synchronous notification and the transport's re-entry guard.
  headphone_audio_can_send_now(cid);
  return 0;
}
int l2cap_send(uint16_t c, uint8_t *p, uint16_t n) {
  assert(c == 42 && p[0] == 0xa2);
  if (p[1] == 0x31) {
    lightbar_count++;
  } else if (n == 548) {
    memcpy(last_stream, p, n);
    stream_count++;
  } else if (p[3] == 0x90) {
    memcpy(last_control, p, n);
    control_count++;
  } else
    status_count++;
  return 0;
}
uint32_t time_us_32(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint32_t)((uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000);
}
void sleep_us(uint64_t n) {
  struct timespec t = {.tv_sec = n / 1000000, .tv_nsec = (n % 1000000) * 1000};
  nanosleep(&t, NULL);
}
bool flash_safe_execute_core_init(void) { return true; }
static void (*entry)(void);
static void *run(void *p) {
  (void)p;
  entry();
  return NULL;
}
void multicore_launch_core1_with_stack(void (*fn)(void), uint32_t *stack,
                                       size_t bytes) {
  (void)stack;
  assert(bytes >= 48 * 1024);
  entry = fn;
  pthread_t th;
  assert(pthread_create(&th, NULL, run, NULL) == 0);
  pthread_detach(th);
}
void queue_init(queue_t *q, size_t bytes, unsigned count) {
  q->size = bytes;
  q->capacity = count;
  q->items = calloc(count, bytes);
  pthread_mutex_init(&q->lock, NULL);
  pthread_cond_init(&q->data, NULL);
}
bool queue_try_add(queue_t *q, const void *v) {
  pthread_mutex_lock(&q->lock);
  bool ok = q->count < q->capacity;
  if (ok) {
    memcpy(q->items + q->head * q->size, v, q->size);
    q->head = (q->head + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->data);
  }
  pthread_mutex_unlock(&q->lock);
  return ok;
}
bool queue_try_remove(queue_t *q, void *v) {
  pthread_mutex_lock(&q->lock);
  bool ok = q->count > 0;
  if (ok) {
    if (v)
      memcpy(v, q->items + q->tail * q->size, q->size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count--;
  }
  pthread_mutex_unlock(&q->lock);
  return ok;
}
void queue_remove_blocking(queue_t *q, void *v) {
  pthread_mutex_lock(&q->lock);
  while (!q->count)
    pthread_cond_wait(&q->data, &q->lock);
  memcpy(v, q->items + q->tail * q->size, q->size);
  q->tail = (q->tail + 1) % q->capacity;
  q->count--;
  pthread_mutex_unlock(&q->lock);
}
static void play(unsigned packets) {
  uint8_t pcm[192];
  for (unsigned i = 0; i < sizeof(pcm); i += 2) {
    pcm[i] = 0xff;
    pcm[i + 1] = 0x1f;
  }
  for (unsigned n = 0; n < packets; n++) {
    headphone_audio_submit_pcm(pcm, sizeof(pcm));
    headphone_audio_task();
    sleep_us(1000);
  }
}
int main(void) {
  headphone_audio_init();
  uni_hid_device_t d = {.controller_type = 1, .conn.interrupt_cid = 42};
  headphone_audio_connect(&d);
  uint8_t input[78] = {0x31};
  input[55] = 3;
  assert(headphone_audio_input(&d, input, 78));
  for (unsigned n = 0; n < 1000 && !headphone_audio_headset(); n++)
    sleep_us(1000);
  assert(headphone_audio_headset() == 1);
  headphone_audio_set_streams(true, false);
  play(240);
  assert(control_count >= 1 && status_count == 1 && stream_count >= 4);
  uint8_t lightbar[79] = {0xa2, 0x31};
  assert(headphone_audio_owns_lightbar(&d));
  assert(headphone_audio_lightbar_ready(&d));
  assert(headphone_audio_queue_lightbar(&d, lightbar, sizeof(lightbar)));
  assert(lightbar_count == 1);
  tx_ready = false;
  assert(!headphone_audio_lightbar_ready(&d));
  assert(!headphone_audio_queue_lightbar(&d, lightbar, sizeof(lightbar)));
  assert(lightbar_count == 1);
  tx_ready = true;
  // The actual encoder output must decode as two valid stereo Opus frames.
  int err;
  OpusDecoder *decoder = opus_decoder_create(48000, 2, &err);
  assert(err == OPUS_OK);
  int16_t decoded[960];
  assert(last_stream[141] == 0xd6 && last_stream[142] == 200);
  assert(opus_decode(decoder, last_stream + 143, 200, decoded, 480, 0) == 480);
  assert(opus_decode(decoder, last_stream + 343, 200, decoded, 480, 0) == 480);
  opus_decoder_destroy(decoder);
  assert(headphone_audio_rumble(&d, 50, 100));
  play(25);
  bool haptics = false;
  for (unsigned i = 13; i < 141; i++)
    haptics |= last_stream[i] != 0;
  assert(haptics);
  headphone_audio_set_control(true, -20 * 256);
  play(10);
  assert(control_count >= 2);
  // Dropped audio reports must not be mistaken for new button input.
  input[1] = 2;
  assert(!headphone_audio_input(&d, input, 78));
  input[1] = 0;
  // Backpressure never blocks core0, even when codec/output queues fill.
  tx_ready = false;
  play(250);
  assert(credit_requests > 0);
  unsigned queued_before = stream_count;
  tx_ready = true;
  headphone_audio_can_send_now(41); // Other channels must not drain audio.
  assert(stream_count == queued_before);
  headphone_audio_can_send_now(42); // No task/poll/PCM submission required.
  assert(stream_count == queued_before + 2);
  unsigned before = stream_count;
  play(100);
  assert(stream_count > before);
  // Physical removal and USB stream stop flush old sound and restore legacy
  // rumble.
  tx_ready = false;
  play(60); // Leave audio queued when the headset is removed.
  input[55] = 0;
  assert(headphone_audio_input(&d, input, 78));
  assert(!headphone_audio_headset());
  assert(!headphone_audio_rumble(&d, 50, 100));
  before = stream_count;
  tx_ready = true;
  headphone_audio_can_send_now(42);
  play(40);
  assert(stream_count == before);
  input[55] = 1;
  headphone_audio_input(&d, input, 78);
  play(160);
  assert(stream_count > before);
  headphone_audio_set_streams(false, false);
  before = stream_count;
  play(40);
  assert(stream_count == before);
  // Return to no physical jack, then toggle console -> internal speaker.
  input[55] = 0;
  headphone_audio_input(&d, input, 78);
  assert(!headphone_audio_headset());
  assert(!headphone_audio_output_buttons(&d, true, false));
  assert(headphone_audio_output_buttons(&d, true, true));
  assert(headphone_audio_headset());
  assert(headphone_audio_output_buttons(&d, true, true)); // Held: no retoggle.
  assert(headphone_audio_output_buttons(&d, true, false)); // Consume release.
  assert(headphone_audio_output_buttons(&d, false, false));
  assert(!headphone_audio_output_buttons(&d, false, false));
  headphone_audio_set_streams(true, false);
  play(160);
  assert(last_control[12] == 0x30 && last_stream[141] == 0xd3);
  // Physical headphones override the selected internal speaker.
  input[55] = 1;
  headphone_audio_input(&d, input, 78);
  play(160);
  assert(last_control[12] == 0x02 && last_stream[141] == 0xd6);
  assert(headphone_audio_output_buttons(&d, true, true));
  headphone_audio_output_buttons(&d, false, false);
  play(40);
  assert(last_stream[141] == 0xd6);
  input[55] = 0;
  headphone_audio_input(&d, input, 78);
  play(160);
  assert(last_control[12] == 0x30 && last_stream[141] == 0xd3);
  // Switch back to normal console output and restore ordinary rumble.
  assert(headphone_audio_output_buttons(&d, true, true));
  headphone_audio_output_buttons(&d, false, false);
  assert(!headphone_audio_headset());
  assert(!headphone_audio_rumble(&d, 50, 100));
  before = stream_count;
  play(40);
  assert(stream_count == before);
  headphone_audio_disconnect(&d);
  assert(!headphone_audio_headset());
  puts("Headphone bridge: real CELT/Opus decode, routing, rumble, "
       "backpressure, reinsert and disconnect passed");
  return 0;
}
