// SPDX-License-Identifier: Apache-2.0
// UAC1 driver adapted from PicoSwitch2 v2.5.0; see
// third_party/picoswitch2/NOTICE.
#include "class/audio/audio.h"
#include "device/usbd_pvt.h"
#include "headphone_audio.h"
#include "tusb.h"
#include <string.h>
#define NS2_AUDIO_SPEAKER_ITF 0x03
#define NS2_AUDIO_MIC_ITF 0x04
#define NS2_AUDIO_SPEAKER_EP 0x03
#define NS2_AUDIO_MIC_EP 0x83
#define NS2_AUDIO_PACKET_SIZE 192u
// Keep the hardware receiving across eight SOFs before foreground completion.
#define NS2_AUDIO_SPEAKER_TRANSFER_SIZE (8u * NS2_AUDIO_PACKET_SIZE)

// Standard endpoint descriptors copied byte-for-byte from ns2_config_desc.
// Keeping dedicated copies avoids depending on fragile offsets into the full
// configuration descriptor when an alternate setting is selected.
static const uint8_t ns2_audio_speaker_ep_desc[] = {
    0x07,
    TUSB_DESC_ENDPOINT,
    NS2_AUDIO_SPEAKER_EP,
    0x0D,
    (NS2_AUDIO_PACKET_SIZE & 0xFF),
    (NS2_AUDIO_PACKET_SIZE >> 8),
    0x01,
};
static const uint8_t ns2_audio_mic_ep_desc[] = {
    0x07,
    TUSB_DESC_ENDPOINT,
    NS2_AUDIO_MIC_EP,
    0x0D,
    (NS2_AUDIO_PACKET_SIZE & 0xFF),
    (NS2_AUDIO_PACKET_SIZE >> 8),
    0x01,
};

CFG_TUD_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t
    ns2_audio_speaker_packet[NS2_AUDIO_SPEAKER_TRANSFER_SIZE];
// Preserve a completed batch before rearming the same DMA/IRQ destination.
static uint8_t ns2_audio_speaker_completed[NS2_AUDIO_SPEAKER_TRANSFER_SIZE];
CFG_TUD_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t
    ns2_audio_mic_silence[NS2_AUDIO_PACKET_SIZE];

static uint8_t ns2_audio_alt_speaker;
static uint8_t ns2_audio_alt_mic;
// RP2040/RP2350 ISO allocation persists for the whole USB configuration.
// Track hardware activation and a pending transfer separately from the host's
// current alternate setting so alt 1 -> 0 -> 1 does not restart an already
// active endpoint and corrupt unrelated HID endpoint progress.
static bool ns2_audio_speaker_activated;
static bool ns2_audio_mic_activated;
static bool ns2_audio_speaker_armed;
static bool ns2_audio_mic_armed;
static uint8_t ns2_audio_control_data[2];

// UAC1 volume is signed 1/256 dB. These conservative controls expose -60 dB
// through 0 dB in 1 dB steps, matching what Windows' mixer expects to query.
#define NS2_AUDIO_VOLUME_MIN ((int16_t)-15360)
#define NS2_AUDIO_VOLUME_MAX ((int16_t)0)
#define NS2_AUDIO_VOLUME_RES ((int16_t)256)

typedef struct {
  uint8_t mute;
  int16_t volume;
} ns2_audio_feature_state_t;

static ns2_audio_feature_state_t ns2_audio_speaker_feature;
static ns2_audio_feature_state_t ns2_audio_mic_feature;

static ns2_audio_feature_state_t *ns2_audio_feature(uint8_t unit_id) {
  if (unit_id == 0x02)
    return &ns2_audio_speaker_feature;
  if (unit_id == 0x05)
    return &ns2_audio_mic_feature;
  return NULL;
}

