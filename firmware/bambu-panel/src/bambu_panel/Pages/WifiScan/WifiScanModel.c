/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - WiFi 扫描页 Model 实现
 */
#include "WifiScanModel.h"

#include <string.h>

#include "data_bridge.h"

static wifi_ap_t s_ap_cache[WIFI_SCAN_MAX];
static uint16_t s_ap_count = 0;
static wifi_scan_result_cb_t s_result_cb = NULL;
static void *s_result_ctx = NULL;

/* HAL 扫描回调（WiFi 任务上下文）：先缓存结果，再通知上层 */
static void on_hal_scan_done(const wifi_ap_t *ap_list, uint16_t count, esp_err_t err)
{
    if (err == ESP_OK) {
        s_ap_count = (count > WIFI_SCAN_MAX) ? WIFI_SCAN_MAX : count;
        if (s_ap_count > 0 && ap_list) {
            memcpy(s_ap_cache, ap_list, s_ap_count * sizeof(wifi_ap_t));
        }
    }
    if (s_result_cb) {
        s_result_cb(err == ESP_OK ? s_ap_count : 0, err, s_result_ctx);
    }
}

bool WifiScanModel_IsScanning(void)
{
    return HAL_WiFi_IsScanning();
}

esp_err_t WifiScanModel_StartScan(wifi_scan_result_cb_t cb, void *ctx)
{
    s_result_cb = cb;
    s_result_ctx = ctx;
    return HAL_WiFi_StartScan(on_hal_scan_done);
}

uint16_t WifiScanModel_GetApCount(void)
{
    return s_ap_count;
}

const wifi_ap_t *WifiScanModel_GetApList(void)
{
    return s_ap_cache;
}

const wifi_ap_t *WifiScanModel_GetAp(uint16_t idx)
{
    if (idx >= s_ap_count) return NULL;
    return &s_ap_cache[idx];
}

void WifiScanModel_Connect(const char *ssid, const char *pass)
{
    HAL_WiFi_SaveCreds(ssid, pass ? pass : "");
    HAL_WiFi_ConnectAsync(ssid, pass ? pass : "");
}
