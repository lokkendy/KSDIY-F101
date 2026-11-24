/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 应用语音表（Edge-TTS 16k PCM, ES8311 播放）
 *
 * 播放引擎在 Model/voice.c（voice_init 注入下方表 + 条目数），
 * 播报语言跟随 UI 语言实时选择 zh/en 表。
 *
 * 音频数据在 app_voice_data.c（tools/gen_voice.py 生成）：
 *   python tools/gen_voice.py          # 重新生成真实语音
 * 模板默认占位表（空条目 = 静音跳过），不阻塞编译。
 * 本头只依赖纯 C 类型：Pages 与模拟器桩均可直接包含。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "voice.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_VOICE_READY = 0,   /* 设备已就绪 / Device ready */
    APP_VOICE_WIFI_OK,     /* WiFi 已连接 / WiFi connected */
    APP_VOICE_WIFI_FAIL,   /* WiFi 连接失败 / WiFi connect failed */
    APP_VOICE_DONE,        /* 操作完成 / Done */
    APP_VOICE_ERROR,       /* 操作失败 / Operation failed */
    APP_VOICE_ID_COUNT,
} app_voice_id_t;

extern const voice_clip_t g_app_voice_zh[APP_VOICE_ID_COUNT];
extern const voice_clip_t g_app_voice_en[APP_VOICE_ID_COUNT];

#ifdef __cplusplus
}
#endif