static uint16_t ns2_audio_open(uint8_t rhport,
                               tusb_desc_interface_t const *itf_desc,
                               uint16_t max_len) {
  if (itf_desc->bInterfaceClass != TUSB_CLASS_AUDIO)
    return 0; // not ours
  // Claim the whole audio function: consume descriptors until the next
  // interface of a different class (or the end of this config descriptor).
  uint8_t const *p = (uint8_t const *)itf_desc;
  uint16_t consumed = 0;
  while (consumed < max_len) {
    if (tu_desc_type(p) == TUSB_DESC_INTERFACE && consumed > 0 &&
        ((tusb_desc_interface_t const *)p)->bInterfaceClass != TUSB_CLASS_AUDIO)
      break;
    uint8_t l = tu_desc_len(p);
    if (l == 0)
      break;
    consumed += l;
    p += l;
  }
#ifdef TUP_DCD_EDPT_ISO_ALLOC
  // RP2040/RP2350 cannot open an isochronous endpoint through the ordinary
  // dcd_edpt_open path. Reserve DPRAM once while the configuration is opened,
  // then activate the relevant endpoint when its interface selects alt 1.
  if (!usbd_edpt_iso_alloc(rhport, NS2_AUDIO_SPEAKER_EP, NS2_AUDIO_PACKET_SIZE))
    return 0;
  if (!usbd_edpt_iso_alloc(rhport, NS2_AUDIO_MIC_EP, NS2_AUDIO_PACKET_SIZE))
    return 0;
#else
  (void)rhport;
#endif
  return consumed;
}

// UAC1 (not UAC2) request codes — this descriptor's AC header is bcdADC 0x0100,
// so GET/SET use separate opcodes (unlike UAC2, where tinyusb's audio.h
// AUDIO_CS_REQ_* enum applies instead). Table A-9, USB Audio Class 1.0 spec.
#define UAC1_REQ_SET_CUR 0x01
#define UAC1_REQ_GET_CUR 0x81
#define UAC1_REQ_GET_MIN 0x82
#define UAC1_REQ_GET_MAX 0x83
#define UAC1_REQ_GET_RES 0x84

// Feature Unit IDs from ns2_config_desc's AC descriptors above: 0x02 = speaker
// path (2-channel, feeds IF3 Audio Streaming OUT), 0x05 = mic path (mono, feeds
// IF4 Audio Streaming IN).
static bool ns2_audio_set_alt(uint8_t rhport, uint8_t itf, uint8_t alt) {
  if (alt > 1)
    return false;

  uint8_t *current_alt;
  uint8_t ep_addr;
  tusb_desc_endpoint_t const *ep_desc;
  uint8_t *packet;
  bool *activated;
  bool *armed;

  if (itf == NS2_AUDIO_SPEAKER_ITF) {
    current_alt = &ns2_audio_alt_speaker;
    ep_addr = NS2_AUDIO_SPEAKER_EP;
    ep_desc = (tusb_desc_endpoint_t const *)ns2_audio_speaker_ep_desc;
    packet = ns2_audio_speaker_packet;
    activated = &ns2_audio_speaker_activated;
    armed = &ns2_audio_speaker_armed;
  } else if (itf == NS2_AUDIO_MIC_ITF) {
    current_alt = &ns2_audio_alt_mic;
    ep_addr = NS2_AUDIO_MIC_EP;
    ep_desc = (tusb_desc_endpoint_t const *)ns2_audio_mic_ep_desc;
    packet = ns2_audio_mic_silence;
    activated = &ns2_audio_mic_activated;
    armed = &ns2_audio_mic_armed;
  } else {
    return false;
  }

  if (*current_alt == alt)
    return true;
#ifndef TUP_DCD_EDPT_ISO_ALLOC
  if (*current_alt != 0) {
    usbd_edpt_close(rhport, ep_addr);
    *activated = false;
    *armed = false;
  }
#endif
  *current_alt = 0;

  if (alt != 0) {
    if (!*activated) {
#ifdef TUP_DCD_EDPT_ISO_ALLOC
      if (!usbd_edpt_iso_activate(rhport, ep_desc))
        return false;
#else
      if (!usbd_edpt_open(rhport, ep_desc))
        return false;
#endif
      *activated = true;
    }
    *current_alt = alt;
    // Under the RP ISO allocation API, selecting alt 0 does not close or
    // abort the endpoint. Reuse a transfer that is still pending; if it
    // completed while inactive, the callback cleared `armed` and we
    // safely queue a new one here.
    if (!*armed) {
      if (!usbd_edpt_xfer(rhport, ep_addr, packet,
                          ep_addr == NS2_AUDIO_SPEAKER_EP
                              ? NS2_AUDIO_SPEAKER_TRANSFER_SIZE
                              : NS2_AUDIO_PACKET_SIZE)) {
#ifndef TUP_DCD_EDPT_ISO_ALLOC
        usbd_edpt_close(rhport, ep_addr);
        *activated = false;
#endif
        *current_alt = 0;
        return false;
      }
      *armed = true;
    }
  }
  headphone_audio_set_streams(ns2_audio_alt_speaker != 0,
                              ns2_audio_alt_mic != 0);
  return true;
}

