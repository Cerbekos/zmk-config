#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/battery.h>
#include <logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// ビヘイビアの処理関数
static int bt_status_key_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    int level = zmk_battery_level();
    LOG_INF("🔋 Battery Level: %d%%", level);
    return ZMK_BEHAVIOR_OPAQUE;
}

// ビヘイビア定義マクロ（ラベル名と紐づけ）
ZMK_BEHAVIOR(bt_status, bt_status_key_pressed, NULL);