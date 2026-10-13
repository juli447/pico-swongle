# Changelog

## v1.1.0 — 2026-10-13

- Native Switch 2 firmware plays console audio through DualSense headphones or its internal speaker. Touchpad + Mute selects console/internal-speaker output; physical headphones always take priority.
- USB audio reception continues across packet batches and is serviced between Bluetooth input packets, addressing the rare audio gaps observed on the user’s console without a large added-latency reservoir.
- Both firmware personalities add smooth PS4/PS5 RGB lightbars. Hold Touchpad, then press PS to toggle RGB; Home retains immediate input and long holds, using direct combination masking like the screenshot shortcut.
- Cosmetic DualSense output shares the ordered audio transport and waits when audio needs Bluetooth credits.
- Updated English documentation, audio/RGB regression coverage and retained Opus/PicoSwitch2 notices. Test tone and audio-stage blink diagnostics are off in release builds.
- One stable release contains separate original Switch and native Switch 2 firmware downloads, a source archive and SHA256 checksums.

## v1.0.0

Initial stable release: original Switch and native Switch 2 USB personalities, Bluetooth controller input, PS4/PS5 motion, rumble and native C/GameChat mapping. Both firmware downloads were combined in one release.
