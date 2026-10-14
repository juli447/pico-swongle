# Pico Swongle

Turn a Raspberry Pi **Pico 2 W** into a Bluetooth controller adapter for Nintendo Switch works with Dualsense, Dualshok 4 and X-Box Controllers theoretically (Only Dualsense tested)

[Download v1.1.0](https://github.com/juli447/pico-swongle/releases/tag/v1.1.0) · [Changelog](CHANGELOG.md)

## Choose your firmware

Both downloads are included in the same stable **v1.1.0** release.

| Download | USB controller identity |
| --- | --- |
| `pico-swongle-switch1-v1.1.0.uf2` | Original Switch Pro Controller |
| `pico-swongle-switch2-v1.1.0.uf2` | Switch 2 Pro Controller with Headphone support |

The original Switch firmware can also be used on Switch 2. The native Switch 2 firmware is intended for Switch 2 only. Both support Home/Capture, Bluetooth reconnect, PS4/PS5 motion, rumble and PS4/PS5 RGB lightbar. Xbox controllers without motion sensors cannot supply motion.

## Smooth RGB lightbar

The PS4/PS5 lightbar starts with a smooth rainbow animation. **Hold the touchpad down, then press PS** to toggle RGB on/off. Disabled means the lightbar is dark.

## DualSense audio on Switch 2

Plug headphones into your Bluetooth-connected PS5 controller to hear the console's audio. **Headphones always take priority.** Without headphones, **Touchpad click + Mute** toggles normal console audio and the internal DualSense speaker. Removing headphones restores the previous output selection. A controller reconnect resets the selection to normal console output.

Audio playback, speaker switching and RGB were tested on Switch 2 with a DualSense. RGB traffic gives audio priority. See [audio setup and build options](docs/headphone-audio.md).

## Flash and pair

1. Hold **BOOTSEL** while connecting the Pico 2 W to your computer with a USB data cable.
2. Copy the chosen release's `.uf2` to the Pico bootloader drive. It restarts automatically.
3. Enable **Pro Controller Wired Communication** in your console's controller settings.
4. Connect the Pico's Micro-USB port to a USB-A or USB-C port on your Switch dock/console.
5. Pair your controller:
   - PS5: with the controller off, hold **Create + PS** until the lights flash.
   - PS4: with the controller off, hold **Share + PS** until the light bar flashes.
   - Bluetooth Xbox: turn it on and hold its **Pair** button.
6. Press a Button on the Controller if the Switch hasn´t assigned a Player Number

Bonds survive restarts. To change controllers, turn off the current controller, then pair the other. An optional button between **GP15 (pin 20)** and **GND (pin 18)**, held during startup, clears saved Bluetooth bonds.

## Buttons

Face buttons follow their physical Switch positions:

| Position | PlayStation | Xbox | Switch |
| --- | --- | --- | --- |
| South | Cross | A | B |
| East | Circle | B | A |
| West | Square | X | Y |
| North | Triangle | Y | X |

- Options/Menu → **Plus**; Share/Create/View → **Minus**.
- PS/Xbox → **Home**
- PS/Xbox + Share/Create/View → **Capture**.
- DualSense Mute → **Capture**
- **Native Switch 2 build:** touchpad click → **C/GameChat**; Xbox Share → **C/GameChat**. The touch surface is not used
- **Original Switch build:** Xbox Share → **Capture**; touchpad click has no console mapping but can toggle RGB with Dualsense or Dualshock.
- **PS4/PS5 local shortcut:** hold touchpad, then press PS → **RGB on/off**.
- **Switch 2 + DualSense local shortcut:** Touchpad + Mute → **console/internal speaker audio** when no headphones are inserted.

Bluetooth Xbox support depends on controller model and firmware; a newer controller with a Share button is required for the C-Button mapping. Xbox 360 and Xbox One models without Bluetooth are not supported.

## LED status

| LED | Meaning |
| --- | --- |
| Slow blink | Waiting for Bluetooth pairing/reconnect |
| Two flashes | Bluetooth paired; USB not configured |
| Three flashes | USB configured; no host commands received |
| Five flashes | Reports received, but no supported host command parsed |
| Four flashes | USB input transfers are not completing |
| Six flashes | Input reports stopped arriving |
| Eight flashes at startup | Watchdog recovered a stalled loop |
| Mostly solid with tiny periodic off pulse | Bluetooth input and USB transfers active |

A solid LED confirms transport activity; the console must also accept and assign the controller. Try a different USB data cable/port in case of 2 flashes.

## Scope

One controller per Pico. Proprietary Xbox wireless/2.4 GHz receivers, NFC/amiibo(due to lack of NFC on every Controller that is targeted) and GL/GR grip buttons (Due to lack of Physical Buttons and me not owning a Dualsense Edge) are not implemented. Rumble translates amplitudes to conventional controller motors; it does not reproduce exact Nintendo HD Rumble waveforms.

## Build

Install Git, CMake, Ninja, and an Arm GNU `arm-none-eabi` toolchain, with the compiler on `PATH`. Bluepad32 and the selected protocol helpers are vendored.

```sh
git clone --branch 2.3.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git pico-sdk
git -C pico-sdk submodule update --init lib/btstack lib/cyw43-driver lib/tinyusb
cmake -S . -B build-switch1 -G Ninja -DPICO_SDK_PATH="$PWD/pico-sdk" -DSWONGLE_TARGET=switch1
cmake --build build-switch1 --parallel 4
cmake -S . -B build-switch2 -G Ninja -DPICO_SDK_PATH="$PWD/pico-sdk" -DSWONGLE_TARGET=switch2
cmake --build build-switch2 --parallel 4
```

Each build directory contains `pico2w_switch_adapter.uf2`. This project targets **Pico 2 W / RP2350 ARM**, not Pico W/RP2040. CMake may fetch picotool. Local builds were verified with Pico SDK 2.3.1 and Arm GNU 14.3.Rel1.

The audio-enabled Switch 2 build uses a 300 MHz CPU clock and 1.20 V core voltage, matching the tested reference configuration. This exceeds the board’s standard CPU clock. To omit audio, add `-DSWONGLE_HEADPHONE_AUDIO=OFF`.

Run `sh tests/run_tests.sh` with GCC/G++ on Linux. Tests exercise mapping, motion, haptics, USB handshakes, malformed packets, native C mapping, pairing crypto, calibration reads, and disconnect/suspend handling. Both USB personalities have been tested on Switch 2; the new audio and RGB shortcut behaviour was confirmed with a DualSense. PS4 RGB uses Bluepad32’s existing lightbar support; no Tests where performed with a PS4 Controller. For audio/codec/UAC tests, run `PICO_SDK_PATH=/absolute/path/to/pico-sdk sh tests/run_audio_tests.sh`. An optional GitHub Actions template is included at `examples/github-actions/firmware.yml`. Copy it to `.github/workflows/firmware.yml` to enable builds for both personalities and tag-based releases; it is not enabled by default.

## Credits and licenses

- [2wiCC](https://github.com/knflrpn/2wiCC), MIT: original Switch USB protocol, descriptors and calibration. Snapshot `2c2275c2fd231e6b4d9884cdff5d723ddbccc226`.
- [Bluepad32](https://github.com/ricardoquesada/bluepad32), Apache-2.0: Bluetooth controller support. Snapshot `80d6314e46f871719fbb4b721a2cc02227c4f369`; local fixes include touchpad click forwarding, calibrated motion, and Pico SDK compatibility.
- [switch-pico](https://github.com/jyapayne/switch-pico), MIT: Switch motion packing and haptics decoder. Snapshot `7449d6d029a0d3aa1ff1ce80d524a8f7b1e08ba8`.
- [PicoSwitch2](https://github.com/notsosaelin/PicoSwitch2), Apache-2.0: native Switch 2 protocol, pairing crypto, firmware profile, and motion translation. USB descriptor/factory reference v1.5.0; pure helpers v2.5.0 (`cdb5bf6f85cef9998c11da280073a0c7642b712c`). Local changes include USB lifecycle integration, bounded/queued replies, synthetic stick calibration, and defined signed arithmetic.
- [Opus](https://github.com/xiph/opus), BSD-style: CELT audio codec, pinned to `2d862ea14b233e5a3f3afaf74d96050691af3cd5` (`v1.6.1-9-g2d862ea1`). Notices are retained in `third_party/opus/COPYING` and `licenses/opus.txt`.
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.3.1 and its dependencies: bundled binary notices are in `licenses/`.

New adapter code is MIT; the adapted native Switch 2 backend and PicoSwitch2 helpers are Apache-2.0. Retain the included third-party licenses and notices when distributing source or firmware. This is an independent project, unaffiliated with Nintendo, Sony, Microsoft, Raspberry Pi.
