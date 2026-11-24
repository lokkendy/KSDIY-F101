/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 语音提示播放（仅固件编译；模拟器走 sim_stubs 桩）
 *
 * 语音表由应用注入（app_voice_data.c 生成），框架不持有业务文案；
 * 播放时按界面语言自动选表，开关跟随用户设置（语音提示/触摸反馈音）。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 12 kHz mono G.711 mu-law samples. */
typedef struct {
    const uint8_t *pcm;
    size_t len;
} voice_clip_t;

/**
 * 初始化语音播放（App_Init 中调用一次，幂等）
 *
 * @param zh    中文语音表（中英文表长度必须一致，按 id 索引）
 * @param en    英文语音表（无英文可传 zh，跟随语言只会播同一份）
 * @param count 表长度（也即合法 id 上界）
 */
void voice_init(const voice_clip_t *zh, const voice_clip_t *en, size_t count);

/** 播放一段语音（异步，新请求覆盖旧请求；设置关掉语音提示时静默忽略） */
void voice_play(int id);

/** 播放触摸按键反馈音（设置关掉反馈音时静默忽略） */
void voice_touch_click(void);

#ifdef __cplusplus
}
#endif
