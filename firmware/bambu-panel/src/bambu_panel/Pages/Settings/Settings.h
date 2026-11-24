/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 系统设置页 Presenter
 *
 * WiFi 连接（凭据保存于 NVS，重启自动连接）、显示/语言/声音、重启。
 */
#pragma once

#include "Page.h"

extern const page_t page_settings;

/** 供 WiFi 扫描页回填已选热点（连接成功后设置页输入框同步） */
void settings_wifi_fill_fields(const char *ssid, const char *pass);
