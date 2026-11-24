/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 桥事件泵实现
 */
#include "ui_events.h"

#include "lvgl.h"

#include "data_bridge.h"

#define UI_EVT_MAX_SUBS 6

typedef struct {
    ui_event_cb_t cb;
    void *user_data;
} ui_evt_sub_t;

static ui_evt_sub_t s_subs[UI_EVT_MAX_SUBS];
static bool s_inited = false;

static void ui_evt_pump_cb(lv_timer_t *t)
{
    (void)t;
    /* 每拍最多消化 8 条（队列容量），防单拍长占 LVGL */
    for (int i = 0; i < 8; i++) {
        bridge_evt_t e = bridge_poll_event();
        if (e.id == EVT_NONE) {
            break;
        }
        /* 逆订阅序分发：后订阅者优先 —— 页面(后注册)先于 App.c 全局兜底(先注册)，
         * 页面消费(返回 true)后全局不再重复提示 */
        for (int s = UI_EVT_MAX_SUBS - 1; s >= 0; s--) {
            if (s_subs[s].cb && s_subs[s].cb(e.id, e.arg, s_subs[s].user_data)) {
                break;   /* 已消费，不再向后续订阅者分发 */
            }
        }
    }
}

void ui_events_init(void)
{
    if (s_inited) {
        return;
    }
    s_inited = true;
    lv_timer_create(ui_evt_pump_cb, 200, NULL);
}

bool ui_events_subscribe(ui_event_cb_t cb, void *user_data)
{
    if (!cb) {
        return false;
    }
    for (int i = 0; i < UI_EVT_MAX_SUBS; i++) {
        if (!s_subs[i].cb) {
            s_subs[i].cb = cb;
            s_subs[i].user_data = user_data;
            return true;
        }
    }
    return false;
}

void ui_events_unsubscribe(ui_event_cb_t cb)
{
    if (!cb) {
        return;
    }
    for (int i = 0; i < UI_EVT_MAX_SUBS; i++) {
        if (s_subs[i].cb == cb) {
            s_subs[i].cb = NULL;
            s_subs[i].user_data = NULL;
        }
    }
}
