// SPDX-License-Identifier: MIT
#include <uni.h>
#include <string.h>
#include "pico/cyw43_arch.h"
#include "pico/async_context_poll.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "switch_usb.h"
#include "rumble.h"
#include "controller_rgb.h"
#ifdef ADAPTER_SWITCH2_AUDIO
#include "headphone_audio.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#endif

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
static controller_rgb_t rgb;
static bool rgb_supported;

static bool inputs_fresh(uint32_t now) {
    return received_input && (!continuous_reports || now - input_at < 1000);
}

static void platform_init(int argc, const char **argv) { (void)argc; (void)argv; }
static void init_complete(void) {
    if (forget_keys) uni_bt_del_keys_unsafe();
    // Don't expose USB while the radio is initializing and USB servicing is unavailable.
#ifdef ADAPTER_SWITCH2_AUDIO
    headphone_audio_init();
#endif
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
    rgb_supported = (d->controller_type == CONTROLLER_TYPE_PS5Controller ||
        d->controller_type == CONTROLLER_TYPE_PS4Controller) && d->report_parser.set_lightbar_color;
    controller_rgb_init(&rgb, to_ms_since_boot(get_absolute_time()));
#ifdef ADAPTER_SWITCH2_AUDIO
    headphone_audio_connect(d);
#endif
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
#ifdef ADAPTER_SWITCH2_AUDIO
    headphone_audio_disconnect(d);
#endif
    active = NULL;
    rgb_supported = false;
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
    uni_gamepad_t gamepad = ctl->gamepad;
    bool ps = (gamepad.misc_buttons & MISC_BUTTON_SYSTEM) != 0;
    bool touchpad = (gamepad.misc_buttons & MISC_BUTTON_TOUCHPAD) != 0;
    bool audio_chord = false;
#ifdef ADAPTER_SWITCH2_AUDIO
    audio_chord = headphone_audio_output_buttons(d, touchpad,
            (gamepad.misc_buttons & MISC_BUTTON_CAPTURE) != 0);
    if (audio_chord)
        gamepad.misc_buttons &= ~(MISC_BUTTON_TOUCHPAD | MISC_BUTTON_CAPTURE);
#endif
    if (rgb_supported) {
        bool ps_out, touchpad_out;
        controller_rgb_filter_buttons(&rgb, to_ms_since_boot(get_absolute_time()),
            ps, touchpad, audio_chord, &ps_out, &touchpad_out);
        gamepad.misc_buttons &= ~(MISC_BUTTON_SYSTEM | MISC_BUTTON_TOUCHPAD);
        if (ps_out) gamepad.misc_buttons |= MISC_BUTTON_SYSTEM;
        if (touchpad_out) gamepad.misc_buttons |= MISC_BUTTON_TOUCHPAD;
    }
    switch_state_t state;
    switch_state_from_gamepad(&gamepad, &state);
#ifdef ADAPTER_SWITCH2
    switch_state_set_gamechat(&gamepad,
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
#ifdef ADAPTER_SWITCH2_AUDIO
    if (active && headphone_audio_diagnostic_led(now, &led)) {
        // Only changes the GPIO pattern; never sleeps or blocks audio.
    } else
#endif
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

#ifdef ADAPTER_SWITCH2_AUDIO
// USB audio completion must not recursively poll CYW43 or drain Bluetooth.
// Rearming the receive endpoint stays ahead of lightweight audio TX/watchdog.
void headphone_audio_service_io(void) {
    headphone_audio_task();
    if (usb_started) watchdog_update();
}
// Input packet boundaries interrupt a potentially long radio receive batch.
// This also guards the regular main-loop call against nested USB dispatch.
void headphone_audio_service_usb(void) {
    static bool busy;
    if (!usb_started || busy) return;
    busy = true;
    switch_usb_task();
    busy = false;
}

#endif

int main(void) {
#ifdef ADAPTER_SWITCH2_AUDIO
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(1000);
    set_sys_clock_khz(300000,true);
#endif
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
#ifdef ADAPTER_SWITCH2_AUDIO
        headphone_audio_service_usb();
#endif
        cyw43_arch_poll();
#ifdef ADAPTER_SWITCH2_AUDIO
        headphone_audio_task();
#endif
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (active && continuous_reports && !inputs_fresh(now) && !neutralized) {
            switch_state_t neutral;
            switch_state_neutral(&neutral);
            switch_usb_set_state(&neutral);
            switch_usb_stop_motion();
            rumble_stop();
            neutralized = true;
        }
#ifdef ADAPTER_SWITCH2_AUDIO
        headphone_audio_service_usb();
#else
        if (usb_started) switch_usb_task();
#endif
        uint8_t weak, strong;
        if (active && rumble_take(now,&weak,&strong) && active->report_parser.play_dual_rumble) {
            if (!inputs_fresh(now)) weak=strong=0;
#ifdef ADAPTER_SWITCH2_AUDIO
            if (!headphone_audio_rumble(active,weak,strong))
#endif
                active->report_parser.play_dual_rumble(active,0,(weak || strong) ? 150 : 0,weak,strong);
        }
#ifdef ADAPTER_SWITCH2_AUDIO
        headphone_audio_task();
#endif
        if (active && rgb_supported) {
            bool ready = true;
#ifdef ADAPTER_SWITCH2_AUDIO
            ready = headphone_audio_lightbar_ready(active);
#endif
            uint8_t r,g,b;
            if (ready && controller_rgb_next(&rgb, now, &r, &g, &b))
                active->report_parser.set_lightbar_color(active, r, g, b);
        }
        update_led();
        if (usb_started) watchdog_update();
        sleep_us(100);
    }
    return 0;
}
