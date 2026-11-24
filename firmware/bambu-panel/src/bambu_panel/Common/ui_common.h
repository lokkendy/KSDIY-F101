/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - UI 设计系统
 *
 * 暗色工业风格设计语言：
 *  - 深空蓝背景 + 卡片分层 + 细描边
 *  - 中文使用 FreeType 矢量字体（回退 BSP myFont_24/20/14 点阵）
 *  - 所有控件统一圆角 / 间距 / 字号层级
 */
#pragma once

#include "lvgl.h"
#include "ui_symbol_font.h"
#include "ksdiy_lvgl_port.h"
#include "myFont.h"
#include "ui_screen.h"
#include "i18n.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────── 色彩系统（暗色/亮色可切换） ────────────────────
 * UI_COLOR_* 经活动调色板取值：控件在创建时读取当前配色；
 * 切换配色 = ui_theme_set_mode 换调色板 + pm_reload_shell 重建 UI。 */
typedef struct {
    lv_color_t bg;         /* 页面背景 */
    lv_color_t bg_bar;     /* 状态栏/Tab栏背景 */
    lv_color_t card;       /* 卡片背景 */
    lv_color_t card_hi;    /* 卡片高亮/按下 */
    lv_color_t primary;    /* 主色 */
    lv_color_t accent;     /* 强调 */
    lv_color_t success;    /* 成功 */
    lv_color_t danger;     /* 危险 */
    lv_color_t warning;    /* 警告 */
    lv_color_t text;       /* 主文字 */
    lv_color_t text_sec;   /* 次级文字 */
    lv_color_t border;     /* 描边 */
    lv_color_t track;      /* 进度条底轨 */
} ui_palette_t;

extern const ui_palette_t g_ui_pal_dark;   /* 深空蓝工业风（默认） */
extern const ui_palette_t g_ui_pal_light;  /* 浅色 */

/** 当前活动调色板 */
const ui_palette_t *ui_palette(void);

#define UI_COLOR_BG        (ui_palette()->bg)
#define UI_COLOR_BG_BAR    (ui_palette()->bg_bar)
#define UI_COLOR_CARD      (ui_palette()->card)
#define UI_COLOR_CARD_HI   (ui_palette()->card_hi)
#define UI_COLOR_PRIMARY   (ui_palette()->primary)
#define UI_COLOR_ACCENT    (ui_palette()->accent)
#define UI_COLOR_SUCCESS   (ui_palette()->success)
#define UI_COLOR_DANGER    (ui_palette()->danger)
#define UI_COLOR_WARNING   (ui_palette()->warning)
#define UI_COLOR_TEXT      (ui_palette()->text)
#define UI_COLOR_TEXT_SEC  (ui_palette()->text_sec)
#define UI_COLOR_BORDER    (ui_palette()->border)
#define UI_COLOR_TRACK     (ui_palette()->track)

/* ──────────────────── 布局常量（逻辑分辨率见 ui_screen.h） ──────────────────── */
#define UI_PAD                 16          /* 页面左右留白 */
#define UI_PAD_SM              8           /* 卡片内留白 */
#define UI_CARD_RADIUS         12
#define UI_BTN_RADIUS          10
#define UI_BTN_H               46          /* 主按钮高度（手指友好） */
#define UI_BTN_H_SM            36          /* 次级按钮高度 */
#define UI_STATUS_BAR_H        48
#define UI_TAB_BAR_H           66
#define UI_HEADER_H            56          /* 二级页大标题头 */
#define UI_KB_H                320         /* 软键盘高度 */
#define UI_KB_KEY_H            58          /* 单键最小高度（触控友好） */

/* ──────────────────── 字体（中文 + 符号回退） ────────────────────
 * myFont 只含 ASCII + CJK，LV_SYMBOL_*（PUA 区）不在其中。
 * ui_theme.c 内将字体浅拷贝到 RAM 副本并挂接 montserrat 作为 fallback，
 * 使同一标签可同时显示中文与图标符号。
 * 运行期热升级：先以 myFont 点阵立即可用，FreeType TTF 预载完成后
 * 由 80ms 定时器热替换（见 ui_theme.c）。
 */
