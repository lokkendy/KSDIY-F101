/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 系统设置页 Model 实现
 */
#include "SettingsModel.h"

#include <string.h>

#include "esp_system.h"

#include "HAL.h"
#include "i18n.h"
#include "app_settings.h"
#include "data_bridge.h"
#include "ui_common.h"
#include "ui_theme.h"
#include "page_manager.h"

static void show_save_error(void)
{
    if(settings_last_save_error()!=ESP_OK)
        ui_toast(tr("本次设置仅临时生效，保存失败，请重试"),UI_TOAST_ERROR);
}


/* ── WiFi 凭据 ── */

bool SettingsModel_LoadWifiCreds(char *ssid, size_t ssid_size,
                                 char *pass, size_t pass_size)
{
    return HAL_WiFi_LoadCreds(ssid, ssid_size, pass, pass_size);
}

void SettingsModel_SaveWifiCreds(const char *ssid, const char *pass)
{
    HAL_WiFi_SaveCreds(ssid, pass);
}

void SettingsModel_ClearWifiCreds(void)
{
    HAL_WiFi_ClearCreds();
}

void SettingsModel_ConnectWifiAsync(const char *ssid, const char *pass)
{
    HAL_WiFi_ConnectAsync(ssid, pass);
}

void SettingsModel_DisconnectWifi(void)
{
    HAL_WiFi_Disconnect();
}

/* ── 显示 / 声音 ── */

void SettingsModel_SetBrightness(uint8_t level)
{
    settings_set_brightness(level);
    show_save_error();
}

void SettingsModel_ApplyBrightness(uint8_t level)
{
    settings_apply_brightness(level);
}

uint8_t SettingsModel_GetBrightness(void)
{
    return settings_get()->brightness;
}

void SettingsModel_SetVolume(uint8_t vol)
{
    settings_set_volume(vol);
    show_save_error();
}

void SettingsModel_ApplyVolume(uint8_t vol)
{
    settings_apply_volume(vol);
}

uint8_t SettingsModel_GetVolume(void)
{
    return settings_get()->volume;
}

void SettingsModel_SetLanguage(uint8_t lang)
{
    settings_set_language(lang);
    show_save_error();
}

uint8_t SettingsModel_GetLanguage(void)
{
    return settings_get()->language;
}

void SettingsModel_SetTheme(uint8_t theme)
{
    settings_set_theme(theme);
    show_save_error();
}

uint8_t SettingsModel_GetTheme(void)
{
    return settings_get()->theme;
}

void SettingsModel_SetRotation(uint8_t mode)
{
    settings_set_rotation(mode);
    show_save_error();
}

uint8_t SettingsModel_GetRotation(void)
{
    return settings_get()->rotation;
}

void SettingsModel_SetVoice(bool enable)
{
    settings_set_voice(enable ? 1 : 0);
    show_save_error();
}

bool SettingsModel_GetVoice(void)
{
    return settings_get()->voice != 0;
}

void SettingsModel_SetTouchSound(bool enable)
{
    settings_set_touch_sound(enable ? 1 : 0);
    show_save_error();
}

bool SettingsModel_GetTouchSound(void)
{
    return settings_get()->touch_sound != 0;
}

/* ── 系统 ── */

void SettingsModel_Reboot(void)
{
    bridge_add_log(tr("[SYS] 用户重启设备"));
    esp_restart();
}

/* ── 快照 ── */

void SettingsModel_GetNetSnapshot(settings_net_snapshot_t *out)
{
    if (!out) return;

    app_bridge_t b;
    bridge_get(&b);

    out->wifi_connected = b.wifi_connected;
    strlcpy(out->wifi_ssid, b.ssid, sizeof(out->wifi_ssid));
    strlcpy(out->wifi_ip, b.ip_str, sizeof(out->wifi_ip));
    strlcpy(out->device_id, b.device_id, sizeof(out->device_id));
}
