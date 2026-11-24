/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 输入法键盘（覆盖层）
 *
 * 焦点进入 textarea 时弹出底部键盘，点击确定 / 取消或焦点离开时收起。
 * 用法：在 textarea 的 LV_EVENT_FOCUSED / LV_EVENT_DEFOCUSSED 中调用
 *       ui_keyboard_attach(ta) / ui_keyboard_detach(ta)。
 */
#pragma once

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 为输入框注册焦点回调：获得焦点弹键盘、失焦收起 */
void ui_keyboard_bind(lv_obj_t *ta);

/* 弹出键盘并绑定到目标输入框（若已弹出则切换绑定） */
void ui_keyboard_attach(lv_obj_t *ta);

/* 目标输入框失焦时解绑；无其它焦点则收起键盘 */
void ui_keyboard_detach(lv_obj_t *ta);

/* 立即收起键盘（页面销毁时必须调用，键盘挂在顶层图层上不随页面销毁） */
void ui_keyboard_hide(void);

/* 键盘是否处于弹出状态 */
bool ui_keyboard_active(void);

#ifdef __cplusplus
}
#endif
