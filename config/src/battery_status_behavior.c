// app/behaviors/battery_status_behavior.c

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/text/type.h> // 文字出力用API

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static const struct device *battery_dev = DEVICE_DT_GET(DT_INST(0, zephyr_battery));

static int on_key_pressed(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    if (!device_is_ready(battery_dev)) {
        LOG_WRN("Battery device not ready");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    struct sensor_value battery_level;
    int rc = sensor_sample_fetch(battery_dev);
    if (rc < 0) {
        LOG_ERR("Failed to fetch battery sample: %d", rc);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    rc = sensor_channel_get(battery_dev, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &battery_level);
    if (rc < 0) {
        LOG_ERR("Failed to get battery level: %d", rc);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "Battery: %d%%", battery_level.val1);

    // テキスト出力（HID）
    zmk_text_type_from_encoded_utf8(buf);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_key_released(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_battery_status_driver_api = {
    .binding_pressed = on_key_pressed,
    .binding_released = on_key_released,
};

DEVICE_DEFINE(battery_status_behavior, "BATTERY_STATUS_BEHAVIOR", NULL, NULL, NULL, NULL,
              APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
              &behavior_battery_status_driver_api);
