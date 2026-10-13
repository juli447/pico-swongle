#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${PICO_SDK_PATH:?Set PICO_SDK_PATH to the Pico SDK for TinyUSB headers}"
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
export UBSAN_OPTIONS=halt_on_error=1
cmake -S third_party/opus -B "$test_dir/opus" -DOPUS_PACKAGE_VERSION=1.6.1-9-g2d862ea1 \
    -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_SHARED_LIBRARY=OFF > "$test_dir/opus-config.log"
cmake --build "$test_dir/opus" --parallel 4 > "$test_dir/opus-build.log"
for suite in packet resample; do
    gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -DNS2_DS5_AUDIO=1 -DSWONGLE_AUDIO_DIAGNOSTICS=1 \
        -Ithird_party/picoswitch2 "tests/test_ds5_audio_$suite.c" "third_party/picoswitch2/ds5_audio_$suite.c" -o "$test_dir/$suite"
    "$test_dir/$suite"
done
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -DNS2_DS5_AUDIO=1 -DSWONGLE_AUDIO_DIAGNOSTICS=1 \
    -Itests/audio_stubs -Isrc -Ithird_party/picoswitch2 -Ithird_party/opus/include -Ithird_party/opus/celt -I"$test_dir/opus" \
    tests/test_headphone_bridge.c src/headphone_audio.c third_party/picoswitch2/ds5_audio_packet.c \
    third_party/picoswitch2/ds5_audio_resample.c "$test_dir/opus/libopus.a" -lm -pthread -o "$test_dir/bridge"
"$test_dir/bridge"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/uac_stubs \
    -I"$PICO_SDK_PATH/lib/tinyusb/src" -Isrc tests/test_headphone_usb.c src/headphone_usb.c -o "$test_dir/usb"
"$test_dir/usb"

gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ithird_party/opus/include \
    tests/test_headphone_tone.c src/headphone_test_tone.c "$test_dir/opus/libopus.a" -lm -o "$test_dir/tone"
"$test_dir/tone"

gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -DNS2_DS5_AUDIO=1 -DSWONGLE_AUDIO_DIAGNOSTICS=1 -DSWONGLE_AUDIO_TEST_TONE=1 \
    -Itests/audio_stubs -Isrc -Ithird_party/picoswitch2 -Ithird_party/opus/include -Ithird_party/opus/celt -I"$test_dir/opus" \
    tests/test_headphone_tone_cadence.c src/headphone_audio.c src/headphone_test_tone.c \
    third_party/picoswitch2/ds5_audio_packet.c third_party/picoswitch2/ds5_audio_resample.c \
    "$test_dir/opus/libopus.a" -lm -pthread -o "$test_dir/cadence"
"$test_dir/cadence"

gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -DNS2_DS5_AUDIO=1 -DSWONGLE_AUDIO_DIAGNOSTICS=1 \
    -Itests/audio_stubs -Isrc -Ithird_party/picoswitch2 -Ithird_party/opus/include -Ithird_party/opus/celt -I"$test_dir/opus" \
    tests/test_headphone_live_cadence.c src/headphone_audio.c \
    third_party/picoswitch2/ds5_audio_packet.c third_party/picoswitch2/ds5_audio_resample.c \
    "$test_dir/opus/libopus.a" -lm -pthread -o "$test_dir/live-cadence"
"$test_dir/live-cadence"
