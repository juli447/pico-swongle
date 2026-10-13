cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2_w CACHE STRING "Target board")
include(pico_sdk_import.cmake)
project(pico2w_switch_adapter C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

set(BLUEPAD32_ROOT ${CMAKE_CURRENT_LIST_DIR}/third_party/bluepad32)
set(BTSTACK_ROOT ${PICO_SDK_PATH}/lib/btstack)
# Bluepad32 and BTstack must see the same configuration as the application.
include_directories(src ${BTSTACK_ROOT}/src
    ${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include
    ${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include)
set(SWONGLE_TARGET "switch1" CACHE STRING "USB personality: switch1 or switch2")
option(SWONGLE_AUDIO_DIAGNOSTICS "Temporary audio delay blink diagnostics" OFF)
option(SWONGLE_AUDIO_TEST_TONE "Diagnostic quiet tone, bypasses USB PCM and codec worker" OFF)
option(SWONGLE_HEADPHONE_AUDIO "Switch 2 -> DualSense headphone audio" ON)
if(SWONGLE_TARGET STREQUAL "switch2" AND SWONGLE_HEADPHONE_AUDIO)
    add_compile_definitions(ADAPTER_SWITCH2_AUDIO=1 NS2_DS5_AUDIO=1 NS2_DS5_AUDIO_LIVE_OPUS=1
        NS2_DS5_AUDIO_RAM_MEM=1 PICO_CORE1_STACK_SIZE=0 PICO_STACK_SIZE=4096 CYW43_PIO_CLOCK_DIV_INT=4)
    if(SWONGLE_AUDIO_DIAGNOSTICS)
        add_compile_definitions(SWONGLE_AUDIO_DIAGNOSTICS=1)
    endif()
    set(OPUS_PACKAGE_VERSION "1.6.1-9-g2d862ea1" CACHE STRING "Pinned Opus snapshot" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_FIXED_POINT OFF CACHE BOOL "" FORCE)
    set(OPUS_ENABLE_FLOAT_API ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/opus EXCLUDE_FROM_ALL)
    set(AUDIO_SOURCES src/headphone_audio.c src/headphone_usb.c
        third_party/picoswitch2/ds5_audio_packet.c third_party/picoswitch2/ds5_audio_resample.c
        third_party/picoswitch2/ds5_audio_ram_mem.c)
    if(SWONGLE_AUDIO_TEST_TONE)
        add_compile_definitions(SWONGLE_AUDIO_TEST_TONE=1)
        list(APPEND AUDIO_SOURCES src/headphone_test_tone.c)
    endif()
endif()
add_subdirectory(${BLUEPAD32_ROOT} libbluepad32)

set(SWONGLE_TARGET "switch1" CACHE STRING "USB personality: switch1 or switch2")
set_property(CACHE SWONGLE_TARGET PROPERTY STRINGS switch1 switch2)
if(SWONGLE_TARGET STREQUAL "switch2")
    add_compile_definitions(ADAPTER_SWITCH2=1)
    set(USB_SOURCES src/switch2_usb.c
        third_party/picoswitch2/ns2_pairing_crypto.c third_party/picoswitch2/ns2_firmware_profile.c
        third_party/picoswitch2/ns2_ds5_motion.c third_party/picoswitch2/ns2_motion_pdu.c)
elseif(SWONGLE_TARGET STREQUAL "switch1")
    set(USB_SOURCES src/switch_usb.c third_party/2wiCC/usb_descriptors.c
        third_party/2wiCC/procon_functions.c third_party/2wiCC/procon_data.c)
else()
    message(FATAL_ERROR "SWONGLE_TARGET must be switch1 or switch2")
endif()
add_executable(pico2w_switch_adapter
    src/main.c src/controller_rgb.c src/input_mapping.c src/motion.c src/rumble.cpp src/status_bridge.c ${USB_SOURCES} ${AUDIO_SOURCES}
    third_party/switch-pico/switch_haptics.cpp
)
target_include_directories(pico2w_switch_adapter PRIVATE src third_party/2wiCC third_party/switch-pico third_party/picoswitch2)
target_link_libraries(pico2w_switch_adapter PRIVATE
    pico_stdlib pico_cyw43_arch_none pico_async_context_poll pico_btstack_classic pico_btstack_ble
    pico_btstack_cyw43 bluepad32 hardware_flash tinyusb_device tinyusb_board)
# The USB port belongs entirely to the Switch controller, not a serial console.
pico_enable_stdio_usb(pico2w_switch_adapter 0)
pico_enable_stdio_uart(pico2w_switch_adapter 0)
pico_set_program_name(pico2w_switch_adapter "Pico Swongle ${SWONGLE_TARGET}")
set(SWONGLE_VERSION "1.1.0")
pico_set_program_version(pico2w_switch_adapter "${SWONGLE_VERSION}")
pico_add_extra_outputs(pico2w_switch_adapter)

if(SWONGLE_TARGET STREQUAL "switch2" AND SWONGLE_HEADPHONE_AUDIO)
    target_include_directories(pico2w_switch_adapter PRIVATE third_party/opus/celt third_party/opus/src)
    target_link_libraries(pico2w_switch_adapter PRIVATE opus pico_multicore pico_flash)
    set_source_files_properties(third_party/picoswitch2/ds5_audio_ram_mem.c PROPERTIES
        COMPILE_OPTIONS "-fno-builtin;-fno-tree-loop-distribute-patterns")
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp
        COMMAND ${CMAKE_OBJCOPY} --rename-section .text=.time_critical.opus_text
            --rename-section .rodata=.time_critical.opus_rodata
            --rename-section .rodata.str1.4=.time_critical.opus_strings $<TARGET_FILE:opus>
        COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp
        DEPENDS $<TARGET_FILE:opus> VERBATIM)
    add_custom_target(opus_ram_relocate ALL DEPENDS ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp)
    add_dependencies(pico2w_switch_adapter opus_ram_relocate)
endif()
