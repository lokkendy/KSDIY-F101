/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 页面公共头
 *
 * 每个页面为一个 MVP 三件套目录：
 *   <Name>.c        Presenter —— 生命周期 + 事件接线 + Model→View 刷新
 *   <Name>Model.c   Model     —— 纯数据/业务（bridge 快照、HAL 调用），无 LVGL
 *   <Name>View.c    View      —— 纯控件构建与样式，无业务
 * 简单页面也可以单文件（见 About/）。
 */
#pragma once

#include "page_manager.h"
#include "ui_common.h"
#include "ui_theme.h"
