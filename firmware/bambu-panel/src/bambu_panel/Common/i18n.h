/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 中英文运行时切换
 *
 * 以中文原文为键查词典：tr("待机")。英文模式下未命中时返回键本身
 * （安全回退为中文），中文模式直接返回键，零查表开销。
 * 词典条目两侧的 LV_SYMBOL_* 一律用宏拼接，不手打 UTF-8 字节。
 *
 * 应用自有词条：main/App/Model/app_i18n.c 里建 i18n_dict_entry_t 数组，
 * 启动时 i18n_register(entries, count) 注册（可注册多张，先注册先命中，
 * 应用词条可覆盖内置词条）。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_LANG_ZH = 0,   /* 中文（默认） */
    APP_LANG_EN = 1,   /* English */
} app_lang_t;

/** 词典条目：中文原文为键，英文为译文 */
typedef struct {
    const char *zh;
    const char *en;
} i18n_dict_entry_t;

/**
 * @brief 注册应用词典（可在 UI 创建前的任意时刻调用，允许多次注册）
 *
 * 词条格式规则：
 * - 格式串两列的替换符集合/顺序/换行数必须一致
 * - LV_SYMBOL_* 用宏拼接（两侧同宏，字节天然一致）
 * - 含 PRIu32/PRIX32 等宽宏的格式串需改写为显式 %lu/%08lX 再入典
 *   （宏在 ESP32/MinGW 工具链展开不同，会导致键不匹配）
 */
void i18n_register(const i18n_dict_entry_t *entries, size_t count);

/** 设置当前语言（仅 LVGL 任务调用；HAL/工作线程只读） */
void i18n_set_lang(app_lang_t lang);

app_lang_t i18n_get_lang(void);

/** 翻译：中文原文为键，返回当前语言文案；未命中返回键本身 */
const char *tr(const char *zh);

/* ──────────────────── 文案绑定（语言切换原地重译，不重建页面） ────────────────────
 *
 * View 构建时把"静态文案标签"经 tr_bind 绑定中文键；切语言时
 * i18n_retranslate_all() 走注册表原地重设 label 文本 —— 滚动位置、
 * 输入内容、控件状态全保留（对比 pm_reload 整页重建）。
 *
 * - 绑定表为静态定长（TR_BIND_MAX），零堆分配；满了先回收失效槽
 *   （label 已删的条目）再降级为仅设文案（LOGW）
 * - label 删除后注册项自动失效（重译时 lv_obj_is_valid 校验并紧凑）
 * - 同一 label 重复 bind = 覆盖旧键
 * - 动态拼接文案（snprintf 等）不适配绑定表：页面用 page_t.on_language
 *   钩子自行重算
 * - LVGL 任务调用（View 构建天然满足）
 */
#define TR_BIND_MAX 384

/** 设文案并登记绑定（label 须为 lv_label）；同 label 重复调用换键 */
void tr_bind(lv_obj_t *label, const char *zh_key);

/** 按注册表原地重设全部绑定文案（LVGL 任务；pm_retranslate 自动调用）。
 *  返回实际重设条数（失效项顺带清理）。 */
size_t i18n_retranslate_all(void);

/** 当前绑定条数（调试） */
size_t i18n_bind_count(void);

#ifdef __cplusplus
}
#endif
