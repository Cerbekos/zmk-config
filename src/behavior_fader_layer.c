/*
 * Copyright (c) 2026 Cerbekos
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define DT_DRV_COMPAT zmk_behavior_fader_layer

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>

static int fader_layer_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_TRANSPARENT;
}

static int fader_layer_binding_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_TRANSPARENT;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata layer_values[] = {{
    .display_name = "Fader target layer (0-6)",
    .range = {.min = 0, .max = 6},
    .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
}};

static const struct behavior_parameter_metadata_set parameter_sets[] = {{
    .param1_values_len = ARRAY_SIZE(layer_values),
    .param1_values = layer_values,
}};

static const struct behavior_parameter_metadata parameter_metadata = {
    .sets_len = ARRAY_SIZE(parameter_sets),
    .sets = parameter_sets,
};

#endif

static const struct behavior_driver_api fader_layer_driver_api = {
    .binding_pressed = fader_layer_binding_pressed,
    .binding_released = fader_layer_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &parameter_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &fader_layer_driver_api);
