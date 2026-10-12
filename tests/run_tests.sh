#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
export UBSAN_OPTIONS=halt_on_error=1
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Isrc -Ithird_party/bluepad32/include \
    tests/test_mapping.c src/input_mapping.c -o "$test_dir/mapping"
"$test_dir/mapping"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Isrc -Ithird_party/bluepad32/include tests/test_motion.c src/motion.c -lm -o "$test_dir/motion"
"$test_dir/motion"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Ithird_party/switch-pico tests/test_haptics.cpp third_party/switch-pico/switch_haptics.cpp \
    -o "$test_dir/haptics"
"$test_dir/haptics"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Isrc -Ithird_party/switch-pico tests/test_rumble.cpp src/rumble.cpp \
    third_party/switch-pico/switch_haptics.cpp -o "$test_dir/rumble"
"$test_dir/rumble"
g++ -std=c++17 -fsanitize=address,undefined -Isrc -Ithird_party/switch-pico \
    -c src/rumble.cpp -o "$test_dir/rumble.o"
g++ -std=c++17 -fsanitize=address,undefined -Ithird_party/switch-pico \
    -c third_party/switch-pico/switch_haptics.cpp -o "$test_dir/haptics.o"
gcc -std=c11 -fsanitize=address,undefined \
    -Itests/stubs -Isrc -Ithird_party/2wiCC -Ithird_party/bluepad32/include \
    tests/test_usb.c src/switch_usb.c src/input_mapping.c src/motion.c src/status_bridge.c \
    third_party/2wiCC/procon_functions.c third_party/2wiCC/procon_data.c \
    "$test_dir/rumble.o" "$test_dir/haptics.o" -lstdc++ -lm -o "$test_dir/usb"
"$test_dir/usb"
gcc -std=c11 -Wall -Wextra -fsanitize=address,undefined \
    -Itests/stubs -Isrc -Ithird_party/bluepad32/include -Ithird_party/picoswitch2 \
    tests/test_switch2_usb.c src/switch2_usb.c src/input_mapping.c \
    third_party/picoswitch2/ns2_pairing_crypto.c third_party/picoswitch2/ns2_firmware_profile.c \
    third_party/picoswitch2/ns2_ds5_motion.c third_party/picoswitch2/ns2_motion_pdu.c \
    "$test_dir/rumble.o" "$test_dir/haptics.o" -lstdc++ -lm -o "$test_dir/switch2"
"$test_dir/switch2"
for suite in ns2_pairing_crypto ns2_ds5_motion; do
    gcc -std=c11 -Wall -Wextra -fsanitize=address,undefined -Ithird_party/picoswitch2 \
        "tests/test_$suite.c" "third_party/picoswitch2/$suite.c" \
        third_party/picoswitch2/ns2_motion_pdu.c -lm -o "$test_dir/$suite"
    "$test_dir/$suite"
done
