/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 首页 Presenter
 *
 * 生命周期 + Model→View 定时刷新 + 导航事件。
 * 作为新页面的参考样板：改本目录三件套即可做出第一个业务页。
 */
#pragma once

#include "Page.h"

extern const page_t page_home;

/** 当前首页在栈顶且对象有效时刷新 Model→View（全局同步定时器调用） */
void home_ui_refresh(void);

void home_open_fleet(void);

void home_open_media(bool camera);
