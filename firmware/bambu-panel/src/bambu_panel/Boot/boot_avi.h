/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 开机 AVI 开机动画(阻塞播放一次)
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 播放开机动画; 文件不存在返回 ESP_ERR_NOT_FOUND, 禁用 Kconfig 时直接 ESP_OK */
esp_err_t boot_avi_play(const char *path);

#ifdef __cplusplus
}
#endif
