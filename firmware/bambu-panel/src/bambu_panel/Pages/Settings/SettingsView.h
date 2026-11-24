/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 系统设置页 View（纯控件）
 */
#pragma once

#include "Page.h"

typedef struct {
    lv_obj_t *root;

    /* WiFi 卡 */
    lv_obj_t *txt_ssid;
    lv_obj_t *txt_pass;
    lv_obj_t *lbl_wifi_state;
    lv_obj_t *lbl_wifi_ip;

    /* 显示与声音 */
    lv_obj_t *lbl_bri_pct;   /* 亮度百分数（拖动实时刷新） */
    lv_obj_t *lbl_vol_pct;   /* 音量百分数 */
    lv_obj_t *dd_rot;        /* 屏幕方向下拉（语言切换重设选项文案） */

    /* 设备信息卡 */
    lv_obj_t *lbl_devid;
} SettingsView_t;

void SettingsView_Create(SettingsView_t *v, lv_obj_t *parent,
                         const char *saved_ssid, const char *saved_pass,
                         uint8_t brightness, uint8_t language, uint8_t theme,
                         uint8_t rotation, bool voice_on, bool touch_on,
                         uint8_t volume,
                         lv_event_cb_t on_scan, lv_event_cb_t on_connect,
                         lv_event_cb_t on_disconnect, lv_event_cb_t on_brightness,
                         lv_event_cb_t on_language, lv_event_cb_t on_theme,
                         lv_event_cb_t on_rotation,
                         lv_event_cb_t on_voice, lv_event_cb_t on_touch_sound,
                         lv_event_cb_t on_volume,
                         lv_event_cb_t on_reboot, lv_event_cb_t on_shutdown);
