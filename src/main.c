// SPDX-License-Identifier: MIT
#include <uni.h>
#include <string.h>
#include "pico/cyw43_arch.h"
#include "pico/async_context_poll.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "switch_usb.h"
#include "rumble.h"

// Optional button between GP15 and GND: hold during boot to forget Bluetooth bonds.
#define FORGET_PIN 15
static bool forget_keys;
static uni_hid_device_t *active;
static bool usb_started;
static async_context_poll_t foreground_context;
static bool received_input;
static uint8_t last_buttons[3];
static uint32_t activity_at;
static bool activity_pending;
static uint32_t input_at;
static bool continuous_reports;
static bool neutralized;

static bool inputs_fresh(uint32_t now) {
    return received_input && (!continuous_reports || now - input_at < 1000);
}

static void platform_init(int argc, const char **argv) { (void)argc; (void)argv; }
static void init_complete(void) {
    if (forget_keys) uni_bt_del_keys_unsafe();
    // Don't expose USB while the radio is initializing and USB servicing is unavailable.
    switch_usb_init();
    usb_started = true;
    watchdog_enable(2000, true);
    uni_bt_start_scanning_and_autoconnect_unsafe();
}
static void connected(uni_hid_device_t *d) { (void)d; }
static uni_error_t ready(uni_hid_device_t *d) {
    if (active || !uni_hid_device_is_gamepad(d) || uni_hid_device_is_virtual_device(d))
        return UNI_ERROR_NO_SLOTS;
    active = d;
    continuous_reports = d->controller_type == CONTROLLER_TYPE_PS5Controller ||
        d->controller_type == CONTROLLER_TYPE_PS4Controller;
    input_at = to_ms_since_boot(get_absolute_time());
    neutralized = false;
    received_input = activity_pending = false;
    memset(last_buttons, 0, sizeof(last_buttons));
    // Complete the parser/device-ready transition before issuing scan-stop HCI commands.
    uni_bt_stop_scanning_safe();
    return UNI_ERROR_SUCCESS;
}
static void disconnected(uni_hid_device_t *d) {
    if (active != d) return;
    active = NULL;
    rumble_stop();
    switch_usb_stop_motion();
    received_input = activity_pending = false;
    switch_state_t neutral;
    switch_state_neutral(&neutral);
    switch_usb_set_state(&neutral); // Prevent stuck buttons when the link drops.
    uni_bt_start_scanning_and_autoconnect_unsafe();
}
static void controller_data(uni_hid_device_t *d, uni_controller_t *ctl) {
    if (d != active || ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) return;
    switch_state_t state;
    switch_state_from_gamepad(&ctl->gamepad, &state);
#ifdef ADAPTER_SWITCH2
    switch_state_set_gamechat(&ctl->gamepad,
        d->controller_type == CONTROLLER_TYPE_XBoxOneController ||
        d->controller_type == CONTROLLER_TYPE_XBox360Controller, &state);
#endif
    switch_usb_set_state(&state);
    if (continuous_reports) switch_usb_set_motion(&ctl->gamepad);
    received_input = true;
    input_at = to_ms_since_boot(get_absolute_time());
    neutralized = false;
    if (memcmp(last_buttons, state.bytes, 3)) {
        memcpy(last_buttons, state.bytes, 3);
        activity_at = to_ms_since_boot(get_absolute_time());
        activity_pending = true;
    }
}
static const uni_property_t *get_property(uni_property_idx_t idx) {
    (void)idx; return NULL;
}
static void oob_event(uni_platform_oob_event_t event, void *data) {
    (void)event; (void)data;
}

static void update_led(void) {
    static bool led_known, last_led;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool led;
    if (!active) led = (now / 500) & 1;
    else if (!switch_usb_has_host_commands() || !inputs_fresh(now) || !switch_usb_reports_flowing()) {
        unsigned flashes;
        if (!switch_usb_is_mounted()) flashes = 2;
        else if (!switch_usb_has_any_reports()) flashes = 3;
        else if (!switch_usb_has_host_commands()) flashes = 5;
        else if (!inputs_fresh(now)) flashes = 6;
        else flashes = 4;
        uint32_t phase = now % 2500;
        led = phase < flashes * 250 && phase % 250 < 100;
    } else {
        // Solid means controller reports are received and USB input transfers complete.
        // Brief off pulses on button changes make the Bluetooth input path visible.
        // A short heartbeat every two seconds proves the main loop is still running.
        led = !(activity_pending && now - activity_at < 60) && now % 2000 >= 40;
        if (now - activity_at >= 60) activity_pending = false;
    }
    if (!led_known || led != last_led) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
        last_led = led;
        led_known = true;
    }
}

int main(void) {
    gpio_init(FORGET_PIN);
    gpio_set_dir(FORGET_PIN, GPIO_IN);
    gpio_pull_up(FORGET_PIN);
    sleep_ms(5);
    forget_keys = !gpio_get(FORGET_PIN);
    // pico_cyw43_arch_none otherwise defaults to a background IRQ context.
    // Explicitly install a polling context before initializing the radio/BTstack.
    if (!async_context_poll_init_with_defaults(&foreground_context)) return 2;
    cyw43_arch_set_async_context(&foreground_context.core);
    if (cyw43_arch_init()) return 1;
    if (watchdog_caused_reboot()) {
        // Eight quick flashes at startup identify a recovered main-loop stall.
        for (unsigned i = 0; i < 8; ++i) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
            sleep_ms(60);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
            sleep_ms(60);
        }
    }
    static struct uni_platform platform = {
        .name = "Pico2W-Switch-Adapter",
        .init = platform_init,
        .on_init_complete = init_complete,
        .on_device_connected = connected,
        .on_device_disconnected = disconnected,
        .on_device_ready = ready,
        .on_controller_data = controller_data,
        .get_property = get_property,
        .on_oob_event = oob_event,
    };
    uni_platform_set_custom(&platform);
    uni_init(0, NULL);
    while (true) {
        // Poll Bluetooth and service USB independently in the foreground.
        // USB work no longer depends on a BTstack timer firing or runs inside its timer callback.
        cyw43_arch_poll();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (active && continuous_reports && !inputs_fresh(now) && !neutralized) {
            switch_state_t neutral;
            switch_state_neutral(&neutral);
            switch_usb_set_state(&neutral);
            switch_usb_stop_motion();
            rumble_stop();
            neutralized = true;
        }
        if (usb_started) switch_usb_task();
        uint8_t weak, strong;
        if (active && rumble_take(now,&weak,&strong) && active->report_parser.play_dual_rumble) {
            if (!inputs_fresh(now)) weak=strong=0;
            active->report_parser.play_dual_rumble(active,0,(weak || strong) ? 150 : 0,weak,strong);
        }
        update_led();
        if (usb_started) watchdog_update();
        sleep_us(100);
    }
    return 0;
}
