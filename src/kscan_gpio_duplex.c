/*
 * Copyright (c) 2026 Cerbekos
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define DT_DRV_COMPAT zmk_kscan_gpio_duplex

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(kscan_gpio_duplex, CONFIG_ZMK_LOG_LEVEL);

#define ROW_COUNT(inst) DT_INST_PROP_LEN(inst, row_gpios)
#define COL_COUNT(inst) DT_INST_PROP_LEN(inst, col_gpios)
#define KEY_COUNT(inst) (ROW_COUNT(inst) * COL_COUNT(inst) * 2)
#define ROW_GPIO(idx, inst) GPIO_DT_SPEC_GET_BY_IDX(DT_DRV_INST(inst), row_gpios, idx)
#define COL_GPIO(idx, inst) GPIO_DT_SPEC_GET_BY_IDX(DT_DRV_INST(inst), col_gpios, idx)

struct duplex_key_state {
    bool stable;
    bool candidate;
    bool candidate_valid;
    int64_t candidate_since;
};

struct duplex_data {
    const struct device *dev;
    kscan_callback_t callback;
    struct k_work_delayable work;
    struct duplex_key_state *keys;
};

struct duplex_config {
    const struct gpio_dt_spec *rows;
    const struct gpio_dt_spec *cols;
    size_t row_count;
    size_t col_count;
    uint32_t debounce_press_ms;
    uint32_t debounce_release_ms;
    uint32_t poll_period_ms;
};

static int configure_inputs(const struct duplex_config *config) {
    for (size_t i = 0; i < config->row_count; i++) {
        int err = gpio_pin_configure_dt(&config->rows[i], GPIO_INPUT | GPIO_PULL_UP);
        if (err) {
            LOG_ERR("Failed to configure row GPIO %u: %d", config->rows[i].pin, err);
            return err;
        }
    }

    for (size_t i = 0; i < config->col_count; i++) {
        int err = gpio_pin_configure_dt(&config->cols[i], GPIO_INPUT | GPIO_PULL_UP);
        if (err) {
            LOG_ERR("Failed to configure column GPIO %u: %d", config->cols[i].pin, err);
            return err;
        }
    }

    return 0;
}

static int scan_direction(const struct duplex_config *config, const struct gpio_dt_spec *output,
                          size_t output_count, const struct gpio_dt_spec *input,
                          size_t input_count, bool *pressed, bool reverse_columns) {
    for (size_t out = 0; out < output_count; out++) {
        int err = gpio_pin_configure_dt(&output[out], GPIO_OUTPUT_ACTIVE);
        if (err) {
            LOG_ERR("Failed to drive duplex GPIO %u: %d", output[out].pin, err);
            return err;
        }

        k_busy_wait(5);

        for (size_t in = 0; in < input_count; in++) {
            int value = gpio_pin_get_dt(&input[in]);
            if (value < 0) {
                LOG_ERR("Failed to read duplex GPIO %u: %d", input[in].pin, value);
                int restore_err =
                    gpio_pin_configure_dt(&output[out], GPIO_INPUT | GPIO_PULL_UP);
                if (restore_err) {
                    LOG_ERR("Failed to restore duplex GPIO %u to input: %d", output[out].pin,
                            restore_err);
                }
                return value;
            }

            size_t row = reverse_columns ? in : out;
            size_t col = reverse_columns ? out + config->col_count : in;
            pressed[row * (config->col_count * 2) + col] = value != 0;
        }

        err = gpio_pin_configure_dt(&output[out], GPIO_INPUT | GPIO_PULL_UP);
        if (err) {
            LOG_ERR("Failed to restore duplex GPIO %u to input: %d", output[out].pin, err);
            return err;
        }
    }

    return 0;
}

static bool is_unpopulated_key(size_t row, size_t col) {
    return row == 3 && (col == 4 || col == 6);
}

static void update_key_state(const struct device *dev, struct duplex_data *data,
                             const struct duplex_config *config, const bool *pressed,
                             int64_t now) {
    for (size_t row = 0; row < config->row_count; row++) {
        for (size_t col = 0; col < config->col_count * 2; col++) {
            if (is_unpopulated_key(row, col)) {
                continue;
            }

            size_t index = row * (config->col_count * 2) + col;
            struct duplex_key_state *key = &data->keys[index];
            bool current = pressed[index];

            if (current == key->stable) {
                key->candidate_valid = false;
                continue;
            }

            if (!key->candidate_valid || key->candidate != current) {
                key->candidate = current;
                key->candidate_since = now;
                key->candidate_valid = true;
                continue;
            }

            uint32_t debounce_ms =
                current ? config->debounce_press_ms : config->debounce_release_ms;
            if (now - key->candidate_since >= debounce_ms) {
                key->stable = current;
                key->candidate_valid = false;
                data->callback(dev, row, col, current);
            }
        }
    }
}

static void duplex_scan_work(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct duplex_data *data = CONTAINER_OF(delayable, struct duplex_data, work);
    const struct duplex_config *config = data->dev->config;
    bool pressed[40] = {false};

    int err = configure_inputs(config);
    if (!err) {
        err = scan_direction(config, config->rows, config->row_count, config->cols,
                             config->col_count, pressed, false);
    }
    if (!err) {
        err = scan_direction(config, config->cols, config->col_count, config->rows,
                             config->row_count, pressed, true);
    }

    if (err) {
        LOG_ERR("Duplex matrix scan failed: %d", err);
    } else {
        update_key_state(data->dev, data, config, pressed, k_uptime_get());
    }

    k_work_reschedule(&data->work, K_MSEC(config->poll_period_ms));
}

static int duplex_configure(const struct device *dev, kscan_callback_t callback) {
    struct duplex_data *data = dev->data;
    if (callback == NULL) {
        return -EINVAL;
    }
    data->callback = callback;
    return 0;
}

static int duplex_enable(const struct device *dev) {
    struct duplex_data *data = dev->data;
    return k_work_reschedule(&data->work, K_NO_WAIT);
}

static int duplex_disable(const struct device *dev) {
    struct duplex_data *data = dev->data;
    int err = k_work_cancel_delayable(&data->work);
    int input_err = configure_inputs(dev->config);
    return err < 0 ? err : input_err;
}

static int duplex_init(const struct device *dev) {
    const struct duplex_config *config = dev->config;
    struct duplex_data *data = dev->data;
    data->dev = dev;

    if (config->row_count != 4 || config->col_count != 5) {
        LOG_ERR("Duplex matrix requires 4 row GPIOs and 5 column GPIOs");
        return -EINVAL;
    }

    for (size_t i = 0; i < config->row_count; i++) {
        if (!device_is_ready(config->rows[i].port)) {
            LOG_ERR("Row GPIO controller is not ready");
            return -ENODEV;
        }
    }
    for (size_t i = 0; i < config->col_count; i++) {
        if (!device_is_ready(config->cols[i].port)) {
            LOG_ERR("Column GPIO controller is not ready");
            return -ENODEV;
        }
    }

    int err = configure_inputs(config);
    if (err) {
        return err;
    }

    k_work_init_delayable(&data->work, duplex_scan_work);
    return 0;
}

static const struct kscan_driver_api duplex_api = {
    .config = duplex_configure,
    .enable_callback = duplex_enable,
    .disable_callback = duplex_disable,
};

#define DUPLEX_INIT(inst)                                                                          \
    BUILD_ASSERT(ROW_COUNT(inst) == 4, "Gridaria duplex matrix requires four row GPIOs");           \
    BUILD_ASSERT(COL_COUNT(inst) == 5, "Gridaria duplex matrix requires five column GPIOs");        \
    static const struct gpio_dt_spec duplex_rows_##inst[] = {                                      \
        LISTIFY(ROW_COUNT(inst), ROW_GPIO, (,), inst)};                                             \
    static const struct gpio_dt_spec duplex_cols_##inst[] = {                                      \
        LISTIFY(COL_COUNT(inst), COL_GPIO, (,), inst)};                                             \
    static struct duplex_key_state duplex_keys_##inst[KEY_COUNT(inst)];                            \
    static struct duplex_data duplex_data_##inst = {.keys = duplex_keys_##inst};                    \
    static const struct duplex_config duplex_config_##inst = {                                     \
        .rows = duplex_rows_##inst,                                                                \
        .cols = duplex_cols_##inst,                                                                \
        .row_count = ARRAY_SIZE(duplex_rows_##inst),                                                \
        .col_count = ARRAY_SIZE(duplex_cols_##inst),                                                \
        .debounce_press_ms = DT_INST_PROP(inst, debounce_press_ms),                                \
        .debounce_release_ms = DT_INST_PROP(inst, debounce_release_ms),                            \
        .poll_period_ms = DT_INST_PROP(inst, poll_period_ms),                                      \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(inst, duplex_init, NULL, &duplex_data_##inst, &duplex_config_##inst,      \
                          POST_KERNEL, CONFIG_KSCAN_INIT_PRIORITY, &duplex_api);

DT_INST_FOREACH_STATUS_OKAY(DUPLEX_INIT)
