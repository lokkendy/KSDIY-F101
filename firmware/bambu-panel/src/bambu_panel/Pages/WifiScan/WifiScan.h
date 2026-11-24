/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - WiFi 扫描页 Presenter
 *
 * 扫描附近热点、选择 SSID 输入密码并连接（扫描不断开当前 WiFi）。
 */
#pragma once

#include "Page.h"

extern const page_t page_wifi_scan;

/** 入口：从设置页推入扫描页 */
void wifi_scan_page_open(void);
