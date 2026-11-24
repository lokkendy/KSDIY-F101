/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 桥事件泵（bridge 事件队列 → LVGL 上下文分发）
 *
 * 生产者（HAL/工作线程）bridge_post_event()；本泵 200ms 轮询取出并按
 * 订阅顺序分发。订阅回调返回 true = 已消费，停止向后续订阅者分发
 * （页面级订阅者注册在前，优先于 App.c 的全局兜底 toast）。
 *
 * 订阅/退订在 LVGL 上下文调用（页面 create/destroy 中）；
 * 不初始化本模块时事件仅滞留 8 深环形（满丢最旧），无副作用。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 返回 true = 已消费该事件，停止向后续订阅者分发 */
typedef bool (*ui_event_cb_t)(uint8_t id, int16_t arg, void *user_data);

/** 创建泵定时器（LVGL 上下文调用一次；重复调用幂等） */
void ui_events_init(void);

/** 订阅事件（最多 6 个订阅者），false = 已满 */
bool ui_events_subscribe(ui_event_cb_t cb, void *user_data);

/** 退订（清除所有匹配 cb 的槽位） */
void ui_events_unsubscribe(ui_event_cb_t cb);

#ifdef __cplusplus
}
#endif
