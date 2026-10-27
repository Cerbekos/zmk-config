/*
 * Copyright (c) 2026 Cerbekos
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

LOG_MODULE_REGISTER(gridaria_layer_led, CONFIG_ZMK_LOG_LEVEL);

#define RGB_EFFECT_SOLID 0
#define RGB_EFFECT_SWIRL 3

/* Indexed by keymap layer ID: 0 is rainbow, 1-7 are red, blue, green, yellow, violet, cyan, white. */
static const struct zmk_led_hsb layer_colors[] = {
    {.h = 0, .s = 100, .b = 50},
    {.h = 0, .s = 100, .b = 50},
    {.h = 240, .s = 100, .b = 50},
    {.h = 120, .s = 100, .b = 50},
    {.h = 60, .s = 100, .b = 50},
    {.h = 270, .s = 100, .b = 50},
    {.h = 180, .s = 100, .b = 50},
    {.h = 0, .s = 0, .b = 50},
};

static uint8_t indicated_layer = ZMK_KEYMAP_LAYER_ID_INVAL;

static int select_rainbow(void) {
    int err = zmk_rgb_underglow_set_hsb(layer_colors[0]);
    if (err) {
        return err;
    }

    return zmk_rgb_underglow_select_effect(RGB_EFFECT_SWIRL);
}

static void show_layer_color(uint8_t layer) {
    if (layer == indicated_layer) {
        return;
    }

    int err = zmk_rgb_underglow_on();
    if (err) {
        LOG_ERR("Failed to enable underglow: %d", err);
        return;
    }

    if (layer == 0 || layer >= ARRAY_SIZE(layer_colors)) {
        err = select_rainbow();
    } else {
        err = zmk_rgb_underglow_set_hsb(layer_colors[layer]);
        if (!err) {
            err = zmk_rgb_underglow_select_effect(RGB_EFFECT_SOLID);
        }
    }

    if (err) {
        LOG_ERR("Failed to set underglow for layer %u: %d", layer, err);
        return;
    }
    indicated_layer = layer;
}

static int gridaria_layer_led_listener(const zmk_event_t *eh) {
    if (as_zmk_layer_state_changed(eh) == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_keymap_layer_index_t layer_index = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t layer = zmk_keymap_layer_index_to_id(layer_index);
    show_layer_color(layer);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(gridaria_layer_led, gridaria_layer_led_listener);
ZMK_SUBSCRIPTION(gridaria_layer_led, zmk_layer_state_changed);
