/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 系统设置页 Model（settings/NVS 与 HAL 的薄封装）
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool wifi_connected;
    char wifi_ssid[33];
    char wifi_ip[16];
    char device_id[24];
} settings_net_snapshot_t;

/* ── WiFi ── */
bool SettingsModel_LoadWifiCreds(char *ssid, size_t ssid_size,
                                 char *pass, size_t pass_size);
void SettingsModel_SaveWifiCreds(const char *ssid, const char *pass);
void SettingsModel_ClearWifiCreds(void);
void SettingsModel_ConnectWifiAsync(const char *ssid, const char *pass);
void SettingsModel_DisconnectWifi(void);

/* ── 显示 / 声音 ── */
void SettingsModel_SetBrightness(uint8_t level);   /* 落盘 */
void SettingsModel_ApplyBrightness(uint8_t level); /* 拖动预览，不写 NVS */
uint8_t SettingsModel_GetBrightness(void);
void SettingsModel_SetLanguage(uint8_t lang);
uint8_t SettingsModel_GetLanguage(void);
void SettingsModel_SetTheme(uint8_t theme);
uint8_t SettingsModel_GetTheme(void);
void SettingsModel_SetRotation(uint8_t mode);
uint8_t SettingsModel_GetRotation(void);
void SettingsModel_SetVoice(bool enable);
bool SettingsModel_GetVoice(void);
void SettingsModel_SetTouchSound(bool enable);
bool SettingsModel_GetTouchSound(void);
void SettingsModel_SetVolume(uint8_t vol);    /* 落盘 */
void SettingsModel_ApplyVolume(uint8_t vol);  /* 拖动预览，不写 NVS */
uint8_t SettingsModel_GetVolume(void);

/* ── 系统 ── */
void SettingsModel_Reboot(void);

/* ── 快照 ── */
void SettingsModel_GetNetSnapshot(settings_net_snapshot_t *out);
