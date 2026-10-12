# Pico Swongle — Switch 2 v1.0.0-rc.1

Console-tested native Switch 2 Pro Controller USB personality for Pico 2 W. Includes the native composite HID/vendor-bulk handshake, pairing challenge responses, motion translation and rumble parsing.

PS4/PS5 touchpad click and a supported Bluetooth Xbox controller's Share button send the native C/GameChat bit. PS5 Create remains Minus; Mute remains Capture.

Flash `pico-swongle-switch2-v1.0.0-rc.1.uf2` using BOOTSEL. This integration has passed firmware compilation and host protocol tests, and has now been tested on a Switch 2 console and confirmed working. The tested original Switch personality is available as `switch1-v1.0.0` for rollback.

No controller audio/headset, NFC, grip-button mapping, adaptive triggers or console wake. See README for setup, mapping and license notices.