extern lv_font_t ui_font_title;   /* 26px 中英文（运行期复制到 RAM 以挂接 fallback） */
extern lv_font_t ui_font_body;    /* 20px 中英文 */
extern lv_font_t ui_font_caption; /* 16px 中英文（小号说明文字） */
extern lv_font_t ui_font_num;     /* 48px 大数字 */
extern lv_font_t ui_font_num_sm;  /* 30px 中数字 */
#define UI_FONT_TITLE   (&ui_font_title)
#define UI_FONT_BODY    (&ui_font_body)
#define UI_FONT_CAPTION (&ui_font_caption)
#define UI_FONT_NUM     (&ui_font_num)
#define UI_FONT_NUM_SM  (&ui_font_num_sm)
#define UI_FONT_KB      UI_FONT_NUM_SM   /* 软键盘按键（30px FreeType） */
/* 纯 LV_SYMBOL 标签：勿继承 BSP myFont 主题字（无 PUA 区 → 方框） */
#define UI_FONT_SYMBOL  (ui_symbol_font(24))

typedef enum {
    UI_THEME_DARK = 0,
    UI_THEME_LIGHT,
} ui_theme_mode_t;

void ui_theme_init(void);
/** 切换配色（换活动调色板；已创建界面需配合 pm_reload_shell 重建生效） */
void ui_theme_set_mode(ui_theme_mode_t mode);
ui_theme_mode_t ui_theme_get_mode(void);
/** 同步 LVGL default theme（暗/亮 + UI_FONT_BODY）；shell 重建末尾调用 */
void ui_theme_apply_lvgl(void);
/**
 * 品牌色覆盖：把 primary/accent 换成行业主题色（RAM 调色板副本，暗/亮两套同步）。
 * 须在任何界面创建之前调用（App 启动最早期）；仅影响 primary/accent 两个槽位，
 * success/danger 等语义色不动。
 */
void ui_theme_set_brand(lv_color_t primary, lv_color_t accent);

/* ──────────────────── 基础控件 ──────────────────── */

/* 页面根容器：纵向 flex，不可滚动，深色背景 */
static inline lv_obj_t *ui_create_page_root(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    /* ── 应用私有扩展（舰队同步合并保留）── 根容器不吃点击 */
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return root;
}

/* 可滚动内容区（页面主体） */
static inline lv_obj_t *ui_create_content(lv_obj_t *parent)
{
    lv_obj_t *content = lv_obj_create(parent);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_min_height(content, 0, 0);
    lv_obj_set_style_pad_all(content, UI_PAD, 0);
    lv_obj_set_style_pad_top(content, 4, 0);
    lv_obj_set_style_pad_bottom(content, UI_PAD, 0);
    lv_obj_set_style_bg_opa(content, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, UI_PAD, 0);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    /* ── 应用私有扩展（舰队同步合并保留）── 横屏紧凑边距 */
    if (ui_screen_is_landscape()) {
        lv_obj_set_style_pad_all(content, UI_PAD_SM, 0);
        lv_obj_set_style_pad_row(content, UI_PAD_SM, 0);
        lv_obj_set_style_pad_top(content, 2, 0);
        lv_obj_set_style_pad_bottom(content, UI_PAD_SM, 0);
    }
    /* 须保持 CLICKABLE，否则 hit_test 失败，整页无法拖动滚动（子控件仍优先命中） */
    return content;
}

/* ── 应用私有扩展（舰队同步合并保留）── */

