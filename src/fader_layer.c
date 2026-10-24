/*
 * Copyright (c) 2026 Cerbekos
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include <string.h>

LOG_MODULE_REGISTER(gridaria_fader, CONFIG_ZMK_LOG_LEVEL);

#define FADER_SAMPLE_COUNT 4
#define FADER_HYSTERESIS 12
#define FADER_SETTINGS_LAYER 6
#define FADER_LAYER_COUNT 7

static const uint16_t fader_boundaries[] = {153, 306, 460, 563, 716, 869};
static const uint8_t default_fader_layers[FADER_LAYER_COUNT] = {3, 2, 1, 0, 4, 5, 6};
static uint16_t fader_samples[FADER_SAMPLE_COUNT];
static uint32_t fader_sample_sum;
static uint8_t fader_sample_index;
static uint8_t fader_layer_index = 3;
static uint8_t fader_applied_layer = ZMK_KEYMAP_LAYER_ID_INVAL;
static bool fader_initialized;
static bool fader_layer_applied;

static uint8_t fader_target_layer(uint8_t position) {
    const struct zmk_behavior_binding *binding =
        zmk_keymap_get_layer_binding_at_idx(FADER_SETTINGS_LAYER, position);

    if (binding == NULL ||
        binding->behavior_dev == NULL ||
        strcmp(binding->behavior_dev, DEVICE_DT_NAME(DT_NODELABEL(fader_layer))) != 0 ||
        binding->param1 >= FADER_LAYER_COUNT) {
        return default_fader_layers[position];
    }

    return (uint8_t)binding->param1;
}

static void set_fader_layer(uint8_t layer_index) {
    uint8_t layer = fader_target_layer(layer_index);
    if (layer_index == fader_layer_index && layer == fader_applied_layer && fader_layer_applied) {
        return;
    }

    fader_layer_index = layer_index;
    int err = zmk_keymap_layer_to(layer, false);
    if (err) {
        LOG_ERR("Failed to switch fader to layer %u: %d", layer, err);
    } else {
        fader_applied_layer = layer;
        fader_layer_applied = true;
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
    } else {
        set_fader_layer(fader_layer_index);
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(fader)), gridaria_fader_input_callback, NULL);
