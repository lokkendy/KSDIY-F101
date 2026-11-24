/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 应用外壳配置（Tab 表 + 状态栏条目）
 *
 * 应用在这里声明"外壳长什么样"：
 *  - Tab 表：底部导航（NULL = 无 Tab 单栈模式，改用 root_page）
 *  - 状态栏：左侧品牌区 / 右侧图标时钟区，按 sb_item_t 表构建
 * 新增页面 = 写 Pages/XxxPage + 在这里挂进 Tab 表。
 */
#pragma once

#include "page_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 应用外壳配置（pm_shell_init 用） */
extern const pm_shell_cfg_t g_app_shell_cfg;

#ifdef __cplusplus
}
#endif
