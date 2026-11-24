/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 应用词条（中→英词典）
 *
 * 框架内置词条见 Common/i18n.c 的 s_fw_dict；本文件只放应用自有词条，
 * App_Init 里 i18n_register() 注册（先注册先命中，可覆盖内置）。
 * 规则：格式串替换符两侧一致；LV_SYMBOL_* 用宏拼接。
 */
#pragma once

#include <stddef.h>

#include "i18n.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const i18n_dict_entry_t g_app_i18n_entries[];
extern const size_t g_app_i18n_count;

#ifdef __cplusplus
}
#endif