/** 固定在页面底部（不随 content 滚动），用于主操作按钮 */
static inline lv_obj_t *ui_create_page_footer(lv_obj_t *parent)
{
    const bool land = ui_screen_is_landscape();
    lv_obj_t *footer = lv_obj_create(parent);
    lv_obj_set_size(footer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(footer, 0, 0);
    lv_obj_set_style_bg_opa(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_hor(footer, land ? UI_PAD_SM : UI_PAD, 0);
    lv_obj_set_style_pad_top(footer, land ? 2 : UI_PAD_SM, 0);
    lv_obj_set_style_pad_bottom(footer, land ? 4 : UI_PAD_SM, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(footer, UI_PAD_SM, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return footer;
}

/** 单行输入框视觉：白底 + 深灰字（不随暗色主题用 UI_COLOR_TEXT，避免白字白底） */
static inline void ui_style_textarea_field(lv_obj_t *ta)
{
    if (!ta) {
        return;
    }
    const lv_color_t ink = lv_color_hex(0x17202A);
    const lv_color_t hint = lv_color_hex(0x526270);
    const lv_color_t paper = lv_color_hex(0xFFFFFF);
    lv_obj_set_style_text_font(ta, UI_FONT_BODY, 0);
    lv_obj_set_style_bg_color(ta, paper, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ta, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 10, 0);
    lv_obj_set_style_text_color(ta, ink, 0);
    lv_obj_set_style_text_color(ta, hint, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_color(ta, ink, LV_PART_CURSOR);
    lv_obj_set_style_pad_ver(ta, ui_screen_is_landscape() ? 4 : 7, 0);
}

/* 二级页大标题头：返回按钮（back_cb 为 NULL 时不显示，用于 Tab 主页）+ 标题 + 副标题 */
static inline lv_obj_t *ui_create_page_header(lv_obj_t *parent, const char *title,
                                              const char *subtitle, lv_event_cb_t back_cb)
{
    /* ── 应用私有扩展（舰队同步合并保留）── 横屏压缩标题头 */
    const bool land = ui_screen_is_landscape();
    if (land && subtitle) {
        subtitle = NULL;
    }

    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_bottom(header, 2, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* 圆形返回按钮（仅覆盖页） */
    if (back_cb) {
        lv_obj_t *back = lv_btn_create(header);
        lv_coord_t back_sz = land ? 38 : 44;
        lv_obj_set_size(back, back_sz, back_sz);
        lv_obj_set_style_bg_color(back, UI_COLOR_CARD, 0);
        lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(back, 0, 0);
        lv_obj_set_style_shadow_width(back, 0, 0);
        lv_obj_t *bl = lv_label_create(back);
        lv_label_set_text(bl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(bl, UI_FONT_SYMBOL, 0);
        lv_obj_set_style_text_color(bl, UI_COLOR_TEXT, 0);
        lv_obj_center(bl);
        lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    }

    /* 标题 + 副标题 */
    lv_obj_t *col = lv_obj_create(header);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_size(col, 0, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, 0, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(col);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, land ? UI_FONT_BODY : UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(t, UI_COLOR_TEXT, 0);

    if (subtitle && subtitle[0]) {
        lv_obj_t *s = lv_label_create(col);
        lv_label_set_text(s, subtitle);
        lv_obj_set_style_text_font(s, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(s, UI_COLOR_TEXT_SEC, 0);
    }
    return header;
}

/* ── 应用私有扩展（舰队同步合并保留）── */

/** Tab 根页标题：横屏省略（Tab 栏已标识），竖屏保留 */
static inline void ui_create_tab_root_header(lv_obj_t *content, const char *title,
                                             const char *subtitle)
{
    if (!ui_screen_is_landscape()) {
        ui_create_page_header(content, title, subtitle, NULL);
    }
}

/** Tab 根页标题绑定版（竖屏创建时标题入 tr_bind 注册表，横屏整头省略） */
static inline void ui_create_tab_root_header_bind(lv_obj_t *content, const char *zh_title,
                                                  const char *subtitle)
{
    if (ui_screen_is_landscape()) {
        return;
    }
    lv_obj_t *header = ui_create_page_header(content, tr(zh_title), subtitle, NULL);
    lv_obj_t *col = lv_obj_get_child(header, 0);   /* back_cb=NULL → 首子即标题列 */
    if (col) {
        tr_bind(lv_obj_get_child(col, 0), zh_title);
    }
}

/* 卡片：深色圆角 + 细描边 */
static inline lv_obj_t *ui_create_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(card, UI_CARD_RADIUS, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, UI_PAD_SM + 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 8, 0);
    /* ── 应用私有扩展（舰队同步合并保留）── 横屏紧凑卡片 */
    if (ui_screen_is_landscape()) {
        lv_obj_set_style_pad_row(card, 4, 0);
        lv_obj_set_style_pad_all(card, UI_PAD_SM, 0);
    }
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

/* 卡片小节标题：强调色竖条 + 文本 */
static inline lv_obj_t *ui_create_section_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_obj_create(row);
    lv_obj_set_size(bar, 3, 16);
    lv_obj_set_style_bg_color(bar, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    return lbl;
}

/* 状态圆点（连接指示） */
static inline lv_obj_t *ui_create_dot(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    return dot;
}

/* 键值行：左侧灰标签，返回右侧值标签指针 */
static inline lv_obj_t *ui_create_kv_row(lv_obj_t *parent, const char *key)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_bg_opa(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *k = lv_label_create(row);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_font(k, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(k, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_width(k, 96);

    lv_obj_t *v = lv_label_create(row);
    lv_label_set_text(v, "-");
    lv_obj_set_style_text_font(v, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(v, UI_COLOR_TEXT, 0);
    lv_obj_set_flex_grow(v, 1);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    return v;
}

/* 横向透明行容器 */
static inline lv_obj_t *ui_create_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_bg_opa(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

/* 统计数值块：大数字 + 说明文字，占 1/N 宽 */
static inline lv_obj_t *ui_create_stat_tile(lv_obj_t *parent, const char *caption,
                                            lv_color_t accent)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_height(tile, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(tile, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(tile, 10, 0);
    lv_obj_set_style_pad_all(tile, 8, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile, 2, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *num = lv_label_create(tile);
    lv_label_set_text(num, "0");
    lv_obj_set_style_text_font(num, UI_FONT_NUM_SM, 0);
    lv_obj_set_style_text_color(num, accent, 0);

    lv_obj_t *cap = lv_label_create(tile);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_font(cap, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(cap, UI_COLOR_TEXT_SEC, 0);
    return num;  /* 返回数字标签供刷新 */
}

/* 按钮样式变体 */
typedef enum {
    UI_BTN_FILLED,     /* 主操作 实心 */
    UI_BTN_SUCCESS,    /* 确认/成功 绿 */
    UI_BTN_DANGER,     /* 危险操作 红 */
    UI_BTN_OUTLINE,    /* 次级 描边 */
    UI_BTN_SUBTLE,     /* 弱化 灰底 */
} ui_btn_style_t;

/** 按钮文案是否含中文/英文等（非纯 LV_SYMBOL+空格）。混排须 UI_FONT_BODY + montserrat fallback。 */
static inline bool ui_btn_text_has_letters(const char *text)
{
    const uint8_t *p = (const uint8_t *)text;
    if (!p || !p[0]) {
        return false;
    }
    while (*p) {
        if (*p < 0x80) {
            if (*p > ' ') {
                return true;
            }
            p++;
            continue;
        }
        if (*p == 0xEF && p[1] >= 0x80 && p[2] >= 0x80) {
            p += 3;
            continue;
        }
        return true;
    }
    return false;
}

static inline lv_obj_t *ui_create_btn(lv_obj_t *parent, const char *text,
                                      int32_t w, int32_t h,
                                      ui_btn_style_t style,
                                      lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    /* ── 应用私有扩展（舰队同步合并保留）── 横屏按钮高度收紧 */
    if (w > 0) {
        lv_obj_set_width(btn, w);
    } else {
        lv_obj_set_width(btn, 0);
        lv_obj_set_flex_grow(btn, 1);
    }
    if (h <= 0) {
        h = (int32_t)ui_land_btn_h();
    } else if (ui_screen_is_landscape()) {
        if (h == UI_BTN_H) {
            h = (int32_t)ui_land_btn_h();
        } else if (h == UI_BTN_H_SM) {
            h = (int32_t)ui_land_btn_h_sm();
        }
    }
    lv_obj_set_height(btn, h);

    lv_color_t bg, fg;
    switch (style) {
    case UI_BTN_SUCCESS: bg = UI_COLOR_SUCCESS; fg = lv_color_white(); break;
    case UI_BTN_DANGER:  bg = UI_COLOR_DANGER;  fg = lv_color_white(); break;
    case UI_BTN_OUTLINE: bg = UI_COLOR_BG; fg = UI_COLOR_PRIMARY; break;
    case UI_BTN_SUBTLE:  bg = UI_COLOR_CARD_HI; fg = UI_COLOR_TEXT; break;
    default:             bg = UI_COLOR_PRIMARY; fg = lv_color_white(); break;
    }

    /* 高纯度黄/橙/青/青柠仍需保证按钮文字对比度。 */
    if (style == UI_BTN_FILLED || style == UI_BTN_SUCCESS || style == UI_BTN_DANGER) {
        fg = lv_color_luminance(bg) >= 145 ? lv_color_hex(0x17202A) : lv_color_white();
    }

    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, style == UI_BTN_OUTLINE ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, UI_BTN_RADIUS, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, style == UI_BTN_OUTLINE ? 1 : 0, 0);
    lv_obj_set_style_border_color(btn, UI_COLOR_PRIMARY, 0);
    /* 按下反馈 */
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_HI, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, fg, 0);
    /* 纯 LV_SYMBOL 走 montserrat；「符号 + 中文」须 FreeType + fallback（勿按首字节 0xEF 误判） */
    lv_obj_set_style_text_font(label,
                               ui_btn_text_has_letters(text) ? UI_FONT_BODY : UI_FONT_SYMBOL,
                               0);
    lv_obj_set_style_pad_hor(btn, 8, 0);
    if (w != LV_SIZE_CONTENT) {
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    }
    lv_obj_center(label);

    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

/* 下拉框统一配色。注意：弹出列表是挂在屏幕上的独立对象（非下拉的子节点），
 * 列表项字体/文字色取 list 的 LV_PART_SELECTED、底色取 LV_PART_MAIN，
 * 按钮上 selector=0 的样式不会传递过去 —— 中文选项必须单独设 list 字体。
 * 真机 BSP 固定 lv_theme_default_init(dark=true)，切换浅色配色后若不显式设
 * text_color，按钮区仍继承主题白字，浅底上几乎不可见；模拟器无该主题故正常。 */
static inline void ui_style_dropdown(lv_obj_t *dd)
{
    lv_obj_set_style_text_font(dd, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(dd, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_color(dd, UI_COLOR_CARD_HI, 0);
    lv_obj_set_style_border_color(dd, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(dd, 1, 0);

    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (!list) return;
    lv_obj_set_style_text_font(list, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, UI_FONT_BODY, LV_PART_SELECTED);
    lv_obj_set_style_text_color(list, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_color(list, UI_COLOR_TEXT, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(list, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, UI_COLOR_CARD_HI, LV_PART_SELECTED);   /* 选中项高亮 */
}

/* 文件列表容器：覆盖 lv_theme_default 默认底色，随调色板重建生效 */
static inline void ui_style_list(lv_obj_t *list)
{
    lv_obj_set_style_bg_color(list, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_border_color(list, UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(list, UI_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_text_font(list, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(list, UI_COLOR_TEXT, 0);
}

/* 列表行按钮：显式设底色/字色，避免真机 dark theme 固定白底条 */
static inline void ui_style_list_btn(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_HI, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, 10, 0);
    lv_obj_set_style_pad_ver(btn, 8, 0);
    lv_obj_set_style_text_font(btn, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, 0);
}

/* 存储切换 chip：选中/未选中随调色板 */
static inline void ui_style_storage_chip(lv_obj_t *chip, bool selected)
{
    lv_obj_set_style_bg_color(chip, selected ? UI_COLOR_PRIMARY : UI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(chip, selected ? UI_COLOR_PRIMARY : UI_COLOR_BORDER, 0);
    lv_obj_t *lbl = lv_obj_get_child(chip, 0);
    if (lbl) {
        lv_obj_set_style_text_color(lbl,
            selected ? (lv_color_luminance(UI_COLOR_PRIMARY) >= 145
                        ? lv_color_hex(0x17202A) : lv_color_white())
                     : UI_COLOR_TEXT, 0);
    }
}

/* 列表行按钮：图标 + 文本 + 右侧箭头，整行可点（快捷入口/文件列表通用） */
static inline lv_obj_t *ui_create_list_btn(lv_obj_t *parent, const char *icon,
                                           const char *text, lv_color_t accent,
                                           lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), LV_SIZE_CONTENT);
    ui_style_list_btn(btn);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, 10, 0);

    lv_obj_t *ico = lv_label_create(btn);
    lv_label_set_text(ico, icon ? icon : "");
    lv_obj_set_style_text_font(ico, UI_FONT_SYMBOL, 0);
    lv_obj_set_style_text_color(ico, accent, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_flex_grow(lbl, 1);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

    lv_obj_t *arrow = lv_label_create(btn);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(arrow, UI_FONT_SYMBOL, 0);
    lv_obj_set_style_text_color(arrow, UI_COLOR_TEXT_SEC, 0);

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    return btn;
}

/* 统一 Label 快速创建 */
static inline lv_obj_t *ui_create_label(lv_obj_t *parent, const char *text,
                                        const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font ? font : UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    /* ── 应用私有扩展（舰队同步合并保留）── 纯文本标签不吃点击（防误触/命中遮挡）*/
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    return lbl;
}

/* ──────────────────── 文案绑定变体（语言切换原地重译） ────────────────────
 *
 * 与上方同名控件同款，但传「中文键」而非 tr() 结果：创建即翻译并登记
 * tr_bind 注册表；pm_retranslate() 原地重设文本，页面不重建，
 * 滚动位置 / 输入内容 / 导航栈全保留。
 * 动态拼接文案（snprintf）不适配绑定表 —— 用 page_t.on_language 钩子。
 * 子对象索引依赖（btn 首子 = 文本 label 等）已按 ui_common 布局固化。 */

/** 标签绑定版 */
static inline lv_obj_t *ui_create_label_bind(lv_obj_t *parent, const char *zh_key,
                                             const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *lbl = ui_create_label(parent, tr(zh_key), font, color);
    tr_bind(lbl, zh_key);
    return lbl;
}

/** 按钮绑定版（文本 label 为按钮首子） */
static inline lv_obj_t *ui_create_btn_bind(lv_obj_t *parent, const char *zh_key,
                                           int32_t w, int32_t h, ui_btn_style_t style,
                                           lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = ui_create_btn(parent, tr(zh_key), w, h, style, cb, user_data);
    tr_bind(lv_obj_get_child(btn, 0), zh_key);
    return btn;
}

/** 小节标题绑定版（原函数直接返回文本 label） */
static inline lv_obj_t *ui_create_section_title_bind(lv_obj_t *parent, const char *zh_key)
{
    lv_obj_t *lbl = ui_create_section_title(parent, tr(zh_key));
    tr_bind(lbl, zh_key);
    return lbl;
}

/** 二级页大标题头绑定版（header > [back?] > col > 标题） */
static inline lv_obj_t *ui_create_page_header_bind(lv_obj_t *parent, const char *zh_title,
                                                   const char *subtitle, lv_event_cb_t back_cb)
{
    lv_obj_t *header = ui_create_page_header(parent, tr(zh_title), subtitle, back_cb);
    lv_obj_t *col = lv_obj_get_child(header, back_cb ? 1 : 0);
    if (col) {
        tr_bind(lv_obj_get_child(col, 0), zh_title);
    }
    return header;
}

/** 列表行按钮绑定版（icon + 文本 + 箭头，文本为第二个子对象） */
static inline lv_obj_t *ui_create_list_btn_bind(lv_obj_t *parent, const char *icon,
                                                const char *zh_key, lv_color_t accent,
                                                lv_event_cb_t cb)
{
    lv_obj_t *btn = ui_create_list_btn(parent, icon, tr(zh_key), accent, cb);
    tr_bind(lv_obj_get_child(btn, 1), zh_key);
    return btn;
}

#ifdef __cplusplus
}
#endif
