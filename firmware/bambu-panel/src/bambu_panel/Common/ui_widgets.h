/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - Design v2 视觉组件库
 *
 * 在 ui_common 基础件之上提供"有质感"的组合件：
 *   徽章 / 药丸 / 渐变主视觉卡(hero) / 仪表环(gauge) / 迷你柱图 /
 *   统计瓦片 v2 / 列表行 v2 / 按压缩放动效 / 数字滚动 / 页面氛围渐变。
 *
 * 约束：
 *  - 共享代码禁用 lv_chart（模拟器 LV_USE_CHART=0），曲线/柱图用 lv_bar/lv_arc；
 *  - 阴影昂贵（无 GPU 加速路径），一律用渐变+色块代替；
 *  - 所有组件按"创建时读调色板"工作，切换主题需重建界面（pm_reload_shell）。
 */
#pragma once

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 小号符号字体（药丸/箭头内 14px LV_SYMBOL_*，须 montserrat_14，双端已开） */
#define UI_FONT_SYMBOL_SM (&lv_font_montserrat_14)

#define UI_W_BARS_MAX 16   /* 迷你柱图最大柱数 */

/* ──────────────────── 颜色工具 ──────────────────── */

/** 在卡片底色上晕染 color（ratio 0..255，越大越浓）——徽章/药丸浅底 */
lv_color_t ui_w_tint(lv_color_t color, uint8_t ratio);

/** 加暗 color（amt 0..255，越大越黑）——渐变末端 */
lv_color_t ui_w_shade(lv_color_t color, uint8_t amt);

/** 给对象设置双色渐变背景（水平/垂直） */
void ui_w_style_grad(lv_obj_t *obj, lv_color_t c1, lv_color_t c2, bool vertical);

/* ──────────────────── 徽章（圆底 + 符号） ──────────────────── */

/** 彩色浅底圆徽章：底 = color 在卡片底色上的浅晕，符号 = color 本色 */
lv_obj_t *ui_w_badge(lv_obj_t *parent, lv_color_t color, const char *symbol, int32_t size);

/** 彩色面上的白徽章（hero 卡内）：白色半透明底 + 白符号 */
lv_obj_t *ui_w_badge_on(lv_obj_t *parent, const char *symbol, int32_t size);

/* ──────────────────── 药丸（圆角状态标签） ──────────────────── */

/** 彩色浅底药丸（卡片上用）：文字 = color 本色 */
lv_obj_t *ui_w_pill(lv_obj_t *parent, lv_color_t color, const char *text);

/** 彩色面上的白色半透明药丸（hero 卡内用） */
lv_obj_t *ui_w_pill_on(lv_obj_t *parent, const char *text);

/** 就地更新药丸文字与配色（普通药丸） */
void ui_w_pill_set(lv_obj_t *pill, lv_color_t color, const char *text);

/* ──────────────────── 主视觉卡（渐变 hero） ──────────────────── */

typedef struct {
    lv_obj_t *card;    /* 渐变卡本体 */
    lv_obj_t *badge;   /* 左上白徽章 */
    lv_obj_t *title;   /* 标题（白） */
    lv_obj_t *pill;    /* 右上状态药丸（白，半透明底） */
    lv_obj_t *value;   /* 大数字（白，UI_FONT_NUM） */
    lv_obj_t *cap;     /* 底部说明（白 75%） */
} ui_w_hero_t;

/**
 * @brief 渐变主视觉卡：color → 暗色 纵向渐变 + 白徽章 + 大数字 + 状态药丸
 * @note  文字一律白色；pill 用 ui_w_pill_set(h->pill, 任意色, text) 更新
 *        （hero 药丸保持白底白字，忽略传入色也安全）。
 */
void ui_w_hero(lv_obj_t *parent, lv_color_t color, const char *symbol,
               const char *title, ui_w_hero_t *out);

/* ──────────────────── 仪表环（lv_arc 圆环） ──────────────────── */

typedef struct {
    lv_obj_t *box;     /* 外容器（flex_grow/布局可作用于它） */
    lv_obj_t *arc;     /* 圆环本体（user_data 指回本结构，勿复用） */
    lv_obj_t *val;     /* 中心数值 */
    lv_obj_t *unit;    /* 中心单位 */
    int32_t   cur;     /* 当前百分比 0..100 */
    int32_t   max;     /* 最近一次 set 的量程（用于回显真实值） */
} ui_w_gauge_t;

