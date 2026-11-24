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
 *
 * 框架级配置项：亮度/配色/语言/语音提示/触摸反馈音。
 * 应用自有配置建议另开 NVS namespace，或在本文件扩展字段（仿照现有条目）。
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t brightness;   /* 背光亮度 30..255 */
    uint8_t theme;        /* 配色模式: 0=深色 1=亮色 (ui_theme_mode_t) */
    uint8_t language;     /* 界面语言: 0=中文 1=English (app_lang_t) */
    uint8_t rotation;     /* 屏幕方向: 0=竖屏 1=横屏90° 2=竖屏180° 3=横屏270°（显示与触摸同步，重启生效） */
    uint8_t voice;        /* 语音提示开关 */
    uint8_t touch_sound;  /* 触摸反馈音开关 */
    uint8_t volume;       /* 扬声器音量 0..100（ES8311 DAC，全局生效） */
} app_settings_t;

/** 载入配置（首次调用从 NVS 读取并钳位，之后幂等） */
esp_err_t settings_init(void);

/** 只读快照（返回内部指针，勿长期持有） */
const app_settings_t *settings_get(void);
/* Last persisted save result; previews do not change it. */
esp_err_t settings_last_save_error(void);

void settings_apply_brightness(uint8_t level); /* 拖动预览：内存+背光，不写 NVS */
void settings_set_brightness(uint8_t level);   /* 落盘（松手时调用一次） */
void settings_set_theme(uint8_t mode);         /* 运行时切换由 UI 层应用 */
void settings_set_language(uint8_t lang);      /* 运行时切换由 UI 层应用 */
void settings_set_rotation(uint8_t mode);      /* 0..3（×90°），重启后生效 */
void settings_set_voice(uint8_t enable);
void settings_set_touch_sound(uint8_t enable);
void settings_apply_volume(uint8_t vol);   /* 拖动预览：内存+DAC，不写 NVS */
void settings_set_volume(uint8_t vol);     /* 落盘（松手时调用一次） */

#ifdef __cplusplus
}
#endif
