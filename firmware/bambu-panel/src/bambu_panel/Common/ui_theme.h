/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - UI 主题 / 弹窗
 */
#pragma once

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 后台预载 TTF 到 PSRAM（与显示/外设初始化并行，越早调用越好）
 *
 * App 启动第一步调用。内部仅启动一次低优先级任务顺序读字体文件；
 * ui_theme_init() 时若尚未读完则先用 myFont 点阵渲染，
 * 预载完成后由框架内部定时器热升级为 FreeType 矢量字体并重绘。
 * 模拟器（FreeType 直读文件路径）下为空操作。
 */
void ui_theme_font_preload_async(void);

/**
 * @brief 取任意字号的 FreeType 字体（页面级大字号数字/标题等）
 *
 * 按字号缓存复用（≤4 档，常驻不释放），走同一份 TTF 预载镜像。
 * FreeType 尚未就绪（预载中/失败）时返回 NULL，调用方回退 UI_FONT_NUM，
 * 并可用 ui_theme_freetype_ready() 轮询，就绪后重新获取换装。
 */
const lv_font_t *ui_theme_acquire_font_px(int px);

/** FreeType 矢量字体是否已就绪（初始化或热升级完成） */
bool ui_theme_freetype_ready(void);

/* 提示条类型 */
typedef enum {
    UI_TOAST_INFO = 0,
    UI_TOAST_SUCCESS,
    UI_TOAST_ERROR,
} ui_toast_type_t;

/**
 * @brief 在屏幕底部弹出短暂提示（自动消失）
 *
 * @param text  文本（支持中文 + LV_SYMBOL）
 * @param type  类型决定左侧图标与颜色
 */
void ui_toast(const char *text, ui_toast_type_t type);

/**
 * @brief 确认对话框（模态）
 *
 * @param title    标题
 * @param body     正文（支持 \n 多行）
 * @param confirm  确认按钮文字（NULL 则用 tr("确定")）
 * @param on_ok    确认回调（NULL 则只关闭）
 * @param ctx      回调用户参数
 */
void ui_confirm_dialog(const char *title, const char *body, const char *confirm,
                       void (*on_ok)(void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