/**
 * @brief 圆环仪表：去掉旋钮的 270°..360° 满环 + 中心数值/单位 + 下方标题
 * @param size 环外径（建议 96~130）
 */
void ui_w_gauge(lv_obj_t *parent, lv_color_t color, int32_t size,
                const char *caption, ui_w_gauge_t *out);

/**
 * @brief 设置仪表值（动画过渡 ~500ms）
 * @param val  当前值；val<0 或 max<=0 显示 "--"
 * @param unit 单位文字（NULL 保持不变，"" 隐藏）
 */
void ui_w_gauge_set(ui_w_gauge_t *g, int32_t val, int32_t max, const char *unit);

/* ──────────────────── 迷你柱状图（竖向 lv_bar 排） ──────────────────── */

typedef struct {
    lv_obj_t *box;                    /* 容器 */
    lv_obj_t *bars[UI_W_BARS_MAX];    /* 竖条 */
    int       n;                      /* 实际柱数 */
} ui_w_bars_t;

/** @brief n 根竖条迷你柱图（底 = color 浅晕，柱 = color 本色），高度 h */
void ui_w_bars(lv_obj_t *parent, lv_color_t color, int n, int32_t h, ui_w_bars_t *out);

/** @brief 更新柱值（vmax<=0 时全 0），按 vals/vmax 归一化到 0..100% */
void ui_w_bars_set(ui_w_bars_t *b, const int32_t *vals, int n, int32_t vmax);

/* ──────────────────── 统计瓦片 v2（徽章 + 数值 + 标题） ──────────────────── */

typedef struct {
    lv_obj_t *tile;
    lv_obj_t *badge;   /* 顶部彩色徽章 */
    lv_obj_t *value;   /* 数值（UI_FONT_NUM_SM） */
    lv_obj_t *cap;     /* 标题 */
} ui_w_stat_t;

void ui_w_stat(lv_obj_t *parent, lv_color_t color, const char *symbol,
               const char *caption, ui_w_stat_t *out);

/** 更新数值文字（要滚动效果用 ui_w_count_to(s->value, n, ms)） */
void ui_w_stat_set(ui_w_stat_t *s, const char *text);

/* ──────────────────── 列表行 v2（徽章 + 标题/副标题 + 药丸 + 箭头） ──────────────────── */

typedef struct {
    lv_obj_t *row;
    lv_obj_t *badge;
    lv_obj_t *title;   /* 主标题 */
    lv_obj_t *sub;     /* 副标题（次级色） */
    lv_obj_t *pill;    /* 右侧状态药丸（初始隐藏） */
    lv_obj_t *arrow;   /* 右箭头（仅可点击行显示） */
} ui_w_row_t;

/**
 * @brief 现代列表行：无边框圆角卡 + 彩徽章 + 两行文字 + 右侧药丸
 * @param cb   点击回调（NULL = 纯展示行，无箭头无按压反馈）
 */
void ui_w_row(lv_obj_t *parent, lv_color_t color, const char *symbol,
              const char *title, const char *sub, lv_event_cb_t cb, ui_w_row_t *out);

/** 更新标题/副标题 */
void ui_w_row_set(ui_w_row_t *r, const char *title, const char *sub);

/** 显示/更新右侧药丸（text=NULL 隐藏） */
void ui_w_row_pill(ui_w_row_t *r, lv_color_t color, const char *text);

/* ──────────────────── 动效 ──────────────────── */

/** 按压缩放回弹（按下缩至 95%，松开过冲回弹）——可点卡片/行/大按钮通用 */
void ui_w_press_fx(lv_obj_t *obj);

/** 数字滚动：label 内容从当前数值动画滚到 target（dur_ms 毫秒） */
void ui_w_count_to(lv_obj_t *label, int32_t target, uint32_t dur_ms);

/* ──────────────────── 页面氛围 ──────────────────── */

/** 页面底纵向渐变：背景色 → 主色微染（品牌氛围，替代纯色平铺） */
void ui_w_page_ambience(lv_obj_t *root);

#ifdef __cplusplus
}
#endif
