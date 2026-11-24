/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 全局触摸按键反馈音
 *
 * 为所有指针输入设备挂 PRESSED 回调 → 播放短促点击音。
 * App_Init 里 LVGL 就绪后调用一次（ui_touch_feedback_init）。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 给所有 POINTER 输入设备挂按下反馈音回调（幂等，重复调用会重复挂） */
void ui_touch_feedback_init(void);

#ifdef __cplusplus
}
#endif