static bool ns2_audio_control(uint8_t rhport, uint8_t stage,
                              tusb_control_request_t const *request) {
  uint8_t const recipient = request->bmRequestType_bit.recipient;
  uint8_t const type = request->bmRequestType_bit.type;

  // TinyUSB forwards standard alternate-setting requests to the owning class
  // driver. Open/close the isochronous endpoint before completing
  // SET_INTERFACE.
  if (type == TUSB_REQ_TYPE_STANDARD && recipient == TUSB_REQ_RCPT_INTERFACE) {
    uint8_t const itf = tu_u16_low(request->wIndex);
    if (stage != CONTROL_STAGE_SETUP)
      return true;
    if (request->bRequest == TUSB_REQ_GET_INTERFACE) {
      uint8_t *alt = NULL;
      if (itf == NS2_AUDIO_SPEAKER_ITF)
        alt = &ns2_audio_alt_speaker;
      if (itf == NS2_AUDIO_MIC_ITF)
        alt = &ns2_audio_alt_mic;
      return alt ? tud_control_xfer(rhport, request, alt, 1) : false;
    }
    if (request->bRequest == TUSB_REQ_SET_INTERFACE) {
      uint8_t const alt = tu_u16_low(request->wValue);
      return ns2_audio_set_alt(rhport, itf, alt)
                 ? tud_control_status(rhport, request)
                 : false;
    }
    return false;
  }

  if (type != TUSB_REQ_TYPE_CLASS || recipient != TUSB_REQ_RCPT_INTERFACE)
    return false;

  uint8_t const unit_id = tu_u16_high(request->wIndex);
  uint8_t const cs = tu_u16_high(request->wValue);
  uint8_t const channel = tu_u16_low(request->wValue);
  ns2_audio_feature_state_t *feature = ns2_audio_feature(unit_id);
  if (!feature || channel != 0)
    return false; // only master controls are advertised

  if (request->bRequest == UAC1_REQ_SET_CUR) {
    uint16_t expected_len;
    if (cs == AUDIO_FU_CTRL_MUTE)
      expected_len = 1;
    else if (cs == AUDIO_FU_CTRL_VOLUME)
      expected_len = 2;
    else
      return false;
    if (request->wLength != expected_len)
      return false;

    if (stage == CONTROL_STAGE_SETUP)
      return tud_control_xfer(rhport, request, ns2_audio_control_data,
                              expected_len);
    if (stage == CONTROL_STAGE_DATA) {
      if (cs == AUDIO_FU_CTRL_MUTE) {
        feature->mute = ns2_audio_control_data[0] ? 1 : 0;
      } else {
        int16_t value = (int16_t)((uint16_t)ns2_audio_control_data[0] |
                                  ((uint16_t)ns2_audio_control_data[1] << 8));
        if (value < NS2_AUDIO_VOLUME_MIN)
          value = NS2_AUDIO_VOLUME_MIN;
        if (value > NS2_AUDIO_VOLUME_MAX)
          value = NS2_AUDIO_VOLUME_MAX;
        feature->volume = value;
      }
      if (unit_id == 0x02) {
        headphone_audio_set_control(ns2_audio_speaker_feature.mute != 0,
                                    ns2_audio_speaker_feature.volume);
      }
    }
    return true;
  }

  if (stage != CONTROL_STAGE_SETUP)
    return true;
  if (cs == AUDIO_FU_CTRL_MUTE && request->bRequest == UAC1_REQ_GET_CUR)
    return tud_control_xfer(rhport, request, &feature->mute, 1);

  if (cs == AUDIO_FU_CTRL_VOLUME) {
    int16_t const *value = NULL;
    static const int16_t volume_min = NS2_AUDIO_VOLUME_MIN;
    static const int16_t volume_max = NS2_AUDIO_VOLUME_MAX;
    static const int16_t volume_res = NS2_AUDIO_VOLUME_RES;
    if (request->bRequest == UAC1_REQ_GET_CUR)
      value = &feature->volume;
    else if (request->bRequest == UAC1_REQ_GET_MIN)
      value = &volume_min;
    else if (request->bRequest == UAC1_REQ_GET_MAX)
      value = &volume_max;
    else if (request->bRequest == UAC1_REQ_GET_RES)
      value = &volume_res;
    if (value)
      return tud_control_xfer(rhport, request, (void *)value, 2);
  }
  return false;
}

