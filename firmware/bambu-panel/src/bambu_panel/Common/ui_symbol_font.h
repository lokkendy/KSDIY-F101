/*
 * SPDX-FileCopyrightText: 2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * 矢量符号字体（替换 LVGL 内置 FontAwesome 实心字形）
 *
 * 用法：主题字体初始化时把 UI_SYMBOL_FONT(px) 挂到 lv_font_t 的 fallback
 * 链最前面，所有 LV_SYMBOL_* 立刻变成线条图标，页面代码零改动。
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 取指定字号的符号字体（首次调用时注册，之后复用）
 *
 * @param size_px 目标像素字号（8..96；通常传主题字号）
 * @return 字体指针；字号越界返回 NULL
 */
const lv_font_t *ui_symbol_font(int size_px);

/** @brief 该码位是否被本字体接管（调试用） */
bool ui_symbol_font_is_symbol(uint32_t cp);

/**
 * @brief 把符号字体挂到 target 的 fallback 链最前端
 *
 * 目标字体先查自己的字形（中文/ASCII 命中原字体），查不到再落到本层，
 * 于是 PUA 区的 LV_SYMBOL_* 全部走线条图标。
 */
#define UI_SYMBOL_ATTACH(target, px)                                          \
    do {                                                                      \
        (target).fallback = ui_symbol_font(px);                               \
    } while (0)

#ifdef __cplusplus
}
#endif
