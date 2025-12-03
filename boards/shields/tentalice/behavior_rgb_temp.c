#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/rgb_underglow.h>

static uint8_t saved_effect;
static struct zmk_led_hsb saved_color;

static int rgb_temp_press(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    // 現在の状態を保存
    saved_effect = zmk_rgb_underglow_get_effect();
    saved_color = zmk_rgb_underglow_get_color();

    // 一時的に Solid に変更
    zmk_rgb_underglow_set_effect(0); // Solid
    struct zmk_led_hsb new_color = {
        .h = 0, .s = 0, .v = 0
    };
    zmk_rgb_underglow_set_color(new_color);

    return 0;
}

static int rgb_temp_release(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    // 保存しておいた状態を復元
    zmk_rgb_underglow_set_effect(saved_effect);
    zmk_rgb_underglow_set_color(saved_color);
    return 0;
}

// ビヘイビア定義
ZMK_BEHAVIOR_DEFINE(rgb_temp,
    behavior_rgb_temp,
    rgb_temp_press,
    rgb_temp_release);