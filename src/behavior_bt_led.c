/*
 * Copyright (c) 2026 Cerbekos
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define DT_DRV_COMPAT zmk_behavior_bt_led

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/bt.h>
#include <zmk/behavior.h>
#include <zmk/ble.h>
#include <zmk/rgb_underglow.h>
#include <zmk/workqueue.h>

LOG_MODULE_REGISTER(gridaria_bt_led, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)
#define BLINK_COUNT 3
#define BLINK_INTERVAL K_MSEC(250)

BUILD_ASSERT(STRIP_NUM_PIXELS >= 7, "Bluetooth LED feedback requires at least seven LEDs");

static const struct device *const led_strip = DEVICE_DT_GET(STRIP_CHOSEN);
static struct led_rgb pixels[STRIP_NUM_PIXELS];
static uint8_t blink_led;
static uint8_t completed_blinks;
static bool underglow_was_on;
static bool blink_led_is_on;
static bool blink_active;

static void blink_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(blink_work, blink_work_handler);

static int update_pixels(void) {
    int err = led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
    if (err < 0) {
        LOG_ERR("Failed to update Bluetooth indicator LED: %d", err);
    }
    return err;
}

static void set_blink_led(bool on) {
    struct led_rgb background = underglow_was_on
                                    ? (struct led_rgb){.r = 127, .g = 127, .b = 127}
                                    : (struct led_rgb){.r = 0, .g = 0, .b = 0};
    struct led_rgb indicator = on ? (struct led_rgb){.r = 127, .g = 127, .b = 127}
                                  : (struct led_rgb){.r = 0, .g = 0, .b = 0};

    for (size_t i = 0; i < ARRAY_SIZE(pixels); i++) {
        pixels[i] = background;
    }
    pixels[blink_led] = indicator;
    blink_led_is_on = on;
}

static void finish_blink(void) {
    blink_active = false;
    if (underglow_was_on) {
        int err = zmk_rgb_underglow_on();
        if (err) {
            LOG_ERR("Failed to restore underglow after Bluetooth indicator: %d", err);
        }
    }
}

static void blink_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!blink_active) {
        return;
    }

    set_blink_led(!blink_led_is_on);
    int err = update_pixels();
    if (err) {
        finish_blink();
        return;
    }

    if (!blink_led_is_on && ++completed_blinks >= BLINK_COUNT) {
        finish_blink();
        return;
    }

    err = k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &blink_work, BLINK_INTERVAL);
    if (err < 0) {
        LOG_ERR("Failed to schedule Bluetooth indicator blink: %d", err);
        finish_blink();
    }
}

static int start_blink(uint8_t led) {
    bool was_on;
    int err = zmk_rgb_underglow_get_state(&was_on);
    if (err) {
        LOG_ERR("Failed to read underglow state: %d", err);
        return err;
    }
    if (!device_is_ready(led_strip)) {
        LOG_ERR("Underglow LED strip is not ready");
        return -ENODEV;
    }

    if (blink_active) {
        k_work_cancel_delayable(&blink_work);
    } else {
        underglow_was_on = was_on;
        if (underglow_was_on) {
            err = zmk_rgb_underglow_off();
            if (err) {
                LOG_ERR("Failed to pause underglow for Bluetooth indicator: %d", err);
                return err;
            }
        }
        blink_active = true;
    }

    blink_led = led;
    completed_blinks = 0;
    blink_led_is_on = false;
    err = k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &blink_work, K_NO_WAIT);
    if (err < 0) {
        LOG_ERR("Failed to start Bluetooth indicator blink: %d", err);
        finish_blink();
        return err;
    }
    return 0;
}

static int run_bluetooth_command(struct zmk_behavior_binding *binding, uint8_t *led) {
    switch (binding->param1) {
    case BT_SEL_CMD:
        if (binding->param2 >= 5) {
            return -EINVAL;
        }
        *led = binding->param2;
        return zmk_ble_prof_select((uint8_t)binding->param2);
    case BT_CLR_CMD:
        *led = 5;
        zmk_ble_clear_bonds();
        return 0;
    case BT_CLR_ALL_CMD:
        *led = 6;
        zmk_ble_clear_all_bonds();
        return 0;
    default:
        LOG_ERR("Unsupported Bluetooth LED command: %u", binding->param1);
        return -ENOTSUP;
    }
}

static int bt_led_binding_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

    uint8_t led = UINT8_MAX;
    int command_err = run_bluetooth_command(binding, &led);
    int blink_err = led < 7 ? start_blink(led) : 0;

    return command_err ? command_err : blink_err;
}

static int bt_led_binding_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api bt_led_driver_api = {
    .binding_pressed = bt_led_binding_pressed,
    .binding_released = bt_led_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &bt_led_driver_api);
