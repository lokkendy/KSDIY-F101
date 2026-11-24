/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 应用装配层
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define APP_NAME         "KSDIY Bambu Panel"
#define APP_VERSION_STR  "0.3.0"
#define APP_DESC         "Bambu Lab 多机型 LAN MQTT 状态与控制"

/**
 * @brief 应用装配：按启动时序拉起全部子系统（app_main 调用一次）
 *
 * 时序（顺序有依赖，勿随意调换）：
 *  1. settings/NVS → 语言与配色在 UI 创建前生效
 *  2. data_bridge → 全局状态中枢
 *  3. 字体预加载（异步）→ 显示 → 开机动画 → 语音
 *  4. LVGL 锁内建外壳（状态栏 + Tab + 首页）
 *  5. SD 卡（无卡不阻塞）
 *  6. 后台任务：bridge 状态刷新 / 网络（WiFi + SNTP）
 */
void App_Init(void);

#ifdef __cplusplus
}
#endif
