/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * 通用应用模板 - 电源菜单（关机 / 重启 / 取消）
 *
 * 两个入口共用：设置页「关机」确认弹窗走 ui_confirm_dialog，
 * 电源键长按（AXP2101 PEK）与各处主动唤起走本菜单。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 打开电源菜单（关机=AXP2101 软关机 / 重启=esp_restart / 取消）。
 * 已有弹窗打开时不叠加。必须在 LVGL 上下文调用
 * （页面事件回调或 ui_events 泵回调均可）。
 */
void ui_power_menu_open(void);
/* Request power off after confirmation, from the LVGL task. */
void ui_power_shutdown(void);

#ifdef __cplusplus
}
#endif