static bool ns2_audio_xfer(uint8_t rhport, uint8_t ep_addr,
                           xfer_result_t result, uint32_t xferred_bytes) {
  if (ep_addr == NS2_AUDIO_SPEAKER_EP) {
    ns2_audio_speaker_armed = false;
    if (ns2_audio_alt_speaker == 0)
      return true;
    // Isochronous packets are allowed to be lost. TinyUSB's own audio
    // driver deliberately ignores the prior transfer result and always
    // rearms the endpoint; leaving it unarmed after one missed packet
    // stops PCM delivery until Windows recovers the interface. Only feed a
    // successful payload to the bridge, but keep listening either way.
    bool const valid = result == XFER_RESULT_SUCCESS && xferred_bytes != 0 &&
                       xferred_bytes <= NS2_AUDIO_SPEAKER_TRANSFER_SIZE;
    if (valid)
      memcpy(ns2_audio_speaker_completed, ns2_audio_speaker_packet, xferred_bytes);
    bool const armed = usbd_edpt_xfer(rhport, ep_addr, ns2_audio_speaker_packet,
                                      NS2_AUDIO_SPEAKER_TRANSFER_SIZE);
    ns2_audio_speaker_armed = armed;
    // Rearm before queue locks/PCM assembly. The next IRQ may already overwrite
    // the receive buffer, so pass only the preserved completed batch onward.
    if (valid)
      headphone_audio_submit_pcm(ns2_audio_speaker_completed,
                                 (uint16_t)xferred_bytes);

    // Dispatch a due Bluetooth packet even if isochronous traffic keeps the
    // USB event loop busy. Codec work remains exclusively on core 1.
    headphone_audio_service_io();

    return armed;
  }
  if (ep_addr == NS2_AUDIO_MIC_EP) {
    ns2_audio_mic_armed = false;
    if (ns2_audio_alt_mic == 0)
      return true;
    bool const armed = usbd_edpt_xfer(rhport, ep_addr, ns2_audio_mic_silence,
                                      NS2_AUDIO_PACKET_SIZE);
    ns2_audio_mic_armed = armed;
    headphone_audio_service_io();
    return armed;
  }
  return true;
}

static void ns2_audio_init(void) {
  ns2_audio_alt_speaker = 0;
  ns2_audio_alt_mic = 0;
  ns2_audio_speaker_activated = false;
  ns2_audio_mic_activated = false;
  ns2_audio_speaker_armed = false;
  ns2_audio_mic_armed = false;
  ns2_audio_speaker_feature = (ns2_audio_feature_state_t){0, 0};
  ns2_audio_mic_feature = (ns2_audio_feature_state_t){0, 0};
  headphone_audio_set_control(false, 0);
  memset(ns2_audio_mic_silence, 0, sizeof(ns2_audio_mic_silence));
  headphone_audio_set_streams(false, false);
}

static void ns2_audio_reset(uint8_t rhport) {
  (void)rhport;
  ns2_audio_init();
}

static const usbd_class_driver_t ns2_audio_driver = {
    .init = ns2_audio_init,
    .reset = ns2_audio_reset,
    .open = ns2_audio_open,
    .control_xfer_cb = ns2_audio_control,
    .xfer_cb = ns2_audio_xfer,
};

// The audio driver only owns PC2's audio interfaces (IF2-4), which only exist
// in PC2's own config descriptor -- it must not be offered while a different
// personality (GameCube, CDC config) is active and presenting a completely
// different interface set. See docs/switch2-gc/usb-personality.md "TinyUSB
// dispatch and resource constraints".
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {

  *driver_count = 1;
  return &ns2_audio_driver;
}

void headphone_usb_suspend(bool suspended) {
  headphone_audio_set_streams(!suspended && ns2_audio_alt_speaker != 0,
                              !suspended && ns2_audio_alt_mic != 0);
}
