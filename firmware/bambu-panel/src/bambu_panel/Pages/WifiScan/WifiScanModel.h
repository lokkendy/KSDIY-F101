/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - WiFi 扫描页 Model（扫描触发 + 热点缓存 + 凭据连接）
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "HAL_WiFi.h"

/** 扫描结果回调（WiFi 任务上下文，更新 UI 需自行持 LVGL 锁） */
typedef void (*wifi_scan_result_cb_t)(uint16_t count, esp_err_t err, void *ctx);

/** 是否正在扫描 */
bool WifiScanModel_IsScanning(void);

/**
 * @brief 启动后台扫描（不断开当前 WiFi）
 * @return ESP_OK 已启动 / 其他 无法启动
 */
esp_err_t WifiScanModel_StartScan(wifi_scan_result_cb_t cb, void *ctx);

/** 最近一次扫描到的热点数 */
uint16_t WifiScanModel_GetApCount(void);

/** 取内部缓存数组（与 GetApCount 配合使用） */
const wifi_ap_t *WifiScanModel_GetApList(void);

/** 取热点条目（越界返回 NULL） */
const wifi_ap_t *WifiScanModel_GetAp(uint16_t idx);

/* ── 连接（结果经 bridge/事件上报） ── */
void WifiScanModel_Connect(const char *ssid, const char *pass);
