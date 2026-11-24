/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - Design v2 视觉组件库实现
 */
#include "ui_widgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* ──────────────────── 颜色工具 ──────────────────── */

lv_color_t ui_w_tint(lv_color_t color, uint8_t ratio)
{
    /* lv_color_mix(c1,c2,r)：r=255 全 c1 → 在卡片底色上晕染 ratio 份 color */
    return lv_color_mix(color, ui_palette()->card, ratio);
}

lv_color_t ui_w_shade(lv_color_t color, uint8_t amt)
{
    return lv_color_mix(lv_color_black(), color, amt);
}

void ui_w_style_grad(lv_obj_t *obj, lv_color_t c1, lv_color_t c2, bool vertical)
{
    lv_obj_set_style_bg_color(obj, c1, 0);
    lv_obj_set_style_bg_grad_color(obj, c2, 0);
    lv_obj_set_style_bg_grad_dir(obj, vertical ? LV_GRAD_DIR_VER : LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

/* ──────────────────── 徽章 ──────────────────── */

static lv_obj_t *badge_ex(lv_obj_t *parent, const char *symbol, int32_t size,
                          lv_color_t bg, lv_opa_t bg_opa, lv_color_t fg)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, bg_opa, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *s = lv_label_create(b);
    lv_label_set_text(s, symbol);
    lv_obj_set_style_text_font(s, size >= 40 ? UI_FONT_SYMBOL : UI_FONT_SYMBOL_SM, 0);
    lv_obj_set_style_text_color(s, fg, 0);
    lv_obj_center(s);
    return b;
}

lv_obj_t *ui_w_badge(lv_obj_t *parent, lv_color_t color, const char *symbol, int32_t size)
{
    return badge_ex(parent, symbol, size, ui_w_tint(color, 52), LV_OPA_COVER, color);
}

lv_obj_t *ui_w_badge_on(lv_obj_t *parent, const char *symbol, int32_t size)
{
    return badge_ex(parent, symbol, size, lv_color_white(), LV_OPA_40, lv_color_white());
}

/* ──────────────────── 药丸 ──────────────────── */

static lv_obj_t *pill_ex(lv_obj_t *parent, lv_color_t bg, lv_opa_t bg_opa,
                         lv_color_t fg, const char *text)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_height(p, lv_font_get_line_height(UI_FONT_CAPTION) + 8);
    lv_obj_set_width(p, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(p, bg, 0);
    lv_obj_set_style_bg_opa(p, bg_opa, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_hor(p, 10, 0);
    lv_obj_set_style_pad_ver(p, 3, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_obj_center(l);
    return p;
}

lv_obj_t *ui_w_pill(lv_obj_t *parent, lv_color_t color, const char *text)
{
    lv_obj_t *p = pill_ex(parent, ui_w_tint(color, 42), LV_OPA_COVER, color, text);
    lv_obj_set_user_data(p, (void *)(uintptr_t)0);   /* 普通药丸：set 可改色 */
    return p;
}

lv_obj_t *ui_w_pill_on(lv_obj_t *parent, const char *text)
{
    lv_obj_t *p = pill_ex(parent, lv_color_white(), LV_OPA_30, lv_color_white(), text);
    lv_obj_set_user_data(p, (void *)(uintptr_t)1);   /* 彩面药丸：set 只换字不改色 */
    return p;
}

void ui_w_pill_set(lv_obj_t *pill, lv_color_t color, const char *text)
{
    if (!pill || !lv_obj_is_valid(pill)) return;
    lv_obj_t *l = lv_obj_get_child(pill, 0);
    if (!l) return;
    lv_label_set_text(l, text ? text : "");
    if ((uintptr_t)lv_obj_get_user_data(pill) == 1) return;   /* hero 白药丸保持白 */
    lv_obj_set_style_bg_color(pill, ui_w_tint(color, 42), 0);
    lv_obj_set_style_text_color(l, color, 0);
}

/* ──────────────────── 主视觉卡（hero） ──────────────────── */

void ui_w_hero(lv_obj_t *parent, lv_color_t color, const char *symbol,
               const char *title, ui_w_hero_t *out)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    ui_w_style_grad(card, color, ui_w_shade(color, 110), true);
    lv_obj_set_style_radius(card, UI_CARD_RADIUS + 2, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶行：白徽章+标题 …… 状态药丸 */
    lv_obj_t *top = lv_obj_create(card);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_style_bg_opa(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 10, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = lv_obj_create(top);
    lv_obj_set_height(left, LV_SIZE_CONTENT);
    lv_obj_set_width(left, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_bg_opa(left, 0, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 10, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    out->badge = ui_w_badge_on(left, symbol, 44);

    out->title = lv_label_create(left);
    lv_label_set_text(out->title, title);
    lv_obj_set_style_text_font(out->title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(out->title, lv_color_white(), 0);

    out->pill = ui_w_pill_on(top, "");

    out->value = lv_label_create(card);
    lv_label_set_text(out->value, "--");
    lv_obj_set_width(out->value, LV_PCT(100));
    lv_obj_set_style_text_font(out->value, UI_FONT_NUM, 0);
    lv_obj_set_style_text_color(out->value, lv_color_white(), 0);
    lv_label_set_long_mode(out->value, LV_LABEL_LONG_DOT);

    out->cap = lv_label_create(card);
    lv_label_set_text(out->cap, "");
    lv_obj_set_width(out->cap, LV_PCT(100));
    lv_obj_set_style_text_font(out->cap, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(out->cap, lv_color_white(), 0);
    lv_obj_set_style_text_opa(out->cap, LV_OPA_70, 0);
    lv_label_set_long_mode(out->cap, LV_LABEL_LONG_DOT);

    out->card = card;
}

/* ──────────────────── 仪表环 ──────────────────── */

static void gauge_exec_cb(void *var, int32_t v)
{
    lv_obj_t *arc = (lv_obj_t *)var;
    ui_w_gauge_t *g = (ui_w_gauge_t *)lv_obj_get_user_data(arc);
    if (!g) return;
    lv_arc_set_value(arc, v);
    char buf[16];
    if (g->max > 0) {
        int32_t disp = (int32_t)((int64_t)v * g->max / 100);
        snprintf(buf, sizeof(buf), "%d", (int)disp);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(g->val, buf);
}

void ui_w_gauge(lv_obj_t *parent, lv_color_t color, int32_t size,
                const char *caption, ui_w_gauge_t *out)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(box, 2, 0);
    lv_obj_set_style_bg_opa(box, 0, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 2, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *arc = lv_arc_create(box);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, size / 10 < 8 ? 8 : (size / 10 > 12 ? 12 : size / 10), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, size / 10 < 8 ? 8 : (size / 10 > 12 ? 12 : size / 10), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);      /* 去旋钮 */
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    out->val = lv_label_create(arc);
    lv_label_set_text(out->val, "--");
    lv_obj_set_style_text_font(out->val, UI_FONT_NUM_SM, 0);
    lv_obj_set_style_text_color(out->val, UI_COLOR_TEXT, 0);
    lv_obj_align(out->val, LV_ALIGN_CENTER, 0, -8);

    out->unit = lv_label_create(arc);
    lv_label_set_text(out->unit, "");
    lv_obj_set_style_text_font(out->unit, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(out->unit, UI_COLOR_TEXT_SEC, 0);
    lv_obj_align(out->unit, LV_ALIGN_CENTER, 0, 16);

    lv_obj_t *cap = lv_label_create(box);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_font(cap, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(cap, UI_COLOR_TEXT_SEC, 0);

    out->box = box;
    out->arc = arc;
    out->cur = 0;
    out->max = 0;
    lv_obj_set_user_data(arc, out);
}

void ui_w_gauge_set(ui_w_gauge_t *g, int32_t val, int32_t max, const char *unit)
{
    if (!g || !lv_obj_is_valid(g->arc)) return;
    if (unit) lv_label_set_text(g->unit, unit);

    int32_t pct = 0;
    if (val >= 0 && max > 0) {
        pct = val * 100 / max;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
    }
    g->max = (val >= 0 && max > 0) ? val : 0;   /* 回显真实值：动画插值到 val */

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g->arc);
    lv_anim_set_values(&a, g->cur, pct);
    lv_anim_set_duration(&a, 500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, gauge_exec_cb);
    lv_anim_start(&a);
    g->cur = pct;

    if (val < 0 || max <= 0) lv_label_set_text(g->val, "--");
}

/* ──────────────────── 迷你柱状图 ──────────────────── */

void ui_w_bars(lv_obj_t *parent, lv_color_t color, int n, int32_t h, ui_w_bars_t *out)
{
    if (n > UI_W_BARS_MAX) n = UI_W_BARS_MAX;
    if (n < 1) n = 1;

    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_height(box, h);
    lv_obj_set_width(box, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_bg_opa(box, 0, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(box, 3, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < n; i++) {
        lv_obj_t *bar = lv_bar_create(box);
        lv_obj_set_size(bar, 8, lv_pct(100));
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar, ui_w_tint(color, 30), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
        out->bars[i] = bar;
    }
    out->box = box;
    out->n = n;
}

void ui_w_bars_set(ui_w_bars_t *b, const int32_t *vals, int n, int32_t vmax)
{
    if (!b || !lv_obj_is_valid(b->box)) return;
    if (n > b->n) n = b->n;
    for (int i = 0; i < b->n; i++) {
        int32_t pct = 0;
        if (vals && i < n && vmax > 0) {
            pct = vals[i] * 100 / vmax;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
        }
        lv_bar_set_value(b->bars[i], pct, LV_ANIM_ON);
    }
}

/* ──────────────────── 统计瓦片 v2 ──────────────────── */

void ui_w_stat(lv_obj_t *parent, lv_color_t color, const char *symbol,
               const char *caption, ui_w_stat_t *out)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_height(tile, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(tile, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 12, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 6, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    out->badge = ui_w_badge(tile, color, symbol, 32);

    out->value = lv_label_create(tile);
    lv_label_set_text(out->value, "--");
    lv_obj_set_style_text_font(out->value, UI_FONT_NUM_SM, 0);
    lv_obj_set_style_text_color(out->value, UI_COLOR_TEXT, 0);
    lv_label_set_long_mode(out->value, LV_LABEL_LONG_DOT);

    out->cap = lv_label_create(tile);
    lv_label_set_text(out->cap, caption);
    lv_obj_set_style_text_font(out->cap, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(out->cap, UI_COLOR_TEXT_SEC, 0);

    out->tile = tile;
}

void ui_w_stat_set(ui_w_stat_t *s, const char *text)
{
    if (!s || !lv_obj_is_valid(s->value)) return;
    lv_label_set_text(s->value, text);
}

/* ──────────────────── 列表行 v2 ──────────────────── */

void ui_w_row(lv_obj_t *parent, lv_color_t color, const char *symbol,
              const char *title, const char *sub, lv_event_cb_t cb, ui_w_row_t *out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_pad_row(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    out->badge = ui_w_badge(row, color, symbol, 42);

    lv_obj_t *mid = lv_obj_create(row);
    lv_obj_set_width(mid, 0);
    lv_obj_set_height(mid, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(mid, 1);
    lv_obj_set_style_pad_all(mid, 0, 0);
    lv_obj_set_style_bg_opa(mid, 0, 0);
    lv_obj_set_style_border_width(mid, 0, 0);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(mid, 2, 0);
    lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);
    /* LVGL 9.5 无事件冒泡，裸 lv_obj 默认 CLICKABLE（lv_obj.c 构造置位）会截胡行点击
     * —— 点在标题/副标题区域时事件落在 mid 上、行回调不触发，必须让出 */
    lv_obj_clear_flag(mid, LV_OBJ_FLAG_CLICKABLE);

    out->title = lv_label_create(mid);
    lv_label_set_text(out->title, title ? title : "");
    lv_obj_set_width(out->title, LV_PCT(100));
    lv_obj_set_style_text_font(out->title, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(out->title, UI_COLOR_TEXT, 0);
    lv_label_set_long_mode(out->title, LV_LABEL_LONG_DOT);

    out->sub = lv_label_create(mid);
    lv_label_set_text(out->sub, sub ? sub : "");
    lv_obj_set_width(out->sub, LV_PCT(100));
    lv_obj_set_style_text_font(out->sub, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(out->sub, UI_COLOR_TEXT_SEC, 0);
    lv_label_set_long_mode(out->sub, LV_LABEL_LONG_DOT);

    out->pill = ui_w_pill(row, color, "");
    lv_obj_add_flag(out->pill, LV_OBJ_FLAG_HIDDEN);

    out->arrow = lv_label_create(row);
    lv_label_set_text(out->arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(out->arrow, UI_FONT_SYMBOL_SM, 0);
    lv_obj_set_style_text_color(out->arrow, UI_COLOR_TEXT_SEC, 0);

    out->row = row;

    if (cb) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_bg_color(row, UI_COLOR_CARD_HI, LV_STATE_PRESSED);
        ui_w_press_fx(row);
    } else {
        lv_obj_add_flag(out->arrow, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_w_row_set(ui_w_row_t *r, const char *title, const char *sub)
{
    if (!r || !lv_obj_is_valid(r->row)) return;
    if (title) lv_label_set_text(r->title, title);
    if (sub) lv_label_set_text(r->sub, sub);
}

void ui_w_row_pill(ui_w_row_t *r, lv_color_t color, const char *text)
{
    if (!r || !lv_obj_is_valid(r->row)) return;
    if (!text) {
        lv_obj_add_flag(r->pill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    ui_w_pill_set(r->pill, color, text);
    lv_obj_clear_flag(r->pill, LV_OBJ_FLAG_HIDDEN);
}

/* ──────────────────── 动效 ──────────────────── */

static void press_fx_exec_cb(void *var, int32_t v)
{
    /* LVGL 9.5：scale 拆成 x/y 两个属性，同步设置 */
    lv_obj_set_style_transform_scale_x((lv_obj_t *)var, v, 0);
    lv_obj_set_style_transform_scale_y((lv_obj_t *)var, v, 0);
}

static void press_fx_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int32_t cur = lv_obj_get_style_transform_scale_x(obj, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, press_fx_exec_cb);

    if (code == LV_EVENT_PRESSED && cur == 256) {
        lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, 0);
        lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, 0);
        lv_anim_set_values(&a, 256, 244);
        lv_anim_set_duration(&a, 110);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    } else if (cur != 256) {   /* RELEASED / PRESS_LOST：过冲回弹 */
        lv_anim_set_values(&a, cur, 256);
        lv_anim_set_duration(&a, 200);
        lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    } else {
        return;
    }
    lv_anim_start(&a);
}

void ui_w_press_fx(lv_obj_t *obj)
{
    lv_obj_add_event_cb(obj, press_fx_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, press_fx_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(obj, press_fx_cb, LV_EVENT_PRESS_LOST, NULL);
}

static void count_to_exec_cb(void *var, int32_t v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)v);
    lv_label_set_text((lv_obj_t *)var, buf);
}

void ui_w_count_to(lv_obj_t *label, int32_t target, uint32_t dur_ms)
{
    if (!label || !lv_obj_is_valid(label)) return;

    const char *txt = lv_label_get_text(label);
    long from = 0;
    if (txt && txt[0]) {
        char *end = NULL;
        long v = strtol(txt, &end, 10);
        if (end != txt) from = v;
    }
    if (from == target) {
        count_to_exec_cb(label, target);
        return;
    }
    if (dur_ms == 0) dur_ms = 1;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_values(&a, (int32_t)from, target);
    lv_anim_set_duration(&a, dur_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, count_to_exec_cb);
    lv_anim_start(&a);
}

/* ──────────────────── 页面氛围 ──────────────────── */

void ui_w_page_ambience(lv_obj_t *root)
{
    ui_w_style_grad(root, UI_COLOR_BG,
                    lv_color_mix(UI_COLOR_PRIMARY, UI_COLOR_BG, 36), true);
}
