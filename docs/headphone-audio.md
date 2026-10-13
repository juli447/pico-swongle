# DualSense audio playback

The native Switch 2 firmware forwards the console’s USB audio to headphones or the internal speaker of a Bluetooth-connected PS5 DualSense. The user confirmed smooth playback, output switching, RGB coexistence and the final RGB/Home shortcut behaviour on Switch 2. The original Switch firmware has no controller audio.

## Use

1. Flash `pico-swongle-switch2-v1.1.0.uf2`, connect the Pico 2 W to the dock and pair your DualSense.
2. Insert headphones into the controller’s 3.5 mm jack. The console routes playback through the USB controller, then to the headphones.
3. With no headphones inserted, press **Touchpad click + Mute** to toggle normal console audio and the controller’s internal speaker. Adjust playback volume using the console’s headphone-volume control.
4. Headphones always override the internal-speaker selection. Unplugging restores the previously selected no-headphone output. Reconnecting the controller resets that selection to console audio.

**Hold Touchpad, then press PS** to toggle the smooth RGB lightbar. Home alone is forwarded immediately, including long holds. Once the RGB combination is recognized, its buttons remain suppressed until both are released. Previously forwarded single-button actions cannot be withdrawn, so use the touchpad-first order. Touchpad+Mute is handled separately and suppresses the recognized audio shortcut’s Capture/GameChat actions.

This is playback only. The headset microphone is not decoded or forwarded; the USB microphone interface returns silence. PS4/Xbox headphone audio, adaptive triggers and NFC are not supported.

## Runtime

The audio-enabled firmware runs at **300 MHz / 1.20 V**, above the Pico 2 W’s standard CPU clock, using the tested PicoSwitch2 audio configuration. The CYW43 PIO bus runs at 75 MHz. Bluetooth and USB run on core 0 in a foreground polling context; CELT encoding runs on core 1 with a 48 KiB stack and SRAM-resident codec routines. Pico SDK flash lockout coordination is retained.

USB reception uses up to eight 192-byte packets per receive transfer. Completed bytes are preserved and reception is rearmed before PCM processing. USB also runs between Bluetooth input packets, and UAC callbacks do not recursively poll the radio. These changes addressed the rare receive gaps reported during console testing.

Each 512-frame stereo PCM block is resampled to 480 frames and encoded to a fixed 200-byte CELT/Opus frame. Two frames form one DualSense report 0x39. Live output follows the PCM producer with a one-frame startup cushion; it does not use the discarded large latency reservoir. The controller’s effective playback clock corresponds to a 64/3-ms frame-pair interval.

Extended control report 0x32 sets headphones or internal-speaker routing. Headphone detection and the selected internal speaker request USB audio through the console’s controller-headset state. Native haptic PCM carries rumble alongside audio. Lightbar reports share the ordered audio transport and modulo-16 sequence counter; cosmetic updates wait for immediate credits and an empty FIFO. Suspend, output changes and disconnect invalidate queued audio so old sessions cannot replay.

## Build options

| Option | Default | Purpose |
| --- | --- | --- |
| `SWONGLE_TARGET=switch2` | `switch1` | Select native Switch 2 USB personality |
| `SWONGLE_HEADPHONE_AUDIO` | `ON` | Enable DualSense playback for Switch 2 only |
| `SWONGLE_AUDIO_TEST_TONE` | `OFF` | Pre-encoded quiet tone, bypassing USB PCM and live encoding |
| `SWONGLE_AUDIO_DIAGNOSTICS` | `OFF` | Temporary Pico LED diagnostics for observed audio-stage gaps |

To build without audio, use `-DSWONGLE_TARGET=switch2 -DSWONGLE_HEADPHONE_AUDIO=OFF`. Release firmware disables the test tone and audio-stage diagnostics. The normal pairing/USB status LED remains enabled.

When diagnostics are enabled, short ON-flash groups repeat for 20 seconds: **1** USB PCM gap over 24 ms; **2** resample/encode over 21.333 ms; **3** queued L2CAP blocked over 30 ms; **4** outgoing audio gap over 45 ms; **5** PCM/encoded queue overflow. Multiple observations prioritize the lowest code. These identify an observed stage, not necessarily the underlying cause.

## Validation and provenance

`sh tests/run_tests.sh` covers mapping, RGB continuity/shortcut behaviour, motion, haptics, rumble, USB handshakes, pairing and lifecycle handling. `PICO_SDK_PATH=/absolute/path/to/pico-sdk sh tests/run_audio_tests.sh` additionally covers packet CRCs, resampling, actual CELT output decoded through Opus, UAC reception/rearm, routing and headphone priority, congestion, disconnect, tone cadence and diagnostic injection. Both firmware personalities build. Hardware confirmation applies to the user’s Switch 2 / DualSense setup; no separate PS4 audio support or PS4 RGB console test is claimed.

Packet packing, resampling, RAM memory helpers, headset definitions and the UAC1 driver are adapted from [PicoSwitch2 v2.5.0](https://github.com/notsosaelin/PicoSwitch2/tree/v2.5.0), Apache-2.0. The codec design follows that implementation and [DS5Dongle](https://github.com/awalol/DS5Dongle). Vendored [Xiph Opus](https://github.com/xiph/opus) is pinned to `2d862ea14b233e5a3f3afaf74d96050691af3cd5`; original license notices are included.
