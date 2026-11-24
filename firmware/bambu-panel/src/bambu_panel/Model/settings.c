/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 本地用户配置（NVS 持久化；模拟器走内存桩）
 */
#include "app_settings.h"
#include "ksdiy_rotation.h"

#include <string.h>

#include "HAL_Display.h"
#include "HAL_Audio.h"
#include "esp_log.h"
#include "nvs.h"
#include "ui_screen.h"

static const char *TAG = "settings";
static const char *NS = "app_cfg";

#define KEY_BRIGHTNESS  "bright"
#define KEY_THEME       "theme"
#define KEY_LANG        "lang"
#define KEY_ROTATION    "rot"
#define KEY_VOICE       "voice"
#define KEY_TOUCH_SOUND "touch_snd"
#define KEY_VOLUME      "vol"

static app_settings_t s_cfg;
static bool s_loaded = false;
static esp_err_t s_last_save_error = ESP_OK;
esp_err_t settings_last_save_error(void) { return s_last_save_error; }

static void apply_defaults(app_settings_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->brightness = 200;
    cfg->theme = 0;
    cfg->language = 0;
    cfg->voice = 1;
    cfg->touch_sound = 1;
    cfg->volume = 70;   /* 与 HAL_Audio 默认值一致（AUDIO_DEFAULT_VOLUME） */
}

static void load_from_nvs(app_settings_t *cfg)
{
    apply_defaults(cfg);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }

    uint8_t u8 = 0;
    if (nvs_get_u8(h, KEY_BRIGHTNESS, &u8) == ESP_OK) {
        if (u8 >= 30 && u8 <= 255) {
            cfg->brightness = u8;
        }
    }

    (void)nvs_get_u8(h, KEY_THEME, &cfg->theme);
    if (cfg->theme > 1) {
        cfg->theme = 1;
    }

    (void)nvs_get_u8(h, KEY_LANG, &cfg->language);
    if (cfg->language > 1) {
        cfg->language = 0;
    }

    (void)nvs_get_u8(h, KEY_ROTATION, &cfg->rotation);
    if (cfg->rotation > 3) {
        cfg->rotation = 0;
    }

    (void)nvs_get_u8(h, KEY_VOICE, &cfg->voice);
    if (cfg->voice > 1) {
        cfg->voice = 1;
    }

    (void)nvs_get_u8(h, KEY_TOUCH_SOUND, &cfg->touch_sound);
    if (cfg->touch_sound > 1) {
        cfg->touch_sound = 1;
    }

    (void)nvs_get_u8(h, KEY_VOLUME, &cfg->volume);
    if (cfg->volume > 100) {
        cfg->volume = 70;
    }

    nvs_close(h);
}

static esp_err_t save_all(void)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        s_last_save_error=ret;
        return ret;
    }

    ret = nvs_set_u8(h, KEY_BRIGHTNESS, s_cfg.brightness);
    if (ret == ESP_OK) ret = nvs_set_u8(h, KEY_THEME, s_cfg.theme);
    if (ret == ESP_OK) ret = nvs_set_u8(h, KEY_LANG, s_cfg.language);
    if (ret == ESP_OK) ret = nvs_set_u8(h, KEY_ROTATION, s_cfg.rotation);
    if (ret == ESP_OK) ret = nvs_set_u8(h, KEY_VOICE, s_cfg.voice);
    if (ret == ESP_OK) ret = nvs_set_u8(h, KEY_TOUCH_SOUND, s_cfg.touch_sound);
    if (ret == ESP_OK) ret = nvs_set_u8(h, KEY_VOLUME, s_cfg.volume);
    if (ret == ESP_OK) ret = nvs_commit(h);

    nvs_close(h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(ret));
    }
    s_last_save_error=ret;
    return ret;
}

esp_err_t settings_init(void)
{
    if (s_loaded) {
        return ESP_OK;
    }

    load_from_nvs(&s_cfg);
    /* Old rot entries were never applied on F101. Use the shared, validated
     * orientation record; absent record preserves the former landscape. */
    s_cfg.rotation = ksdiy_rotation_pending();
    s_loaded = true;

    /* 音量在音频链路初始化前设定即可：HAL 缓存该值，codec 打开后统一套用 */
    HAL_Audio_SetVolume(s_cfg.volume);

    ESP_LOGI(TAG, "loaded: bright=%u theme=%u lang=%u rot=%u voice=%u touch=%u vol=%u",
             s_cfg.brightness, s_cfg.theme, s_cfg.language, s_cfg.rotation,
             s_cfg.voice, s_cfg.touch_sound, s_cfg.volume);
    return ESP_OK;
}

const app_settings_t *settings_get(void)
{
    return &s_cfg;
}

/** 拖动中即时预览：只改内存 + 背光，不写 NVS（松手由 set 落盘） */
void settings_apply_brightness(uint8_t level)
{
    if (level < 30) level = 30;
    if (level > 255) level = 255;
    s_cfg.brightness = level;
    HAL_Display_SetBrightness(level);
}

void settings_set_brightness(uint8_t level)
{
    settings_apply_brightness(level);
    save_all();
}

void settings_set_theme(uint8_t mode)
{
    /* 运行时切换由 UI 层（ui_theme_set_mode + pm_reload_shell）应用 */
    s_cfg.theme = (mode <= 1) ? mode : 1;
    save_all();
}

void settings_set_language(uint8_t lang)
{
    /* 运行时切换由 UI 层（i18n_set_lang + pm_reload）应用 */
    s_cfg.language = (lang <= 1) ? lang : 0;
    save_all();
}

void settings_set_rotation(uint8_t mode)
{
    if(mode>3 || ksdiy_rotation_save(mode)!=0) {
        s_last_save_error=ESP_FAIL;
        ESP_LOGE(TAG, "screen rotation was not saved");
        return;
    }
    s_cfg.rotation = mode;
    save_all();
}

void settings_set_voice(uint8_t enable)
{
    s_cfg.voice = (enable != 0) ? 1 : 0;
    save_all();
}

void settings_set_touch_sound(uint8_t enable)
{
    s_cfg.touch_sound = (enable != 0) ? 1 : 0;
    save_all();
}

/** 拖动中即时预览：只改内存 + DAC 音量，不写 NVS（松手由 set 落盘） */
void settings_apply_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    s_cfg.volume = vol;
    HAL_Audio_SetVolume(vol);
}

void settings_set_volume(uint8_t vol)
{
    settings_apply_volume(vol);
    save_all();
}
