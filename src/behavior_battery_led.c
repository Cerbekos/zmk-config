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
#include <zmk/workqueue.h>

#include "gridaria_led.h"

LOG_MODULE_REGISTER(gridaria_battery_led, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)
#define BATTERY_LED_BRIGHTNESS 127

BUILD_ASSERT(STRIP_NUM_PIXELS > 0, "Battery indicator requires at least one LED");

static const struct device *const led_strip = DEVICE_DT_GET(STRIP_CHOSEN);
static struct led_rgb pixels[STRIP_NUM_PIXELS];
static bool display_active;
static bool display_requested;
static bool underglow_was_on;

bool gridaria_battery_led_is_active(void) { return display_active; }

static void battery_led_work_handler(struct k_work *work);

K_WORK_DEFINE(battery_led_work, battery_led_work_handler);

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

static void show_battery_level(void) {
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

    int err = update_pixels();
    if (err) {
        display_requested = false;
    }
}

static void restore_underglow(void) {
    if (underglow_was_on) {
        int err = zmk_rgb_underglow_on();
        if (err) {
            LOG_ERR("Failed to restore underglow after battery indicator: %d", err);
        }
        display_active = false;
        if (!err) {
            gridaria_layer_led_refresh();
        }
    } else {
        clear_pixels();
        update_pixels();
        display_active = false;
    }

    underglow_was_on = false;
}

static void battery_led_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (display_requested) {
        show_battery_level();
    }

    if (!display_requested && display_active) {
        restore_underglow();
    }
}

static int battery_led_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    if (display_requested) {
        return ZMK_BEHAVIOR_OPAQUE;
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

    display_active = true;
    display_requested = true;
    if (underglow_was_on) {
        /* Queue the display after the asynchronous underglow-off update. */
        err = zmk_rgb_underglow_off();
        if (err) {
            display_requested = false;
            display_active = false;
            LOG_ERR("Failed to pause underglow for battery indicator: %d", err);
            return err;
        }
    }

    err = k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_led_work);
    if (err < 0) {
        display_requested = false;
        restore_underglow();
        LOG_ERR("Failed to schedule battery indicator: %d", err);
        return err;
    }
    return 0;
}

static int battery_led_binding_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    if (!display_requested) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    display_requested = false;
    int err = k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_led_work);
    if (err < 0) {
        LOG_ERR("Failed to schedule battery indicator cleanup: %d", err);
        return err;
    }
    return 0;
}

static const struct behavior_driver_api battery_led_driver_api = {
    .binding_pressed = battery_led_binding_pressed,
    .binding_released = battery_led_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &battery_led_driver_api);
