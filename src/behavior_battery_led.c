/*
 * Copyright (c) 2026 Cerbekos
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define DT_DRV_COMPAT zmk_behavior_battery_led

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/battery.h>
#include <zmk/rgb_underglow.h>

#include "gridaria_led.h"

LOG_MODULE_REGISTER(gridaria_battery_led, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)
#define BATTERY_LED_BRIGHTNESS 127

BUILD_ASSERT(STRIP_NUM_PIXELS > 0, "Battery indicator requires at least one LED");

static const struct device *const led_strip = DEVICE_DT_GET(STRIP_CHOSEN);
static struct led_rgb pixels[STRIP_NUM_PIXELS];
static bool display_active;
static bool underglow_was_on;

bool gridaria_battery_led_is_active(void) { return display_active; }

static int update_pixels(void) {
    int err = led_strip_update_rgb(led_strip, pixels, ARRAY_SIZE(pixels));
    if (err < 0) {
        LOG_ERR("Failed to update battery indicator LEDs: %d", err);
    }
    return err;
}

static void clear_pixels(void) {
    for (size_t i = 0; i < ARRAY_SIZE(pixels); i++) {
        pixels[i] = (struct led_rgb){.r = 0, .g = 0, .b = 0};
    }
}

static int battery_led_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    if (display_active) {
        LOG_WRN("Battery indicator is already active");
        return -EBUSY;
    }
    if (!device_is_ready(led_strip)) {
        LOG_ERR("Underglow LED strip is not ready");
        return -ENODEV;
    }

    int err = zmk_rgb_underglow_get_state(&underglow_was_on);
    if (err) {
        LOG_ERR("Failed to read underglow state: %d", err);
        return err;
    }

    if (underglow_was_on) {
        err = zmk_rgb_underglow_off();
        if (err) {
            LOG_ERR("Failed to pause underglow for battery indicator: %d", err);
            return err;
        }
    }

    uint8_t charge = zmk_battery_state_of_charge();
    if (charge > 100) {
        charge = 100;
    }
    size_t lit_pixels = (charge * ARRAY_SIZE(pixels) + 99) / 100;
    for (size_t i = 0; i < ARRAY_SIZE(pixels); i++) {
        bool on = i < lit_pixels;
        pixels[i] = (struct led_rgb){
            .r = on ? BATTERY_LED_BRIGHTNESS : 0,
            .g = on ? BATTERY_LED_BRIGHTNESS : 0,
            .b = on ? BATTERY_LED_BRIGHTNESS : 0,
        };
    }

    err = update_pixels();
    if (err) {
        if (underglow_was_on) {
            int restore_err = zmk_rgb_underglow_on();
            if (restore_err) {
                LOG_ERR("Failed to restore underglow after battery indicator error: %d",
                        restore_err);
            }
        }
        return err;
    }

    display_active = true;
    return 0;
}

static int battery_led_binding_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    if (!display_active) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    display_active = false;
    if (underglow_was_on) {
        int err = zmk_rgb_underglow_on();
        if (err) {
            LOG_ERR("Failed to restore underglow after battery indicator: %d", err);
            return err;
        }
        gridaria_layer_led_refresh();
        return 0;
    }

    clear_pixels();
    return update_pixels();
}

static const struct behavior_driver_api battery_led_driver_api = {
    .binding_pressed = battery_led_binding_pressed,
    .binding_released = battery_led_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &battery_led_driver_api);
