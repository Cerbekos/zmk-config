/*
 * Copyright (c) 2026 Cerbekos
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

LOG_MODULE_REGISTER(gridaria_fader, CONFIG_ZMK_LOG_LEVEL);

#define FADER_SAMPLE_COUNT 4
#define FADER_HYSTERESIS 12
#define FADER_LED_DISPLAY_MS 1000
#define RGB_EFFECT_SPECTRUM 2
#define RGB_EFFECT_SOLID 0

static const uint16_t fader_boundaries[] = {153, 306, 460, 563, 716, 869};
static const uint8_t fader_layers[] = {3, 2, 1, 0, 4, 5, 6};
static const struct zmk_led_hsb fader_layer_colors[] = {
    {.h = 0, .s = 100, .b = 50},
    {.h = 0, .s = 100, .b = 50},
    {.h = 240, .s = 100, .b = 50},
    {.h = 120, .s = 100, .b = 50},
    {.h = 60, .s = 100, .b = 50},
    {.h = 270, .s = 100, .b = 50},
    {.h = 30, .s = 100, .b = 50},
};
static uint16_t fader_samples[FADER_SAMPLE_COUNT];
static uint32_t fader_sample_sum;
static uint8_t fader_sample_index;
static uint8_t fader_layer_index = 3;
static bool fader_initialized;
static bool fader_layer_applied;

static void restore_fader_leds(struct k_work *work) {
    ARG_UNUSED(work);

    int err = zmk_rgb_underglow_select_effect(RGB_EFFECT_SPECTRUM);
    if (err) {
        LOG_ERR("Failed to restore rainbow underglow effect: %d", err);
    }
}

K_WORK_DELAYABLE_DEFINE(fader_led_restore_work, restore_fader_leds);

static void show_fader_layer_color(uint8_t layer_index) {
    int err = zmk_rgb_underglow_on();
    if (err) {
        LOG_ERR("Failed to enable underglow: %d", err);
        return;
    }

    if (fader_layers[layer_index] == 0) {
        k_work_cancel_delayable(&fader_led_restore_work);
        err = zmk_rgb_underglow_select_effect(RGB_EFFECT_SPECTRUM);
    } else {
        err = zmk_rgb_underglow_set_hsb(fader_layer_colors[fader_layers[layer_index]]);
        if (!err) {
            err = zmk_rgb_underglow_select_effect(RGB_EFFECT_SOLID);
        }
    }

    if (err) {
        LOG_ERR("Failed to set underglow color for layer %u: %d", fader_layers[layer_index], err);
        return;
    }

    if (fader_layers[layer_index] == 0) {
        return;
    }

    err = k_work_reschedule(&fader_led_restore_work, K_MSEC(FADER_LED_DISPLAY_MS));
    if (err < 0) {
        LOG_ERR("Failed to schedule underglow restore: %d", err);
    }
}

static void set_fader_layer(uint8_t layer_index) {
    if (layer_index == fader_layer_index && fader_layer_applied) {
        return;
    }

    fader_layer_index = layer_index;
    int err = zmk_keymap_layer_to(fader_layers[layer_index], false);
    if (err) {
        LOG_ERR("Failed to switch fader to layer %u: %d", fader_layers[layer_index], err);
    } else {
        fader_layer_applied = true;
        show_fader_layer_color(layer_index);
    }
}

static void gridaria_fader_input_callback(struct input_event *event, void *user_data) {
    ARG_UNUSED(user_data);

    if (event->type != INPUT_EV_ABS || event->code != INPUT_ABS_X) {
        return;
    }

    uint16_t sample = CLAMP(event->value - 1, 0, 1023);
    if (!fader_initialized) {
        for (uint8_t i = 0; i < FADER_SAMPLE_COUNT; i++) {
            fader_samples[i] = sample;
        }
        fader_sample_sum = sample * FADER_SAMPLE_COUNT;
        fader_initialized = true;
        fader_layer_index = 0;
        while (fader_layer_index < ARRAY_SIZE(fader_boundaries) &&
               sample > fader_boundaries[fader_layer_index] + FADER_HYSTERESIS) {
            fader_layer_index++;
        }
        set_fader_layer(fader_layer_index);
        return;
    }

    fader_sample_sum -= fader_samples[fader_sample_index];
    fader_samples[fader_sample_index] = sample;
    fader_sample_sum += sample;
    fader_sample_index = (fader_sample_index + 1) % FADER_SAMPLE_COUNT;

    uint16_t filtered_sample = fader_sample_sum / FADER_SAMPLE_COUNT;
    if (fader_layer_index > 0 &&
        filtered_sample < fader_boundaries[fader_layer_index - 1] - FADER_HYSTERESIS) {
        set_fader_layer(fader_layer_index - 1);
    } else if (fader_layer_index < ARRAY_SIZE(fader_boundaries) &&
               filtered_sample > fader_boundaries[fader_layer_index] + FADER_HYSTERESIS) {
        set_fader_layer(fader_layer_index + 1);
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(fader)), gridaria_fader_input_callback, NULL);
