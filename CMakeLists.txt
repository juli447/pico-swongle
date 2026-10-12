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
    src/main.c src/input_mapping.c src/motion.c src/rumble.cpp src/status_bridge.c ${USB_SOURCES}
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
set(SWONGLE_VERSION "1.0.0")
pico_set_program_version(pico2w_switch_adapter "${SWONGLE_VERSION}")
pico_add_extra_outputs(pico2w_switch_adapter)
