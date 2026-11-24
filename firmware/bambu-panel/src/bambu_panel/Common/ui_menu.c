/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 创意动画菜单组件库实现
 *
 * 设计文档见 ui_menu.h 头注释。实现纪律：
 *  - 滚动系一律 get_scroll_*() 读 + scroll_to_*() 写（绝对值语义，源码核实），
 *    换页跳转只发生在 SCROLL_END / 编程 _set —— 不在 SCROLL 事件里动量期跳
 *    （scroll_to_x 会杀 LVGL 原生动量动画，快甩会"急停穿帮"）；
 *  - 全部整数定点运算（256 = 1.0），三角用 lv_trigo_sin（32768 基准）；
 *  - 事件风暴防线：last_sx/sy 2px 早退 + far_state 一次定型 + 解析式坐标；
 *  - 每控件入口 lv_obj_is_valid 守卫（页面销毁后 _set 安全空转）。
 */
#include "ui_menu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "i18n.h"
#include "page_transition.h"
#include "ui_widgets.h"
#include "voice.h"

/* ──────────────────── 共享状态与基础工具 ──────────────────── */

static bool s_scale_fx = true;          /* ui_m_set_scale_fx() */

void ui_m_set_scale_fx(bool on) { s_scale_fx = on; }

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int32_t min_i32(int32_t a, int32_t b) { return a < b ? a : b; }

/** 深色/浅色自适应的前景（亮底配深字，复用按钮先例阈值 145） */
static lv_color_t fg_on(lv_color_t bg)
{
    return lv_color_luminance(bg) >= 145 ? lv_color_hex(0x17202A) : lv_color_white();
}

/** 角度 → 点（0°=12 点方向，顺时针；屏幕坐标 y 向下） */
static lv_point_t polar_top_cw(int deg, int32_t r)
{
    lv_point_t p;
    p.x = (int32_t)((int64_t)r * lv_trigo_sin(deg) / 32768);
    p.y = -(int32_t)((int64_t)r * lv_trigo_cos(deg) / 32768);
    return p;
}

/** 角度 → 点（0°=3 点方向，顺时针；lv_arc 原生角系） */
static lv_point_t polar_raw_cw(int deg, int32_t r)
{
    lv_point_t p;
    p.x = (int32_t)((int64_t)r * lv_trigo_cos(deg) / 32768);
    p.y = (int32_t)((int64_t)r * lv_trigo_sin(deg) / 32768);
    return p;
}

/** 点相对圆心的方位角：0..359，0°=+x 方向，顺时针（屏幕系） */
static int point_angle_cw(int dx, int dy)
{
    float a = atan2f((float)dy, (float)dx) * 57.29578f;
    int deg = (int)(a + (a >= 0 ? 0.5f : -0.5f));
    if (deg < 0) deg += 360;
    return deg % 360;
}

/** 活动输入设备的点换算到 root 本地坐标 */
static void indev_point_local(lv_obj_t *root, lv_point_t *out)
{
    lv_point_t p = {0, 0};
    lv_indev_t *indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &p);
    lv_area_t co;
    lv_obj_get_coords(root, &co);
    out->x = p.x - co.x1;
    out->y = p.y - co.y1;
}

/* ── 动画执行器（共享） ── */
static void xlat_x_exec_cb(void *var, int32_t v) { lv_obj_set_style_translate_x(var, v, 0); }
static void xlat_y_exec_cb(void *var, int32_t v) { lv_obj_set_style_translate_y(var, v, 0); }
static void opa_exec_cb(void *var, int32_t v)    { lv_obj_set_style_opa(var, (lv_opa_t)v, 0); }
static void scale_exec_cb(void *var, int32_t v)
{
    lv_obj_set_style_transform_scale_x(var, v, 0);
    lv_obj_set_style_transform_scale_y(var, v, 0);
}

static void anim_go(lv_obj_t *var, lv_anim_exec_xcb_t exec, int32_t from, int32_t to,
                    uint32_t delay, uint16_t dur, lv_anim_path_cb_t path,
                    lv_anim_completed_cb_t done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_path_cb(&a, path);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

/** 选中通知：交互路径带 60ms 节流触觉音；编程 _set 只回 cb */
static void sel_notify(lv_obj_t *root, int idx, ui_m_sel_cb_t cb, void *ud, bool voice)
{
    if (voice) {
        static uint32_t s_last_voice_ms;
        uint32_t now = lv_tick_get();
        if (now - s_last_voice_ms > 60) {
            s_last_voice_ms = now;
            voice_touch_click();
        }
    }
    if (cb) cb(root, idx, ud);
}

/** 通用单元格：圆角浅晕底 + 居中 symbol/标题(绑定)/副题(可选)，可点 */
static lv_obj_t *make_cell(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *it,
                           int32_t w, int32_t h, bool with_sub)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, w, h);
    lv_obj_set_style_bg_color(cell, ui_w_tint(accent, 22), 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cell, 12, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, UI_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(cell, 6, 0);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 4, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    if (it->symbol)
        ui_create_label(cell, it->symbol, UI_FONT_SYMBOL, accent);
    if (it->zh)
        ui_create_label_bind(cell, it->zh, UI_FONT_CAPTION, UI_COLOR_TEXT);
    if (with_sub && it->zh_sub)
        ui_create_label_bind(cell, it->zh_sub, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    return cell;
}

/* ──────────────────── 入场编排 stagger ──────────────────── */

static void stagger_done_cb(lv_anim_t *a)
{
    /* 收尾清样式：KEEP_ALIVE 页复访不残留初态 */
    lv_obj_set_style_translate_y(a->var, 0, 0);
    lv_obj_set_style_opa(a->var, LV_OPA_COVER, 0);
}

void ui_m_stagger(lv_obj_t *container, int32_t dy, uint16_t step_ms, uint16_t dur)
{
    if (!container || !lv_obj_is_valid(container)) return;
    uint32_t cnt = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(container, i);
        lv_obj_set_style_translate_y(child, dy, 0);   /* 先摆初态再动画，防终态闪帧 */
        lv_obj_set_style_opa(child, 0, 0);
        uint32_t delay = i * step_ms;
        anim_go(child, xlat_y_exec_cb, dy, 0, delay, dur,
                lv_anim_path_ease_out, stagger_done_cb);
        anim_go(child, opa_exec_cb, 0, LV_OPA_COVER, delay, dur,
                lv_anim_path_ease_out, NULL);
    }
}

/* ──────────────────── 2) 旋钮选择器 ui_m_dial ────────────────────
 * 270° lv_arc（rotation 135，底部缺口）+ lv_line 刻度 + 中心交叉淡化文字。
 * 输入 = arc 全 bbox 的 PRESSING 逐帧跟踪，量化到档位（量化即滞回）。 */

static void dial_arc_exec_cb(void *var, int32_t v) { lv_arc_set_value(var, v); }

/** 当前归一化进度 0..256（离散按选中档；连续按量化值） */
static int dial_frac_now(ui_m_dial_t *m)
{
    if (m->items && m->n_items > 1)
        return m->sel >= 0 ? m->sel * 256 / (m->n_items - 1) : 0;
    if (!m->items && m->max > m->min)
        return clamp_i32((int)(((int64_t)(m->val - m->min) * 256) / (m->max - m->min)), 0, 256);
    return 0;
}

/** 刻度根数（离散=档数；连续=步数均匀抽样到 ≤13） */
static int dial_tick_count(const ui_m_dial_t *m)
{
    int n_t;
    if (m->items) {
        n_t = m->n_items;
    } else {
        int32_t step = m->step ? m->step : 1;
        int steps = (int)((m->max - m->min) / step) + 1;
        int stride = (steps + UI_M_DIAL_TICKS_MAX - 1) / UI_M_DIAL_TICKS_MAX;
        n_t = stride > 1 ? (steps + stride - 1) / stride : steps;
    }
    return clamp_i32(n_t, 2, UI_M_DIAL_TICKS_MAX);
}

/** 重建/首次建刻度线（坐标存 out->tick_pts，调用方持有） */
static void dial_ticks_recalc(ui_m_dial_t *m)
{
    int32_t size = lv_obj_get_width(m->root) - 40;
    if (size <= 0) return;
    int32_t c = size / 2 + 20;                 /* 圆心（root 系） */
    int n_t = dial_tick_count(m);

    for (int t = 0; t < n_t; t++) {
        int deg = 135 + (int)((int64_t)t * 270 / (n_t - 1));
        lv_point_t p1 = polar_raw_cw(deg, size / 2 + 6);
        lv_point_t p2 = polar_raw_cw(deg, size / 2 + 18);
        m->tick_pts[t * 2].x = c + p1.x;
        m->tick_pts[t * 2].y = c + p1.y;
        m->tick_pts[t * 2 + 1].x = c + p2.x;
        m->tick_pts[t * 2 + 1].y = c + p2.y;

        if (!m->tick_ln[t]) {
            lv_obj_t *ln = lv_line_create(m->root);
            lv_obj_remove_style_all(ln);
            lv_obj_set_style_line_width(ln, 2, 0);
            lv_obj_set_style_line_rounded(ln, true, 0);
            m->tick_ln[t] = ln;
        }
        lv_line_set_points(m->tick_ln[t], &m->tick_pts[t * 2], 2);
    }
    /* 多余刻度藏起来（模式切换收缩时） */
    for (int t = n_t; t < UI_M_DIAL_TICKS_MAX; t++) {
        if (m->tick_ln[t]) lv_obj_add_flag(m->tick_ln[t], LV_OBJ_FLAG_HIDDEN);
    }
}

/** 刻度重着色：最接近当前值的刻度 = 强调色 */
static void dial_ticks_recolor(ui_m_dial_t *m)
{
    int n_t = dial_tick_count(m);
    int frac = dial_frac_now(m);
    int near = (frac * (n_t - 1) + 128) / 256;
    for (int t = 0; t < n_t; t++) {
        if (!m->tick_ln[t]) continue;
        lv_obj_clear_flag(m->tick_ln[t], LV_OBJ_FLAG_HIDDEN);
        if (t == near) {
            lv_obj_set_style_line_color(m->tick_ln[t],
                                        lv_obj_get_style_arc_color(m->arc, LV_PART_INDICATOR), 0);
            lv_obj_set_style_line_width(m->tick_ln[t], 3, 0);
        } else {
            lv_obj_set_style_line_color(m->tick_ln[t], UI_COLOR_BORDER, 0);
            lv_obj_set_style_line_width(m->tick_ln[t], 2, 0);
        }
    }
}

/** 离散模式：文字交叉淡化切到 zh_key（animate=false 首次直显） */
static void dial_show_text(ui_m_dial_t *m, const char *zh_key, bool animate)
{
    lv_obj_t *in, *out;
    if (m->lbl_show == 0) { in = m->lbl_b; out = m->lbl_a; m->lbl_show = 1; }
    else                  { in = m->lbl_a; out = m->lbl_b; m->lbl_show = 0; }

    lv_label_set_text(in, tr(zh_key));
    tr_bind(in, zh_key);
    lv_obj_clear_flag(in, LV_OBJ_FLAG_HIDDEN);

    if (!animate) {
        lv_obj_set_style_opa(in, LV_OPA_COVER, 0);
        lv_obj_set_style_translate_y(in, 0, 0);
        lv_obj_set_style_opa(out, 0, 0);
        return;
    }
    anim_go(out, opa_exec_cb, LV_OPA_COVER, 0, 0, PM_T_DUR_FAST, lv_anim_path_ease_in, NULL);
    anim_go(in, opa_exec_cb, 0, LV_OPA_COVER, 0, PM_T_DUR_FAST, lv_anim_path_ease_out, NULL);
    anim_go(in, xlat_y_exec_cb, 8, 0, 0, PM_T_DUR_FAST, lv_anim_path_ease_out, NULL);
}

static void dial_val_refresh(ui_m_dial_t *m)
{
    char buf[16];
    if (m->items) snprintf(buf, sizeof(buf), "%d/%d", m->sel + 1, m->n_items);
    else          snprintf(buf, sizeof(buf), "%d", (int)m->val);
    lv_label_set_text(m->val_lbl, buf);
}

/** 统一提交：弧动画 + 刻度 + 中心文字 + 通知 */
static void dial_commit(ui_m_dial_t *m, bool animate, bool voice)
{
    int frac = dial_frac_now(m);
    int32_t target = frac * 1000 / 256;      /* arc 量程 0..1000 */
    int32_t cur = lv_arc_get_value(m->arc);
    if (animate)
        anim_go(m->arc, dial_arc_exec_cb, cur, target, 0, PM_T_DUR_FAST,
                lv_anim_path_ease_out, NULL);
    else
        lv_arc_set_value(m->arc, target);

    dial_ticks_recolor(m);
    if (m->items) dial_show_text(m, m->items[clamp_i32(m->sel, 0, m->n_items - 1)].zh, true);
    dial_val_refresh(m);

    int cb_idx = m->items ? m->sel
                          : (int)((m->val - m->min) / (m->step ? m->step : 1));
    sel_notify(m->root, cb_idx, m->cb, m->cb_ud, voice);
}

static void dial_press_cb(lv_event_t *e)
{
    ui_m_dial_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root)) return;

    lv_point_t lp;
    indev_point_local(m->root, &lp);
    int32_t c = lv_obj_get_width(m->root) / 2;
    int dx = lp.x - c, dy = lp.y - c;
    int32_t rr = c - 8;
    if (dx * dx + dy * dy > rr * rr) return;          /* 圈外忽略 */

    int rel = point_angle_cw(dx, dy) - 135;
    while (rel < 0) rel += 360;
    if (rel > 270) rel = (rel > 315) ? 0 : 270;       /* 底缺口收到近端 */
    int t256 = rel * 256 / 270;

    if (m->items) {
        int n = m->n_items;
        int idx = (t256 * (n - 1) + 128) / 256;
        if (idx != m->sel) { m->sel = idx; dial_commit(m, true, true); }
    } else {
        int32_t span = m->max - m->min;
        int32_t step = m->step ? m->step : 1;
        int32_t v = m->min + (int32_t)(((int64_t)t256 * span + 128) / 256);
        v = m->min + ((v - m->min) / step) * step;    /* 量化到 step */
        if (v != m->val) { m->val = v; dial_commit(m, true, true); }
    }
}

void ui_m_dial(lv_obj_t *parent, lv_color_t accent, int32_t size, ui_m_dial_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!parent) return;
    size = clamp_i32(size, 180, 320);

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, size + 40, size + 40);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    out->root = root;
    lv_obj_set_user_data(root, out);

    lv_obj_t *arc = lv_arc_create(root);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, 20, 20);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 0);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    out->arc = arc;
    lv_obj_add_event_cb(arc, dial_press_cb, LV_EVENT_PRESSING, out);

    out->val_lbl = ui_create_label(root, "0", UI_FONT_NUM_SM, UI_COLOR_TEXT);
    lv_obj_align(out->val_lbl, LV_ALIGN_CENTER, 0, -16);
    out->lbl_a = ui_create_label(root, "-", UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_obj_align(out->lbl_a, LV_ALIGN_CENTER, 0, 18);
    out->lbl_b = ui_create_label(root, "-", UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_obj_align(out->lbl_b, LV_ALIGN_CENTER, 0, 18);
    lv_obj_add_flag(out->lbl_a, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(out->lbl_b, LV_OBJ_FLAG_HIDDEN);
    out->lbl_show = 0;
    out->min = 0; out->max = 100; out->step = 10; out->val = 0;

    lv_obj_update_layout(root);
    dial_ticks_recalc(out);
    dial_ticks_recolor(out);
}

void ui_m_dial_set_range(ui_m_dial_t *m, int32_t min, int32_t max, int32_t step)
{
    if (!m || !lv_obj_is_valid(m->root) || max <= min) return;
    if (step <= 0) step = 1;
    m->items = NULL;
    m->n_items = 0;
    m->sel = -1;
    m->min = min; m->max = max; m->step = step;
    m->val = clamp_i32(m->val, min, max);
    lv_obj_add_flag(m->lbl_a, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m->lbl_b, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(m->val_lbl, UI_FONT_NUM, 0);
    lv_obj_align(m->val_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_update_layout(m->root);
    dial_ticks_recalc(m);
    dial_commit(m, false, false);
}

void ui_m_dial_set_items(ui_m_dial_t *m, const ui_m_item_t *items, int n)
{
    if (!m || !lv_obj_is_valid(m->root) || !items || n < 1) return;
    if (n > UI_M_PIE_MAX && n > 8) n = 8;      /* 离散档位上限 8（刻度 ≤13 内） */
    m->items = items;
    m->n_items = n;
    m->sel = 0;
    lv_obj_set_style_text_font(m->val_lbl, UI_FONT_NUM_SM, 0);
    lv_obj_align(m->val_lbl, LV_ALIGN_CENTER, 0, -16);
    m->lbl_show = 0;                                   /* 双子复位到 a 位 */
    lv_obj_add_flag(m->lbl_b, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(m->lbl_a, 0, 0);
    lv_obj_set_style_opa(m->lbl_b, 0, 0);
    dial_show_text(m, items[0].zh, false);
    lv_obj_update_layout(m->root);
    dial_ticks_recalc(m);
    dial_commit(m, false, false);
}

bool ui_m_dial_set_value(ui_m_dial_t *m, int32_t v, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root)) return false;
    if (m->items) {
        if (v < 0 || v >= m->n_items) return false;
        if (v == m->sel) return false;
        m->sel = v;
    } else {
        int32_t step = m->step ? m->step : 1;
        v = clamp_i32(v, m->min, m->max);
        v = m->min + ((v - m->min) / step) * step;
        if (v == m->val) return false;
        m->val = v;
    }
    dial_commit(m, animate, false);
    return true;
}

int32_t ui_m_dial_get_value(const ui_m_dial_t *m)
{
    if (!m || !lv_obj_is_valid(m->root)) return 0;
    return m->items ? m->sel : m->val;
}

int ui_m_dial_get_index(const ui_m_dial_t *m)
{
    if (!m || !lv_obj_is_valid(m->root) || !m->items) return -1;
    return m->sel;
}

void ui_m_dial_on_sel(ui_m_dial_t *m, ui_m_sel_cb_t cb, void *user_data)
{
    if (!m) return;
    m->cb = cb;
    m->cb_ud = user_data;
}

/* ──────────────────── 1) 辐轮扇区菜单 ui_m_pie ────────────────────
 * N 个 lv_arc 楔形环段（rotation 270 → 0 区在 12 点）+ 中心 hub。
 * 输入 = 整幅透明 overlay 的极坐标判定；双采样滞回防抖。 */

static void pie_palette(lv_color_t pal[5])
{
    pal[0] = UI_COLOR_PRIMARY;
    pal[1] = UI_COLOR_SUCCESS;
    pal[2] = UI_COLOR_WARNING;
    pal[3] = UI_COLOR_DANGER;
    pal[4] = UI_COLOR_ACCENT;
}

static void pie_sector_pop(ui_m_pie_t *m, int idx, int32_t dist, uint16_t dur,
                           lv_anim_path_cb_t path)
{
    /* idx<0 仅归位所有；dist>0 弹出该区（symbol 是 arc 子对象，随动） */
    int n = m->n;
    lv_point_t off = polar_top_cw((2 * idx + 1) * 180 / n, dist);
    anim_go(m->sectors[idx], xlat_x_exec_cb,
            lv_obj_get_style_translate_x(m->sectors[idx], 0), off.x, 0, dur, path, NULL);
    anim_go(m->sectors[idx], xlat_y_exec_cb,
            lv_obj_get_style_translate_y(m->sectors[idx], 0), off.y, 0, dur, path, NULL);
}

/** 选中应用：旧区收回 FAST / 新区弹出(overshoot) + 扇区重着色 + hub 换文案 */
static void pie_apply(ui_m_pie_t *m, int idx, bool animate, bool voice)
{
    if (idx < 0 || idx >= m->n || !m->items) return;
    int old = m->sel;
    m->sel = idx;

    lv_color_t pal[5];
    pie_palette(pal);

    if (old >= 0 && old < m->n && old != idx) {
        if (animate)
            pie_sector_pop(m, old, 0, PM_T_DUR_FAST, lv_anim_path_ease_out);
        else
            lv_obj_set_style_translate_x(m->sectors[old], 0, 0);
        if (!animate) lv_obj_set_style_translate_y(m->sectors[old], 0, 0);
        lv_obj_set_style_arc_color(m->sectors[old],
                                   ui_w_tint(pal[old % 5], 90), LV_PART_INDICATOR);
    }
    if (animate)
        pie_sector_pop(m, idx, 12, PM_T_DUR_NORMAL, lv_anim_path_overshoot);
    else {
        lv_point_t off = polar_top_cw((2 * idx + 1) * 180 / m->n, 12);
        lv_obj_set_style_translate_x(m->sectors[idx], off.x, 0);
        lv_obj_set_style_translate_y(m->sectors[idx], off.y, 0);
    }
    lv_obj_set_style_arc_color(m->sectors[idx], pal[idx % 5], LV_PART_INDICATOR);

    lv_label_set_text(m->hub_title, tr(m->items[idx].zh));
    tr_bind(m->hub_title, m->items[idx].zh);
    if (m->items[idx].zh_sub) {
        lv_label_set_text(m->hub_sub, tr(m->items[idx].zh_sub));
        tr_bind(m->hub_sub, m->items[idx].zh_sub);
    } else {
        lv_label_set_text(m->hub_sub, "");
    }

    sel_notify(m->root, idx, m->cb, m->cb_ud, voice);
}

/** overlay 极坐标判区（返回 -1 = hub 区/无效） */
static int pie_index_at(ui_m_pie_t *m, lv_point_t lp)
{
    int32_t c = lv_obj_get_width(m->root) / 2;
    int dx = lp.x - c, dy = lp.y - c;
    int32_t hub_r = (lv_obj_get_width(m->root) - 104) / 2 + 6;
    if (dx * dx + dy * dy < hub_r * hub_r) return -1;
    return ((point_angle_cw(dx, dy) + 90) % 360) * m->n / 360;
}

static void pie_press_cb(lv_event_t *e)
{
    ui_m_pie_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root) || !m->items) return;
    lv_point_t lp;
    indev_point_local(m->root, &lp);
    int idx = pie_index_at(m, lp);
    if (idx < 0) return;
    if (idx != m->pend) {                /* 双采样滞回：连续两帧同区才提交 */
        m->pend = idx;
        m->pend_cnt = 0;
        return;
    }
    m->pend_cnt++;
    if (m->pend_cnt >= 2 && idx != m->sel)
        pie_apply(m, idx, true, true);
}

static void pie_pressed_cb(lv_event_t *e)      /* 记录起点（tap/drag 判定） */
{
    ui_m_pie_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    indev_point_local(m->root, &m->press_pt);
}

static void pie_click_cb(lv_event_t *e)
{
    ui_m_pie_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root) || !m->items) return;
    lv_point_t lp;
    indev_point_local(m->root, &lp);
    int ddx = lp.x - m->press_pt.x, ddy = lp.y - m->press_pt.y;
    if (ddx * ddx + ddy * ddy > 144) return;   /* 位移 >12px = 拖动，不是点选 */
    int idx = pie_index_at(m, lp);
    if (idx < 0 || idx >= m->n) return;
    if (idx != m->sel) pie_apply(m, idx, true, true);
    else               sel_notify(m->root, idx, m->cb, m->cb_ud, true);  /* 原区轻点=确认 */
}

void ui_m_pie(lv_obj_t *parent, lv_color_t accent, int32_t size,
              const ui_m_item_t *items, int n, ui_m_pie_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!parent) return;
    size = clamp_i32(size, 220, 360);
    if (n < 0) n = 0;
    n = clamp_i32(n, 0, UI_M_PIE_MAX);
    out->n = n;
    out->sel = -1;
    out->pend = -1;
    out->items = items;

    lv_color_t pal[5];
    pie_palette(pal);

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, size, size);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    out->root = root;
    lv_obj_set_user_data(root, out);

    for (int i = 0; i < n; i++) {
        lv_obj_t *arc = lv_arc_create(root);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_set_size(arc, size, size);
        lv_arc_set_rotation(arc, 270);                       /* 0 区在 12 点 */
        lv_arc_set_bg_angles(arc, i * 360 / n + 3, (i + 1) * 360 / n - 3);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 100);                          /* indic 满环段=扇区本体 */
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
        lv_obj_set_style_arc_width(arc, 36, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 36, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, ui_w_tint(pal[i % 5], 18), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, ui_w_tint(pal[i % 5], 90), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        out->sectors[i] = arc;

        if (items && items[i].symbol) {                      /* symbol 挂 arc 下，随弹出位移 */
            lv_point_t pt = polar_top_cw((2 * i + 1) * 180 / n, size / 2 - 18);
            lv_obj_t *sym = ui_create_label(arc, items[i].symbol, UI_FONT_SYMBOL, pal[i % 5]);
            lv_obj_align(sym, LV_ALIGN_CENTER, pt.x, pt.y);
        }
    }

    lv_obj_t *hub = lv_obj_create(root);
    lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, size - 104, size - 104);
    lv_obj_align(hub, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hub, 1, 0);
    lv_obj_set_style_border_color(hub, accent, 0);
    lv_obj_set_flex_flow(hub, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hub, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hub, 4, 0);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_CLICKABLE);
    out->hub = hub;
    out->hub_title = ui_create_label(hub, "-", UI_FONT_TITLE, UI_COLOR_TEXT);
    out->hub_sub = ui_create_label(hub, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

    lv_obj_t *overlay = lv_obj_create(root);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, size, size);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    out->overlay = overlay;
    lv_obj_add_event_cb(overlay, pie_pressed_cb, LV_EVENT_PRESSED, out);
    lv_obj_add_event_cb(overlay, pie_press_cb, LV_EVENT_PRESSING, out);
    lv_obj_add_event_cb(overlay, pie_click_cb, LV_EVENT_CLICKED, out);

    if (items && n > 0) pie_apply(out, 0, false, false);
}

bool ui_m_pie_set(ui_m_pie_t *m, int sel, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root) || !m->items) return false;
    if (sel < 0 || sel >= m->n || sel == m->sel) return false;
    pie_apply(m, sel, animate, false);
    return true;
}

int ui_m_pie_get(const ui_m_pie_t *m)
{
    if (!m || !lv_obj_is_valid(m->root)) return -1;
    return m->sel;
}

void ui_m_pie_on_sel(ui_m_pie_t *m, ui_m_sel_cb_t cb, void *user_data)
{
    if (!m) return;
    m->cb = cb;
    m->cb_ud = user_data;
}

/* ──────────────────── 4) 中心聚焦列表 ui_m_focus ────────────────────
 * 纵向 snap CENTER；行位姿 = 离视口中心距离的解析函数（不查子对象坐标）。
 * d = pad_top + i·86 + 38 − sy − vh/2；u = min(|d|,170)/170；
 * scale 300−104u / opa 255−120u / translate_x 24u；|d|>280 far_state 一次定型。 */

static void focus_style_row(ui_m_focus_t *m, int i, int32_t d)
{
    lv_obj_t *row = m->rows[i];
    int32_t ad = d < 0 ? -d : d;
    int32_t u256 = ad > 170 ? 256 : ad * 256 / 170;

    if (ad > 280) {                            /* 远行：定型一次，风暴期零开销 */
        if (!m->far_state[i]) {
            m->far_state[i] = 1;
            if (s_scale_fx) scale_exec_cb(row, 196);
            lv_obj_set_style_opa(row, 135, 0);
            lv_obj_set_style_translate_x(row, 24, 0);
        }
        return;
    }
    m->far_state[i] = 0;

    int32_t sc = 300 - 104 * u256 / 256;
    int32_t op = 255 - 120 * u256 / 256;
    int32_t tx = 24 * u256 / 256;

    if (s_scale_fx) {
        lv_obj_set_style_transform_pivot_x(row, lv_obj_get_width(row) / 2, 0);
        lv_obj_set_style_transform_pivot_y(row, 38, 0);
        lv_obj_set_style_transform_scale_x(row, sc, 0);
        lv_obj_set_style_transform_scale_y(row, sc, 0);
    }
    lv_obj_set_style_opa(row, (lv_opa_t)op, 0);
    lv_obj_set_style_translate_x(row, tx, 0);

    /* 选中视觉：badge 满色 / 标题亮色 */
    bool is_sel = ad < 43;
    lv_color_t ac = m->accent_v;
    lv_obj_set_style_bg_color(m->badges[i], is_sel ? ac : ui_w_tint(ac, 26), 0);
    lv_obj_t *sym = lv_obj_get_child(m->badges[i], 0);
    if (sym) lv_obj_set_style_text_color(sym, is_sel ? fg_on(ac) : ac, 0);
    lv_obj_set_style_text_color(m->titles[i], is_sel ? UI_COLOR_TEXT : UI_COLOR_TEXT_SEC, 0);
}

static void focus_derive(ui_m_focus_t *m)
{
    int32_t sy = lv_obj_get_scroll_y(m->root);
    int32_t half = m->vh / 2;
    for (int i = 0; i < m->n; i++)
        focus_style_row(m, i, m->pad_top + i * 86 + 38 - sy - half);
}

static void focus_scroll_cb(lv_event_t *e)
{
    ui_m_focus_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    focus_derive(m);
}

static void focus_scroll_end_cb(lv_event_t *e)
{
    ui_m_focus_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    int32_t sy = lv_obj_get_scroll_y(m->root);
    int32_t num = sy + m->vh / 2 - m->pad_top - 38;
    int j = clamp_i32((num + 43) / 86, 0, m->n - 1);
    if (j != m->sel) {
        m->sel = j;
        focus_derive(m);
        sel_notify(m->root, j, m->cb, m->cb_ud, true);
    }
}

void ui_m_focus(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
                int n, int32_t viewport_h, ui_m_focus_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!parent || !items || n < 1) return;
    n = clamp_i32(n, 1, UI_M_FOCUS_MAX);
    viewport_h = clamp_i32(viewport_h, 260, 560);
    out->n = n;
    out->sel = 0;
    out->vh = viewport_h;
    out->pad_top = (viewport_h - 76) / 2;
    out->items = items;
    out->accent_v = accent;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), viewport_h);
    lv_obj_set_style_pad_left(root, 4, 0);
    lv_obj_set_style_pad_right(root, 4, 0);
    lv_obj_set_style_pad_top(root, out->pad_top, 0);
    lv_obj_set_style_pad_bottom(root, out->pad_top, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 10, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(root, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    out->root = root;
    lv_obj_set_user_data(root, out);
    lv_obj_add_event_cb(root, focus_scroll_cb, LV_EVENT_SCROLL, out);
    lv_obj_add_event_cb(root, focus_scroll_end_cb, LV_EVENT_SCROLL_END, out);

    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(root);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 76);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_set_style_pad_left(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
        out->rows[i] = row;

        lv_obj_t *badge = lv_obj_create(row);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 44, 44);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(badge, ui_w_tint(accent, 26), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(badge, 1, 0);
        lv_obj_set_style_border_color(badge, UI_COLOR_BORDER, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
        out->badges[i] = badge;
        lv_obj_t *sym = ui_create_label(badge, items[i].symbol ? items[i].symbol : "",
                                        UI_FONT_SYMBOL, accent);
        lv_obj_center(sym);              /* remove_style_all 后 label 默认左上，须钉圆心 */

        lv_obj_t *col = lv_obj_create(row);
        lv_obj_remove_style_all(col);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_size(col, 0, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(col, 2, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        out->titles[i] = ui_create_label_bind(col, items[i].zh, UI_FONT_BODY, UI_COLOR_TEXT);
        if (items[i].zh_sub)
            ui_create_label_bind(col, items[i].zh_sub, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    }

    lv_obj_update_layout(root);
    focus_derive(out);
}

bool ui_m_focus_set(ui_m_focus_t *m, int sel, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root)) return false;
    if (sel < 0 || sel >= m->n || sel == m->sel) return false;
    m->sel = sel;
    lv_obj_scroll_to_y(m->root, m->pad_top + sel * 86 + 38 - m->vh / 2,
                       animate ? LV_ANIM_ON : LV_ANIM_OFF);
    focus_derive(m);
    sel_notify(m->root, sel, m->cb, m->cb_ud, false);
    return true;
}

int ui_m_focus_get(const ui_m_focus_t *m)
{
    if (!m || !lv_obj_is_valid(m->root)) return -1;
    return m->sel;
}

void ui_m_focus_on_sel(ui_m_focus_t *m, ui_m_sel_cb_t cb, void *user_data)
{
    if (!m) return;
    m->cb = cb;
    m->cb_ud = user_data;
}

/* ──────────────────── 6) 封面流转 ui_m_flow ────────────────────
 * 横向 snap CENTER + SCROLL_ONE；pads=(W−220)/2 让首尾卡可居中
 * → 卡 i 居中恰好 sx = i·244（解析式零补偿）。
 * 侧卡 scale 256−44u / opa 255−96u / translate_y 16u²（二次下沉）。 */

static void flow_derive(ui_m_flow_t *m)
{
    int32_t sx = lv_obj_get_scroll_x(m->root);
    for (int i = 0; i < m->n; i++) {
        int32_t kq = (int32_t)(((int64_t)(i * 244 - sx) * 256) / 244);
        int32_t aq = kq < 0 ? -kq : kq;
        if (aq > 640) continue;                    /* 离屏，免样式 */
        int32_t u256 = aq > 512 ? 256 : aq / 2;
        int32_t sc = 256 - 44 * u256 / 256;
        int32_t op = 255 - 96 * u256 / 256;
        int32_t q = u256 * u256 / 256;
        int32_t ty = 16 * q / 256;

        lv_obj_t *card = m->cards[i];
        if (s_scale_fx) {
            lv_obj_set_style_transform_pivot_x(card, 110, 0);
            lv_obj_set_style_transform_pivot_y(card, 80, 0);
            lv_obj_set_style_transform_scale_x(card, sc, 0);
            lv_obj_set_style_transform_scale_y(card, sc, 0);
        }
        lv_obj_set_style_opa(card, (lv_opa_t)op, 0);
        lv_obj_set_style_translate_y(card, ty, 0);
        lv_obj_set_style_border_color(card, aq < 64 ? m->accent_v : UI_COLOR_BORDER, 0);
    }
}

static void flow_seed(ui_m_flow_t *m)
{
    lv_obj_update_layout(m->root);
    int32_t w = lv_obj_get_width(m->root);
    if (w < 240) return;
    int32_t pad = (w - 220) / 2;
    lv_obj_set_style_pad_left(m->root, pad, 0);
    lv_obj_set_style_pad_right(m->root, pad, 0);
    lv_obj_update_layout(m->root);
    lv_obj_scroll_to_x(m->root, 0, LV_ANIM_OFF);   /* 卡 0 居中 */
    m->seeded = 1;
    m->last_sx = 0;
    flow_derive(m);
}

static void flow_scroll_cb(lv_event_t *e)
{
    ui_m_flow_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    if (!m->seeded) { flow_seed(m); return; }
    int32_t sx = lv_obj_get_scroll_x(m->root);
    if (sx - m->last_sx < 2 && sx - m->last_sx > -2) return;
    m->last_sx = sx;
    flow_derive(m);
}

static void flow_scroll_end_cb(lv_event_t *e)
{
    ui_m_flow_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    if (!m->seeded) { flow_seed(m); return; }
    int32_t sx = lv_obj_get_scroll_x(m->root);
    m->last_sx = sx;
    int j = clamp_i32((sx + 122) / 244, 0, m->n - 1);
    if (j != m->sel) {
        m->sel = j;
        flow_derive(m);
        sel_notify(m->root, j, m->cb, m->cb_ud, true);
    }
}

static void flow_card_click_cb(lv_event_t *e)
{
    ui_m_flow_t *m = lv_event_get_user_data(e);
    lv_obj_t *card = lv_event_get_target(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    int j = -1;
    for (int i = 0; i < m->n; i++) if (m->cards[i] == card) { j = i; break; }
    if (j < 0) return;
    int32_t sx = lv_obj_get_scroll_x(m->root);
    if (sx - j * 244 < 8 && sx - j * 244 > -8) {
        if (m->sel != j) {
            m->sel = j;
            flow_derive(m);
        }
        sel_notify(m->root, j, m->cb, m->cb_ud, true);
    } else {
        lv_obj_scroll_to_x(m->root, j * 244, LV_ANIM_ON);   /* 原生物理滚动 */
    }
}

void ui_m_flow(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
               int n, ui_m_flow_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!parent || !items || n < 1) return;
    n = clamp_i32(n, 1, UI_M_FLOW_MAX);
    out->n = n;
    out->sel = 0;
    out->items = items;
    out->accent_v = accent;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), 210);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(root, 24, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(root, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    out->root = root;
    lv_obj_set_user_data(root, out);
    lv_obj_add_event_cb(root, flow_scroll_cb, LV_EVENT_SCROLL, out);
    lv_obj_add_event_cb(root, flow_scroll_end_cb, LV_EVENT_SCROLL_END, out);

    for (int i = 0; i < n; i++) {
        out->cards[i] = make_cell(root, accent, &items[i], 220, 160, true);
        lv_obj_add_event_cb(out->cards[i], flow_card_click_cb, LV_EVENT_CLICKED, out);
    }

    flow_seed(out);
}

bool ui_m_flow_set(ui_m_flow_t *m, int sel, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root)) return false;
    if (sel < 0 || sel >= m->n || sel == m->sel) return false;
    if (!m->seeded) flow_seed(m);
    m->sel = sel;
    lv_obj_scroll_to_x(m->root, sel * 244, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    flow_derive(m);
    sel_notify(m->root, sel, m->cb, m->cb_ud, false);
    return true;
}

int ui_m_flow_get(const ui_m_flow_t *m)
{
    if (!m || !lv_obj_is_valid(m->root)) return -1;
    return m->sel;
}

void ui_m_flow_on_sel(ui_m_flow_t *m, ui_m_sel_cb_t cb, void *user_data)
{
    if (!m) return;
    m->cb = cb;
    m->cb_ud = user_data;
}

/* ──────────────────── 3) 无限循环轮播 ui_m_loop ────────────────────
 * n 条目 × 3 份克隆手动定位（不用 flex——布局引擎会和 snap 抢行位）。
 * 换页只在 SCROLL_END：j 越出 [n,2n) 即 scroll_to_x(sx∓n·136, OFF)，
 * 精确周期 → 像素级同帧。动量期间不跳（scroll_to_x 会杀原生动量）。 */

static void loop_derive(ui_m_loop_t *m)
{
    int32_t sx = lv_obj_get_scroll_x(m->root);
    int32_t w = lv_obj_get_width(m->root);
    int32_t half = w / 2;
    int total = m->n * 3;
    for (int j = 0; j < total; j++) {
        int32_t kq = (int32_t)(((int64_t)(j * m->pitch + 56 - sx - half) * 256) / m->pitch);
        int32_t aq = kq < 0 ? -kq : kq;
        if (aq > 563) continue;                    /* |k|>2.2 离屏 */
        int32_t u256 = aq > 512 ? 256 : aq / 2;
        int32_t sc = 300 - 86 * u256 / 256;
        int32_t op = 255 - 95 * u256 / 256;

        lv_obj_t *cell = m->cells[j];
        if (s_scale_fx) {
            lv_obj_set_style_transform_pivot_x(cell, 56, 0);
            lv_obj_set_style_transform_pivot_y(cell, 80, 0);
            lv_obj_set_style_transform_scale_x(cell, sc, 0);
            lv_obj_set_style_transform_scale_y(cell, sc, 0);
        } else {
            lv_obj_set_style_translate_y(cell, 6 * u256 / 256, 0);
        }
        lv_obj_set_style_opa(cell, (lv_opa_t)op, 0);
    }
}

static void loop_seed(ui_m_loop_t *m)
{
    lv_obj_update_layout(m->root);
    int32_t w = lv_obj_get_width(m->root);
    if (w < 120) return;
    lv_obj_scroll_to_x(m->root, m->n * m->pitch + 56 - w / 2, LV_ANIM_OFF);
    m->seeded = 1;
    m->last_sx = m->n * m->pitch + 56 - w / 2;
    loop_derive(m);
}

static void loop_scroll_cb(lv_event_t *e)
{
    ui_m_loop_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    if (!m->seeded) { loop_seed(m); return; }
    int32_t sx = lv_obj_get_scroll_x(m->root);
    if (sx - m->last_sx < 2 && sx - m->last_sx > -2) return;
    m->last_sx = sx;
    loop_derive(m);
}

static void loop_scroll_end_cb(lv_event_t *e)
{
    ui_m_loop_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root) || m->n < 2) return;
    if (!m->seeded) { loop_seed(m); return; }

    int32_t sx = lv_obj_get_scroll_x(m->root);
    int32_t w = lv_obj_get_width(m->root);
    int32_t num = sx + w / 2 - 56;
    int j = num >= 0 ? (num + m->pitch / 2) / m->pitch : 0;

    if (j < m->n || j >= 2 * m->n) {               /* 无声换页：精确周期跳回 */
        int32_t jump = (j < m->n) ? (int32_t)m->n * m->pitch : -(int32_t)m->n * m->pitch;
        lv_obj_scroll_to_x(m->root, sx + jump, LV_ANIM_OFF);
        sx += jump;
        j = (sx + w / 2 - 56 + m->pitch / 2) / m->pitch;
    }
    m->last_sx = sx;

    int sel = j % m->n;
    if (sel != m->sel) {
        m->sel = sel;
        loop_derive(m);
        sel_notify(m->root, sel, m->cb, m->cb_ud, true);
    } else {
        loop_derive(m);
    }
}

static void loop_cell_click_cb(lv_event_t *e)
{
    ui_m_loop_t *m = lv_event_get_user_data(e);
    lv_obj_t *cell = lv_event_get_target(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    int j = -1;
    int total = m->n * 3;
    for (int i = 0; i < total; i++) if (m->cells[i] == cell) { j = i; break; }
    if (j < 0) return;
    int32_t w = lv_obj_get_width(m->root);
    int32_t target = j * m->pitch + 56 - w / 2;
    int32_t sx = lv_obj_get_scroll_x(m->root);
    if (sx - target < 8 && sx - target > -8) {
        int sel = j % m->n;
        if (sel != m->sel) m->sel = sel;
        loop_derive(m);
        sel_notify(m->root, sel, m->cb, m->cb_ud, true);
    } else {
        lv_obj_scroll_to_x(m->root, target, LV_ANIM_ON);
    }
}

void ui_m_loop(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
               int n, ui_m_loop_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!parent || !items || n < 2) return;
    n = clamp_i32(n, 2, UI_M_LOOP_MAX);
    out->n = n;
    out->sel = 0;
    out->pitch = 136;
    out->items = items;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), 190);
    lv_obj_set_scroll_dir(root, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(root, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    out->root = root;
    lv_obj_set_user_data(root, out);
    lv_obj_add_event_cb(root, loop_scroll_cb, LV_EVENT_SCROLL, out);
    lv_obj_add_event_cb(root, loop_scroll_end_cb, LV_EVENT_SCROLL_END, out);

    for (int j = 0; j < n * 3; j++) {
        const ui_m_item_t *it = &items[j % n];
        lv_obj_t *cell = make_cell(root, accent, it, 112, 160, false);
        lv_obj_set_pos(cell, j * 136, 15);
        lv_obj_add_event_cb(cell, loop_cell_click_cb, LV_EVENT_CLICKED, out);
        out->cells[j] = cell;
    }

    loop_seed(out);
}

bool ui_m_loop_set(ui_m_loop_t *m, int sel, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root)) return false;
    if (sel < 0 || sel >= m->n || sel == m->sel) return false;
    if (!m->seeded) loop_seed(m);
    m->sel = sel;
    int32_t w = lv_obj_get_width(m->root);
    int j = sel + m->n;                            /* 滚到中拷贝等价位 */
    lv_obj_scroll_to_x(m->root, j * m->pitch + 56 - w / 2,
                       animate ? LV_ANIM_ON : LV_ANIM_OFF);
    loop_derive(m);
    sel_notify(m->root, sel, m->cb, m->cb_ud, false);
    return true;
}

int ui_m_loop_get(const ui_m_loop_t *m)
{
    if (!m || !lv_obj_is_valid(m->root)) return -1;
    return m->sel;
}

void ui_m_loop_on_sel(ui_m_loop_t *m, ui_m_sel_cb_t cb, void *user_data)
{
    if (!m) return;
    m->cb = cb;
    m->cb_ud = user_data;
}

/* ──────────────────── 5) 鱼眼程序坞 ui_m_dock ────────────────────
 * 不可滚条带；PRESSING 逐帧 Bederson 鱼眼（s=256+peak·cos(u·90°)，
 * 累计槽宽 w=base·s/256 重排中心），RELEASED 弹簧回位。
 * 静息 pitch 自适应预留放大余量，峰值受 (W−56)−(n−1)·pitch 钳制，
 * 条带永不溢出根边界。 */

static void dock_exec_release_cb(void *var, int32_t p);   /* 前向声明 */

static int32_t dock_rest_pitch(ui_m_dock_t *m)
{
    if (m->n < 2) return 64;
    return m->rest_x[1] - m->rest_x[0];
}

/** 手指位姿全量重排（纯整数，n≤10 次循环零分配） */
static void dock_apply(ui_m_dock_t *m, int32_t fx)
{
    int32_t w = lv_obj_get_width(m->root);
    if (!m->rest_ok || w < 120) return;
    int n = m->n;
    int32_t base = dock_rest_pitch(m);
    int32_t slack = w - 56 - (n - 1) * base;
    int32_t peak = 256 + (slack > 0 ? min_i32(230, slack * 256 / base) : 0);

    int32_t s[UI_M_DOCK_MAX], wi[UI_M_DOCK_MAX], total = 0;
    for (int i = 0; i < n; i++) {
        int32_t dx = fx - m->rest_x[i];
        int32_t ad = dx < 0 ? -dx : dx;
        int32_t u256 = ad > 140 ? 256 : ad * 256 / 140;
        int32_t c90 = 90 - u256 * 90 / 256;          /* cos(u·90°) = sin(90−u·90°) */
        s[i] = 256 + (int32_t)((int64_t)(peak - 256) * lv_trigo_sin(c90) / 32768);
        wi[i] = base * s[i] / 256;
        total += wi[i];
    }
    int32_t cum = 0;
    for (int i = 0; i < n; i++) {
        int32_t cx = w / 2 + (cum + wi[i] / 2 - total / 2);
        cum += wi[i];
        lv_obj_t *ic = m->icons[i];
        lv_anim_delete(ic, dock_exec_release_cb);    /* 重按杀回弹 */
        lv_obj_set_style_translate_x(ic, cx - m->rest_x[i], 0);
        if (s_scale_fx) {
            lv_obj_set_style_transform_pivot_x(ic, 28, 0);
            lv_obj_set_style_transform_pivot_y(ic, 28, 0);
            lv_obj_set_style_transform_scale_x(ic, s[i], 0);
            lv_obj_set_style_transform_scale_y(ic, s[i], 0);
        }
    }
}

/** 选中态视觉（满色徽章 + 对比符号） */
static void dock_style_sel(ui_m_dock_t *m)
{
    for (int i = 0; i < m->n; i++) {
        lv_obj_t *ic = m->icons[i];
        lv_color_t ac = m->pal_v[i % 5];
        bool on = i == m->sel;
        lv_obj_set_style_bg_color(ic, on ? ac : ui_w_tint(ac, 26), 0);
        lv_obj_t *sym = lv_obj_get_child(ic, 0);
        if (sym) lv_obj_set_style_text_color(sym, on ? fg_on(ac) : ac, 0);
    }
}

static void dock_press_cb(lv_event_t *e)
{
    ui_m_dock_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root) || !m->rest_ok) return;
    lv_point_t lp;
    indev_point_local(m->root, &lp);
    dock_apply(m, lp.x);
}

static ui_m_dock_t *s_dock_anim_owner;
static int32_t s_dock_tx0[UI_M_DOCK_MAX];
static int32_t s_dock_s0[UI_M_DOCK_MAX];

static int dock_index_of(ui_m_dock_t *m, lv_obj_t *ic)
{
    for (int i = 0; i < m->n; i++) if (m->icons[i] == ic) return i;
    return -1;
}

static void dock_exec_release_cb(void *var, int32_t p)   /* p 0..1000 */
{
    lv_obj_t *ic = var;
    ui_m_dock_t *m = s_dock_anim_owner;
    if (!m) return;
    int j = dock_index_of(m, ic);
    if (j < 0) return;
    int32_t tx = s_dock_tx0[j] * (1000 - p) / 1000;
    int32_t s = s_dock_s0[j] + (256 - s_dock_s0[j]) * p / 1000;
    lv_obj_set_style_translate_x(ic, tx, 0);
    if (s_scale_fx) {
        lv_obj_set_style_transform_pivot_x(ic, 28, 0);
        lv_obj_set_style_transform_pivot_y(ic, 28, 0);
        lv_obj_set_style_transform_scale_x(ic, s, 0);
        lv_obj_set_style_transform_scale_y(ic, s, 0);
    }
}

static void dock_release_done_cb(lv_anim_t *a)
{
    lv_obj_set_style_translate_x(a->var, 0, 0);
    if (s_scale_fx) {
        lv_obj_set_style_transform_scale_x(a->var, 256, 0);
        lv_obj_set_style_transform_scale_y(a->var, 256, 0);
    }
}

static void dock_release_cb(lv_event_t *e)
{
    ui_m_dock_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root) || !m->rest_ok) return;
    if (s_dock_anim_owner != m) {                    /* 双坞并发：接管并清场 */
        lv_anim_delete(NULL, dock_exec_release_cb);
        s_dock_anim_owner = m;
    }
    for (int i = 0; i < m->n; i++) {
        lv_obj_t *ic = m->icons[i];
        s_dock_tx0[i] = lv_obj_get_style_translate_x(ic, 0);
        s_dock_s0[i] = lv_obj_get_style_transform_scale_x(ic, 0);
        anim_go(ic, dock_exec_release_cb, 0, 1000, 0, PM_T_DUR_SLOW,
                lv_anim_path_overshoot, dock_release_done_cb);
    }
}

static void dock_pressed_cb(lv_event_t *e)
{
    ui_m_dock_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    indev_point_local(m->root, &m->press_pt);
}

static void dock_pulse2_cb(lv_anim_t *a)
{
    anim_go(a->var, scale_exec_cb, 294, 256, 0, PM_T_DUR_FAST,
            lv_anim_path_ease_in_out, NULL);
}

static void dock_click_cb(lv_event_t *e)
{
    ui_m_dock_t *m = lv_event_get_user_data(e);
    lv_obj_t *ic = lv_event_get_target(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    lv_point_t lp;
    indev_point_local(m->root, &lp);
    int ddx = lp.x - m->press_pt.x, ddy = lp.y - m->press_pt.y;
    if (ddx * ddx + ddy * ddy > 144) return;         /* 拖压非点选 */

    int j = dock_index_of(m, ic);
    if (j < 0) return;
    m->sel = j;
    dock_style_sel(m);

    lv_anim_delete(ic, NULL);                        /* 清回弹/旧脉冲 */
    lv_obj_set_style_translate_x(ic, 0, 0);
    if (s_scale_fx) {
        lv_obj_set_style_transform_pivot_x(ic, 28, 0);
        lv_obj_set_style_transform_pivot_y(ic, 28, 0);
        anim_go(ic, scale_exec_cb, 256, 294, 0, PM_T_DUR_FAST,
                lv_anim_path_ease_out, dock_pulse2_cb);
    }
    sel_notify(m->root, j, m->cb, m->cb_ud, true);
}

void ui_m_dock(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
               int n, ui_m_dock_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!parent || !items || n < 2) return;
    n = clamp_i32(n, 2, UI_M_DOCK_MAX);
    out->n = n;
    out->sel = -1;
    out->items = items;
    (void)accent;                                    /* 图标用循环调色板 */

    lv_color_t pal[5];
    pie_palette(pal);
    for (int i = 0; i < 5; i++) out->pal_v[i] = pal[i];

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), 120);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);    /* 条带空白区也跟踪手指 */
    out->root = root;
    lv_obj_set_user_data(root, out);
    lv_obj_add_event_cb(root, dock_pressed_cb, LV_EVENT_PRESSED, out);
    lv_obj_add_event_cb(root, dock_press_cb, LV_EVENT_PRESSING, out);
    lv_obj_add_event_cb(root, dock_release_cb, LV_EVENT_RELEASED, out);

    for (int i = 0; i < n; i++) {
        lv_color_t ac = pal[i % 5];
        lv_obj_t *ic = lv_obj_create(root);
        lv_obj_remove_style_all(ic);
        lv_obj_set_size(ic, 56, 56);
        lv_obj_set_style_radius(ic, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ic, ui_w_tint(ac, 26), 0);
        lv_obj_set_style_bg_opa(ic, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ic, 1, 0);
        lv_obj_set_style_border_color(ic, UI_COLOR_BORDER, 0);
        lv_obj_add_flag(ic, LV_OBJ_FLAG_CLICKABLE);
        out->icons[i] = ic;
        lv_obj_t *sym = ui_create_label(ic, items[i].symbol ? items[i].symbol : "",
                                        UI_FONT_SYMBOL, ac);
        lv_obj_center(sym);              /* remove_style_all 后 label 默认左上，须钉圆心 */
        lv_obj_add_event_cb(ic, dock_pressed_cb, LV_EVENT_PRESSED, out);
        lv_obj_add_event_cb(ic, dock_press_cb, LV_EVENT_PRESSING, out);
        lv_obj_add_event_cb(ic, dock_release_cb, LV_EVENT_RELEASED, out);
        lv_obj_add_event_cb(ic, dock_click_cb, LV_EVENT_CLICKED, out);
    }

    lv_obj_update_layout(root);
    int32_t w = lv_obj_get_width(root);
    if (w >= 120) {
        int32_t pitch = clamp_i32((w - 116) / (n - 1), 40, 64);
        for (int i = 0; i < n; i++) {
            out->rest_x[i] = w / 2 + (int32_t)((2 * i - n + 1) * pitch) / 2;
            lv_obj_set_pos(out->icons[i], out->rest_x[i] - 28, 32);
        }
        out->rest_ok = 1;
    }
    out->sel = 0;
    dock_style_sel(out);
}

bool ui_m_dock_set(ui_m_dock_t *m, int sel, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root)) return false;
    if (sel < 0 || sel >= m->n || sel == m->sel) return false;
    m->sel = sel;
    dock_style_sel(m);
    if (animate && s_scale_fx) {
        lv_obj_t *ic = m->icons[sel];
        lv_anim_delete(ic, NULL);
        lv_obj_set_style_translate_x(ic, 0, 0);
        anim_go(ic, scale_exec_cb, 256, 294, 0, PM_T_DUR_FAST,
                lv_anim_path_ease_out, dock_pulse2_cb);
    }
    sel_notify(m->root, sel, m->cb, m->cb_ud, false);
    return true;
}

int ui_m_dock_get(const ui_m_dock_t *m)
{
    if (!m || !lv_obj_is_valid(m->root)) return -1;
    return m->sel;
}

void ui_m_dock_on_sel(ui_m_dock_t *m, ui_m_sel_cb_t cb, void *user_data)
{
    if (!m) return;
    m->cb = cb;
    m->cb_ud = user_data;
}

/* ════════════════════════ 第二批：效果件 + 三控件 ════════════════════════ */

/* ──────────────────── 效果件 1: ripple 按下涟漪 ────────────────────
 * set_size + 锚点重定位逐帧驱动（不用 transform_scale，不受降级开关影响）。
 * 触点存在 user_data（lv_malloc），圆删时释放。并发 ≤4 防连点堆积。 */

#define RIPPLE_MAX_LIVE 4
static int s_ripple_live;

static void ripple_done_cb(lv_anim_t *a)
{
    lv_point_t *pt = lv_obj_get_user_data(a->var);
    if (pt) lv_free(pt);
    lv_obj_delete(a->var);          /* 同 obj 的 size anim 随之撤销 */
    if (s_ripple_live > 0) s_ripple_live--;
}

static void ripple_exec_cb(void *var, int32_t d)        /* d = 直径 */
{
    lv_point_t *pt = lv_obj_get_user_data(var);
    if (!pt) return;
    lv_obj_set_size(var, d, d);
    lv_obj_set_pos(var, pt->x - d / 2, pt->y - d / 2);
}

static void ripple_press_cb(lv_event_t *e)
{
    lv_obj_t *host = lv_event_get_target(e);
    if (s_ripple_live >= RIPPLE_MAX_LIVE) return;

    lv_point_t lp;
    indev_point_local(host, &lp);

    lv_obj_t *c = lv_obj_create(host);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 24, 24);
    lv_obj_set_pos(c, lp.x - 12, lp.y - 12);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_30, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT);   /* 宿主可能是 flex 容器 */

    lv_point_t *pt = lv_malloc(sizeof(lv_point_t));
    if (!pt) { lv_obj_delete(c); return; }
    pt->x = lp.x;
    pt->y = lp.y;
    lv_obj_set_user_data(c, pt);

    int32_t w = lv_obj_get_width(host), h = lv_obj_get_height(host);
    int32_t span = (w > h ? w : h) * 3 / 2;
    s_ripple_live++;
    anim_go(c, ripple_exec_cb, 24, span, 0, PM_T_DUR_SLOW,
            lv_anim_path_ease_out, NULL);
    anim_go(c, opa_exec_cb, LV_OPA_30, 0, 0, PM_T_DUR_SLOW,
            lv_anim_path_ease_out, ripple_done_cb);
}

void ui_m_ripple(lv_obj_t *obj)
{
    if (!obj || !lv_obj_is_valid(obj)) return;
    lv_obj_add_event_cb(obj, ripple_press_cb, LV_EVENT_PRESSED, NULL);
}

/* ──────────────────── 效果件 2: squish 按压果冻 ────────────────────
 * PRESSED 压扁（x↓y↑，底心 pivot 贴地感），松手 overshoot 弹回。
 * 非均匀 scale 走独立 exec（共享 scale_exec_cb 是等比的）。 */

static void squish_exec_x(void *var, int32_t v) { lv_obj_set_style_transform_scale_x(var, v, 0); }
static void squish_exec_y(void *var, int32_t v) { lv_obj_set_style_transform_scale_y(var, v, 0); }

static void squish_press_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    lv_obj_set_style_transform_pivot_x(o, lv_obj_get_width(o) / 2, 0);
    lv_obj_set_style_transform_pivot_y(o, lv_obj_get_height(o), 0);
    anim_go(o, squish_exec_x, lv_obj_get_style_transform_scale_x(o, 0), 218, 0,
            PM_T_DUR_FAST, lv_anim_path_ease_out, NULL);
    anim_go(o, squish_exec_y, lv_obj_get_style_transform_scale_y(o, 0), 292, 0,
            PM_T_DUR_FAST, lv_anim_path_ease_out, NULL);
}

static void squish_release_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    anim_go(o, squish_exec_x, lv_obj_get_style_transform_scale_x(o, 0), 256, 0,
            PM_T_DUR_NORMAL, lv_anim_path_overshoot, NULL);
    anim_go(o, squish_exec_y, lv_obj_get_style_transform_scale_y(o, 0), 256, 0,
            PM_T_DUR_NORMAL, lv_anim_path_overshoot, NULL);
}

void ui_m_squish(lv_obj_t *obj)
{
    if (!obj || !lv_obj_is_valid(obj)) return;
    lv_obj_add_event_cb(obj, squish_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, squish_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(obj, squish_release_cb, LV_EVENT_PRESS_LOST, NULL);
}

/* ──────────────────── 效果件 3: odo 里程表翻数器 ────────────────────
 * 每位 = clip 列 + 双十年条带("0..9\n0..9")，显示 d = translate_y -d·line_h。
 * 只前向滚（机械感）：steps=(d'-d+10)%10，跨带行号 ≥10 在动画完成后
 * 无声回卷到等价首带行（与 loop 换页同一把戏，像素级同帧）。 */

static const char s_odo_strip[] =
    "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n0\n1\n2\n3\n4\n5\n6\n7\n8\n9";

static void odo_snap_cb(lv_anim_t *a)
{
    ui_m_odo_t *m = lv_obj_get_user_data(a->var);
    if (!m) return;
    int32_t ln = -lv_obj_get_style_translate_y(a->var, 0) / m->line_h;
    if (ln >= 10) {
        lv_obj_set_style_translate_y(a->var, -((ln - 10) * m->line_h), 0);
    }
}

void ui_m_odo(lv_obj_t *parent, int n_digits, ui_m_odo_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!parent) return;
    out->n = clamp_i32(n_digits, 1, UI_M_ODO_MAX);
    out->line_h = lv_font_get_line_height(UI_FONT_NUM);
    out->max_val = 9;
    for (int i = 1; i < out->n; i++) out->max_val = out->max_val * 10 + 9;

    int32_t dw = out->line_h * 3 / 5 + 6;     /* 数字列宽≈0.6em（lv_text_get_width 属私有 API，绕开） */

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, out->n * dw + (out->n - 1) * 2, out->line_h);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    out->root = root;

    for (int i = 0; i < out->n; i++) {
        lv_obj_t *col = lv_obj_create(root);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, dw, out->line_h);
        lv_obj_set_pos(col, i * (dw + 2), 0);
        lv_obj_set_style_clip_corner(col, true, 0);      /* 裁出单行窗口 */
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = ui_create_label(col, s_odo_strip, UI_FONT_NUM, UI_COLOR_TEXT);
        lv_obj_set_width(lbl, dw);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(lbl, 0, 0);
        lv_obj_set_user_data(lbl, out);
        out->lbl[i] = lbl;
        out->line[i] = 0;
    }
}

void ui_m_odo_set(ui_m_odo_t *m, int32_t v, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root)) return;
    if (v < 0) v = 0;
    if ((uint32_t)v > m->max_val) v = (int32_t)m->max_val;

    uint32_t t = (uint32_t)v;
    for (int i = m->n - 1; i >= 0; i--) {
        int d = (int)(t % 10);
        t /= 10;
        int steps = (d - (int)(m->line[i] % 10) + 10) % 10;
        if (!steps) continue;
        int32_t target = m->line[i] + steps;             /* 可跨带到 10..19 */
        if (animate) {
            anim_go(m->lbl[i], xlat_y_exec_cb,
                    lv_obj_get_style_translate_y(m->lbl[i], 0),
                    -(target * m->line_h), 0, PM_T_DUR_SLOW,
                    lv_anim_path_ease_out, odo_snap_cb);
        } else {
            if (target >= 10) target -= 10;
            lv_anim_delete(m->lbl[i], xlat_y_exec_cb);
            lv_obj_set_style_translate_y(m->lbl[i], -(target * m->line_h), 0);
        }
        if (target >= 10) target -= 10;                  /* 逻辑行回卷 */
        m->line[i] = target;
    }
    m->val = v;
}

int32_t ui_m_odo_get(const ui_m_odo_t *m)
{
    if (!m || !lv_obj_is_valid(m->root)) return 0;
    return m->val;
}

/* ──────────────────── 效果件 4: confetti 彩带（一次性） ────────────────────
 * ≤12 短命粒子：抛物线 exec（pos+opa 同一动画）+ 结束自删。
 * 700ms 为粒子特效有意例外时长（同 loop 原生滚动例外）。 */

typedef struct {
    int32_t x0, y0, vx, vy;
} conf_par_t;

static void conf_exec_cb(void *var, int32_t p)           /* p 0..1000 */
{
    conf_par_t *c = lv_obj_get_user_data(var);
    if (!c) return;
    int64_t P = p;
    int32_t x = c->x0 + (int32_t)(c->vx * P / 1000);
    int32_t y = c->y0 + (int32_t)(c->vy * P / 1000 + 380 * P * P / 2000000);
    lv_obj_set_pos(var, x, y);
    lv_opa_t opa = p < 650 ? LV_OPA_COVER : (lv_opa_t)(255 * (1000 - p) / 350);
    lv_obj_set_style_opa(var, opa, 0);
}

static void conf_done_cb(lv_anim_t *a)
{
    conf_par_t *c = lv_obj_get_user_data(a->var);
    if (c) lv_free(c);
    lv_obj_delete(a->var);
}

void ui_m_confetti(lv_obj_t *parent, int32_t x, int32_t y)
{
    if (!parent || !lv_obj_is_valid(parent)) return;
    lv_color_t pal[5];
    pie_palette(pal);

    for (int i = 0; i < 12; i++) {
        lv_obj_t *p = lv_obj_create(parent);
        lv_obj_remove_style_all(p);
        lv_obj_set_size(p, (i % 3) ? 5 : 8, (i % 3) ? 8 : 5);
        lv_obj_set_style_radius(p, 1, 0);
        lv_obj_set_style_bg_color(p, pal[i % 5], 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_pos(p, x, y);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(p, LV_OBJ_FLAG_IGNORE_LAYOUT);  /* 宿主可能是 flex 容器 */

        conf_par_t *cp = lv_malloc(sizeof(conf_par_t));
        if (!cp) { lv_obj_delete(p); continue; }
        cp->x0 = x;
        cp->y0 = y;
        cp->vx = (rand() % 360) - 180;
        cp->vy = -(120 + rand() % 280);
        lv_obj_set_user_data(p, cp);

        anim_go(p, conf_exec_cb, 0, 1000, (uint32_t)(i * 12), 700,
                lv_anim_path_linear, conf_done_cb);
    }
}
/* ──────────────────── 7) 辐射弹出菜单 ui_m_burst ────────────────────
 * 图标常态 HIDDEN 藏于 hub 之下（零常驻渲染）；展开 = 兜底层 + 图标/hub
 * 重新置顶 + 逐个沿极角 translate 错峰弹出（overshoot 保留——弹出家族）。
 * 说明列是图标容器的子对象（子可越界渲染，IGNORE_LAYOUT 绝对定位）。 */

static void burst_style_sel(ui_m_burst_t *m)
{
    for (int i = 0; i < m->n; i++) {
        lv_obj_t *ic = m->icons[i];
        if (!lv_obj_is_valid(ic)) continue;
        if (i == m->sel) {
            lv_obj_set_style_border_width(ic, 3, 0);
            lv_obj_set_style_border_color(ic, lv_color_white(), 0);
            lv_obj_set_style_border_opa(ic, LV_OPA_90, 0);
        } else {
            lv_obj_set_style_border_width(ic, 0, 0);
        }
    }
}

static void burst_hide_cb(lv_anim_t *a)
{
    lv_obj_add_flag(a->var, LV_OBJ_FLAG_HIDDEN);
}

static void burst_backdrop_cb(lv_event_t *e)
{
    ui_m_burst_close((ui_m_burst_t *)lv_event_get_user_data(e), true);
}

static void burst_hub_cb(lv_event_t *e)
{
    ui_m_burst_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->root)) return;
    if (m->is_open) ui_m_burst_close(m, true);
    else            ui_m_burst_open(m, true);
}

static void burst_icon_cb(lv_event_t *e)
{
    lv_obj_t *ic = lv_event_get_target(e);
    ui_m_burst_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(ic)) return;
    int i = (int)(intptr_t)lv_obj_get_user_data(ic);
    m->sel = i;
    burst_style_sel(m);
    sel_notify(m->root, i, m->cb, m->cb_ud, true);
    ui_m_burst_close(m, true);
}

void ui_m_burst(lv_obj_t *parent, lv_color_t accent, int32_t size,
                const ui_m_item_t *items, int n, ui_m_burst_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!parent) return;
    size = clamp_i32(size, 200, 420);
    if (!items) n = 0;
    n = clamp_i32(n, 0, UI_M_BURST_MAX);

    lv_color_t pal[5];
    pie_palette(pal);

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, size, size);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    out->root = root;
    out->n = n;
    out->items = items;
    lv_obj_set_user_data(root, out);

    int32_t cx = size / 2, cy = size / 2;

    /* 图标先建（展开时 move_to_index 置顶，常态藏 hub 后面） */
    for (int i = 0; i < n; i++) {
        const ui_m_item_t *it = &items[i];
        lv_color_t col = pal[i % 5];
        lv_obj_t *ic = lv_obj_create(root);
        lv_obj_remove_style_all(ic);
        lv_obj_set_size(ic, 48, 48);
        lv_obj_set_pos(ic, cx - 24, cy - 24);
        lv_obj_set_style_radius(ic, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ic, col, 0);
        lv_obj_set_style_bg_opa(ic, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(ic, 0, 0);           /* 常态全透明 + 藏 */
        lv_obj_add_flag(ic, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ic, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(ic, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(ic, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(ic, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_user_data(ic, (void *)(intptr_t)i);
        lv_obj_add_event_cb(ic, burst_icon_cb, LV_EVENT_CLICKED, out);
        if (it->symbol)
            ui_create_label(ic, it->symbol, UI_FONT_SYMBOL, fg_on(col));
        if (it->zh) {
            /* 说明列：子对象越界渲染（图标圆下方），不参与 flex */
            lv_obj_t *cap = ui_create_label_bind(ic, it->zh, UI_FONT_CAPTION, UI_COLOR_TEXT);
            lv_obj_add_flag(cap, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_pos(cap, 24 - 44, 52);
            lv_obj_set_width(cap, 88);
            lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
        }
        out->icons[i] = ic;
    }

    /* 中心键最后建 = 常态在最顶 */
    lv_obj_t *hub = lv_obj_create(root);
    lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, 72, 72);
    lv_obj_set_pos(hub, cx - 36, cy - 36);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, accent, 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_add_flag(hub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hub, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hub, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    out->hub = hub;
    out->hub_sym = ui_create_label(hub, LV_SYMBOL_PLUS, UI_FONT_SYMBOL, fg_on(accent));
    lv_obj_add_event_cb(hub, burst_hub_cb, LV_EVENT_CLICKED, out);

    burst_style_sel(out);
}

void ui_m_burst_open(ui_m_burst_t *m, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root) || !m->n || m->is_open) return;
    m->is_open = 1;

    /* 兜底层：盖满 root，点空白收拢（先建 = 图标之下） */
    lv_obj_t *bk = lv_obj_create(m->root);
    lv_obj_remove_style_all(bk);
    lv_obj_set_size(bk, lv_obj_get_width(m->root), lv_obj_get_height(m->root));
    lv_obj_set_pos(bk, 0, 0);
    lv_obj_add_flag(bk, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(bk, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(bk, burst_backdrop_cb, LV_EVENT_CLICKED, m);
    m->backdrop = bk;

    /* 图标 + hub 提到兜底层之上 */
    for (int i = 0; i < m->n; i++)
        lv_obj_move_to_index(m->icons[i], lv_obj_get_child_count(m->root) - 1);
    lv_obj_move_to_index(m->hub, lv_obj_get_child_count(m->root) - 1);
    lv_label_set_text(m->hub_sym, LV_SYMBOL_CLOSE);

    int32_t ring = lv_obj_get_width(m->root) / 2 - 44;
    for (int i = 0; i < m->n; i++) {
        lv_obj_t *ic = m->icons[i];
        lv_obj_clear_flag(ic, LV_OBJ_FLAG_HIDDEN);
        if (!animate) {
            lv_point_t pt = polar_top_cw(i * 360 / m->n, ring);
            lv_obj_set_style_translate_x(ic, pt.x, 0);
            lv_obj_set_style_translate_y(ic, pt.y, 0);
            lv_obj_set_style_opa(ic, LV_OPA_COVER, 0);
            continue;
        }
        lv_point_t pt = polar_top_cw(i * 360 / m->n, ring);
        uint32_t d = (uint32_t)(i * 30);
        anim_go(ic, xlat_x_exec_cb, lv_obj_get_style_translate_x(ic, 0), pt.x,
                d, PM_T_DUR_SLOW, lv_anim_path_overshoot, NULL);
        anim_go(ic, xlat_y_exec_cb, lv_obj_get_style_translate_y(ic, 0), pt.y,
                d, PM_T_DUR_SLOW, lv_anim_path_overshoot, NULL);
        anim_go(ic, opa_exec_cb, lv_obj_get_style_opa(ic, 0), LV_OPA_COVER,
                d, PM_T_DUR_FAST, lv_anim_path_ease_out, NULL);
    }
}

void ui_m_burst_close(ui_m_burst_t *m, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root) || !m->is_open) return;
    m->is_open = 0;

    if (m->backdrop && lv_obj_is_valid(m->backdrop)) {
        lv_obj_delete(m->backdrop);               /* 头注释契约：收拢即删 */
        m->backdrop = NULL;
    }
    lv_label_set_text(m->hub_sym, LV_SYMBOL_PLUS);

    for (int i = 0; i < m->n; i++) {
        lv_obj_t *ic = m->icons[i];
        uint32_t d = animate ? (uint32_t)((m->n - 1 - i) * 20) : 0;  /* 中心波次收 */
        if (!animate) {
            lv_anim_delete(ic, NULL);
            lv_obj_set_style_translate_x(ic, 0, 0);
            lv_obj_set_style_translate_y(ic, 0, 0);
            lv_obj_set_style_opa(ic, 0, 0);
            lv_obj_add_flag(ic, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        anim_go(ic, xlat_x_exec_cb, lv_obj_get_style_translate_x(ic, 0), 0,
                d, PM_T_DUR_FAST, lv_anim_path_ease_in, NULL);
        anim_go(ic, xlat_y_exec_cb, lv_obj_get_style_translate_y(ic, 0), 0,
                d, PM_T_DUR_FAST, lv_anim_path_ease_in, burst_hide_cb);
        anim_go(ic, opa_exec_cb, lv_obj_get_style_opa(ic, 0), 0,
                d, PM_T_DUR_FAST, lv_anim_path_ease_in, NULL);
    }
}

bool ui_m_burst_is_open(const ui_m_burst_t *m)
{
    return m && m->is_open;
}

bool ui_m_burst_set(ui_m_burst_t *m, int sel, bool animate)
{
    (void)animate;                                /* 高亮无动画语义 */
    if (!m || !lv_obj_is_valid(m->root) || !m->n) return false;
    if (sel < 0 || sel >= m->n || sel == m->sel) return false;
    m->sel = sel;
    burst_style_sel(m);
    sel_notify(m->root, sel, m->cb, m->cb_ud, false);
    return true;
}

int ui_m_burst_get(const ui_m_burst_t *m)
{
    return m ? m->sel : 0;
}

void ui_m_burst_on_sel(ui_m_burst_t *m, ui_m_sel_cb_t cb, void *user_data)
{
    if (!m) return;
    m->cb = cb;
    m->cb_ud = user_data;
}

/* ──────────────────── 8) 老虎机滚轮 ui_m_slot ────────────────────
 * 滚筒 = 3n 克隆 × pitch 64 + snap CENTER；亮带是外层 root 的装饰子，
 * 不随滚筒滚。滚动派生：scale 256-64u / opa 255-170u（u=min(|d|,96)/96）。
 * 自旋 = 手搓 lv_anim（var=scr，删对象即撤销动画），虚拟行程单调递增、
 * exec 里按周期 n*64 回卷进中段（克隆等价 → 像素级同帧）。
 * 三段物理：加速(220) + 长尾减速(1500，粒子级有意例外时长) + 过冲回落(140)。
 * 自旋期间 spinning=1：SCROLL 只派生不换页，SCROLL_END 全静默。 */

#define SLOT_PITCH 64
#define SLOT_SPIN_OVER 22                          /* 定格前过冲 px */
#define SLOT_TAIL_MS 1500                          /* 长尾减速（有意例外时长） */

static void slot_derive(ui_m_slot_t *m, int32_t y)
{
    int32_t mid = y + m->vh / 2;
    for (int i = 0; i < m->n * 3; i++) {
        lv_obj_t *c = m->cells[i];
        int32_t d = (m->pad + i * SLOT_PITCH + SLOT_PITCH / 2) - mid;
        int32_t ad = d < 0 ? -d : d;
        int32_t u = min_i32(ad, 96) * 256 / 96;
        if (s_scale_fx) scale_exec_cb(c, 256 - (u * 64 >> 8));
        lv_obj_set_style_opa(c, (lv_opa_t)(255 - (u * 170 >> 8)), 0);
    }
}

static void slot_scroll_cb(lv_event_t *e)
{
    ui_m_slot_t *m = lv_event_get_user_data(e);
    if (!m || !lv_obj_is_valid(m->scr)) return;
    int32_t y = lv_obj_get_scroll_y(m->scr);
    if (y - m->last_sy > -2 && y - m->last_sy < 2) return;   /* 2px 早退 */
    m->last_sy = y;
    slot_derive(m, y);
}

static void slot_scroll_end_cb(lv_event_t *e)
{
    ui_m_slot_t *m = lv_event_get_user_data(e);
    if (!m || !m->n || !lv_obj_is_valid(m->scr) || m->spinning) return;
    int32_t y = lv_obj_get_scroll_y(m->scr);
    int j = (y + SLOT_PITCH / 2) / SLOT_PITCH;
    if (j < 0) j = 0;
    int sel = j % m->n;
    if (sel != m->sel) {                          /* 先改后滚：程序化 SCROLL_END 守卫 */
        m->sel = sel;
        sel_notify(m->root, sel, m->cb, m->cb_ud, true);
    }
    if (j <= m->n) {                              /* 中段归位（无声换页，同 loop） */
        y += (int32_t)m->n * SLOT_PITCH;
        lv_obj_scroll_to_y(m->scr, y, LV_ANIM_OFF);
        m->last_sy = y;
    } else if (j >= 2 * m->n) {
        y -= (int32_t)m->n * SLOT_PITCH;
        lv_obj_scroll_to_y(m->scr, y, LV_ANIM_OFF);
        m->last_sy = y;
    }
}

/** 虚拟行程 y → 中段物理 scroll_y（v mod 周期 + 中段基；克隆等价，像素同帧） */
static int32_t slot_wrap(ui_m_slot_t *m, int32_t v)
{
    int32_t period = (int32_t)m->n * SLOT_PITCH;
    return period + v % period;
}

static void slot_spin_exec_cb(void *var, int32_t v)   /* v = 虚拟行程 y（单调） */
{
    ui_m_slot_t *m = lv_obj_get_user_data(var);
    if (!m || !lv_obj_is_valid(m->scr)) return;
    int32_t phys = slot_wrap(m, v);
    lv_obj_scroll_to_y(m->scr, phys, LV_ANIM_OFF);
    slot_derive(m, phys);
    int32_t cell = phys / SLOT_PITCH;
    uint32_t now = lv_tick_get();
    if (cell != m->tick_mark && now - m->tick_ms > 70) {
        m->tick_mark = cell;
        m->tick_ms = now;
        voice_touch_click();
    }
}

static void slot_spin_settle_cb(lv_anim_t *a)     /* 第三段完成：定格 + 通知 */
{
    ui_m_slot_t *m = lv_obj_get_user_data(a->var);
    if (!m || !lv_obj_is_valid(m->scr)) return;
    int32_t target = slot_wrap(m, m->spin_end);   /* 虚拟落点 → 物理 */
    lv_obj_scroll_to_y(m->scr, target, LV_ANIM_OFF);
    slot_derive(m, target);
    m->spinning = 0;
    m->last_sy = target;
    lv_obj_add_flag(m->scr, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    sel_notify(m->root, m->sel, m->cb, m->cb_ud, false);
}

static void slot_spin_tail_cb(lv_anim_t *a)       /* 第二段完成 → 起第三段 */
{
    ui_m_slot_t *m = lv_obj_get_user_data(a->var);
    if (!m || !lv_obj_is_valid(m->scr)) return;
    anim_go(m->scr, slot_spin_exec_cb, m->spin_end + SLOT_SPIN_OVER, m->spin_end,
            0, PM_T_DUR_FAST, lv_anim_path_ease_out, slot_spin_settle_cb);
}

static void slot_spin_acc_cb(lv_anim_t *a)        /* 第一段完成 → 起第二段 */
{
    ui_m_slot_t *m = lv_obj_get_user_data(a->var);
    if (!m || !lv_obj_is_valid(m->scr)) return;
    anim_go(m->scr, slot_spin_exec_cb, m->spin_y1, m->spin_end + SLOT_SPIN_OVER,
            0, SLOT_TAIL_MS, lv_anim_path_ease_out, slot_spin_tail_cb);
}

bool ui_m_slot_set(ui_m_slot_t *m, int sel, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root) || !m->n) return false;
    if (sel < 0 || sel >= m->n) return false;
    if (sel == m->sel && !animate) return false;  /* 同值即时 no-op；
                                                     同值自旋 = 老虎机再转一次 */
    m->sel = sel;
    int32_t period = (int32_t)m->n * SLOT_PITCH;
    int32_t target = ((int32_t)m->n + sel) * SLOT_PITCH;   /* 中段拷贝 */

    /* 打断在飞的自旋链（同 var+exec 的旧动画被新动画顶替，链即断） */
    if (m->spinning && !animate) {
        m->spinning = 0;
        lv_obj_add_flag(m->scr, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_anim_delete(m->scr, slot_spin_exec_cb);

    if (!animate) {
        m->spinning = 1;                          /* 压掉程序化 SCROLL_END */
        lv_obj_scroll_to_y(m->scr, target, LV_ANIM_OFF);
        slot_derive(m, target);
        m->last_sy = target;
        m->spinning = 0;
        sel_notify(m->root, sel, m->cb, m->cb_ud, false);
        return true;
    }

    /* 归一当前位进中段，规划 ≥2 整圈的单调虚拟行程 */
    int32_t y0 = lv_obj_get_scroll_y(m->scr);
    while (y0 >= 2 * period) y0 -= period;
    while (y0 < period) y0 += period;
    int32_t total = target - y0;
    while (total < 2 * period) total += period;

    m->spinning = 1;
    m->tick_mark = -1;
    m->tick_ms = 0;
    m->spin_y0 = y0;                              /* wrap 相位锚（日志/调试用） */
    m->spin_y1 = y0 + total * 3 / 20;             /* 加速段终点（虚拟） */
    m->spin_end = y0 + total;                     /* 虚拟落点（≡ target mod 周期） */
    lv_obj_clear_flag(m->scr, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    anim_go(m->scr, slot_spin_exec_cb, y0, m->spin_y1,
            0, PM_T_DUR_NORMAL, lv_anim_path_ease_in, slot_spin_acc_cb);
    return true;
}

int ui_m_slot_get(const ui_m_slot_t *m)
{
    return m ? m->sel : 0;
}

void ui_m_slot_on_sel(ui_m_slot_t *m, ui_m_sel_cb_t cb, void *user_data)
{
    if (!m) return;
    m->cb = cb;
    m->cb_ud = user_data;
}

void ui_m_slot(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
               int n, int32_t viewport_h, ui_m_slot_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!parent) return;
    viewport_h = clamp_i32(viewport_h, 140, 400);
    if (!items) n = 0;
    else n = clamp_i32(n, 2, UI_M_SLOT_MAX);

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 232, viewport_h);
    lv_obj_set_style_radius(root, 16, 0);
    lv_obj_set_style_bg_color(root, ui_w_tint(accent, 12), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 1, 0);
    lv_obj_set_style_border_color(root, UI_COLOR_BORDER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    out->root = root;
    out->n = n;
    out->vh = viewport_h;
    out->pad = (viewport_h - SLOT_PITCH) / 2;
    out->items = items;
    lv_obj_set_user_data(root, out);

    /* 滚筒：3n 克隆文字行（无容器，label 直挂省 3n 个对象） */
    lv_obj_t *scr = lv_obj_create(root);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, 232, viewport_h);
    lv_obj_set_pos(scr, 0, 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(scr, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_user_data(scr, out);
    lv_obj_add_event_cb(scr, slot_scroll_cb, LV_EVENT_SCROLL, out);
    lv_obj_add_event_cb(scr, slot_scroll_end_cb, LV_EVENT_SCROLL_END, out);
    out->scr = scr;

    int32_t lh = lv_font_get_line_height(UI_FONT_TITLE);
    for (int i = 0; i < n * 3; i++) {
        lv_obj_t *lbl = ui_create_label_bind(scr, items[i % n].zh,
                                             UI_FONT_TITLE, UI_COLOR_TEXT);
        lv_obj_set_pos(lbl, 0, out->pad + i * SLOT_PITCH + (SLOT_PITCH - lh) / 2);
        lv_obj_set_width(lbl, 232);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        out->cells[i] = lbl;
    }

    /* 选中亮带：root 装饰子（后建 = 盖滚筒之上），不随滚筒滚 */
    lv_obj_t *band = lv_obj_create(root);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, 220, SLOT_PITCH);
    lv_obj_set_pos(band, 6, viewport_h / 2 - SLOT_PITCH / 2);
    lv_obj_set_style_radius(band, 12, 0);
    lv_obj_set_style_bg_color(band, ui_w_tint(accent, 40), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(band, 2, 0);
    lv_obj_set_style_border_color(band, accent, 0);
    lv_obj_set_style_border_opa(band, LV_OPA_60, 0);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    out->band = band;

    if (!n) return;
    lv_obj_update_layout(root);                   /* 初居中：中段第 0 格 */
    out->spinning = 1;                            /* 压掉程序化 SCROLL_END */
    lv_obj_scroll_to_y(scr, (int32_t)n * SLOT_PITCH, LV_ANIM_OFF);
    out->spinning = 0;
    out->last_sy = (int32_t)n * SLOT_PITCH;
    slot_derive(out, (int32_t)n * SLOT_PITCH);
}

/* ──────────────────── 9) 卡牌飞出 ui_m_deck ────────────────────
 * 对象恒 3 张（创建序 2→1→0 保 z 序），推进只换文案+姿态——零创建销毁。
 * 换文案 = lv_label_set_text(tr()) + tr_bind() 覆盖键（语言切换自动跟）。
 * 顶层跟手 translate_x（±280 钳位）；|dx|>80 松手 = 飞出淡出 + 队列轮转 +
 * 底层错峰上浮；否则 overshoot 弹回（dock 回弹同族）。轻点 = 选中飞出。 */

#define DECK_CARD_W 356
#define DECK_CARD_H 188
#define DECK_FLY_X  520
#define DECK_THRES  80

static void deck_label_card(ui_m_deck_t *m, int L)
{
    if (L >= m->n && m->n < 3) {                  /* n<3 时底层卡循环借位 */
        L = L % m->n;
    }
    const ui_m_item_t *it = &m->items[m->order[L]];
    lv_label_set_text(m->syms[L], it->symbol ? it->symbol : "");
    if (it->zh) {
        lv_label_set_text(m->titles[L], tr(it->zh));
        tr_bind(m->titles[L], it->zh);
    } else {
        lv_label_set_text(m->titles[L], "");
    }
    if (it->zh_sub) {
        lv_label_set_text(m->subs[L], tr(it->zh_sub));
        tr_bind(m->subs[L], it->zh_sub);
    } else {
        lv_label_set_text(m->subs[L], "");
    }
}

static void deck_apply(ui_m_deck_t *m, bool animate)
{
    static const int32_t ty[3] = { 0, 14, 28 };
    static const int32_t sc[3] = { 256, 232, 208 };
    static const int32_t op[3] = { LV_OPA_COVER, 205, 130 };

    for (int L = 0; L < 3; L++) {
        lv_obj_t *c = m->layers[L];
        if (!animate) {
            lv_obj_set_style_translate_y(c, ty[L], 0);
            if (s_scale_fx) scale_exec_cb(c, sc[L]);
            lv_obj_set_style_opa(c, (lv_opa_t)op[L], 0);
            continue;
        }
        uint32_t d = (uint32_t)(L * 40);          /* 底层错峰上浮 */
        anim_go(c, xlat_y_exec_cb, lv_obj_get_style_translate_y(c, 0), ty[L],
                d, PM_T_DUR_FAST, lv_anim_path_ease_out, NULL);
        if (s_scale_fx)
            anim_go(c, scale_exec_cb, lv_obj_get_style_transform_scale_x(c, 0), sc[L],
                    d, PM_T_DUR_FAST, lv_anim_path_ease_out, NULL);
        anim_go(c, opa_exec_cb, lv_obj_get_style_opa(c, 0), op[L],
                d, PM_T_DUR_FAST, lv_anim_path_ease_out, NULL);
    }
}

static void deck_advance_cb(lv_anim_t *a)
{
    ui_m_deck_t *m = lv_obj_get_user_data(a->var);
    if (!m || !lv_obj_is_valid(m->root)) return;
    m->busy = 0;
    lv_obj_set_style_translate_x(a->var, 0, 0);   /* 归位：底层淡入接力 */
    lv_obj_set_style_opa(a->var, 0, 0);
    int head = m->order[0];                       /* 队列轮转：头→尾 */
    for (int i = 0; i < m->n - 1; i++) m->order[i] = m->order[i + 1];
    m->order[m->n - 1] = head;
    for (int L = 0; L < 3; L++) deck_label_card(m, L);
    deck_apply(m, true);
    sel_notify(m->root, m->sel, m->cb, m->cb_ud, true);
}

static void deck_fly(ui_m_deck_t *m, int dir)
{
    lv_obj_t *c = m->layers[0];
    anim_go(c, xlat_x_exec_cb, lv_obj_get_style_translate_x(c, 0), dir * DECK_FLY_X,
            0, PM_T_DUR_FAST, lv_anim_path_ease_in, deck_advance_cb);
    anim_go(c, opa_exec_cb, lv_obj_get_style_opa(c, 0), 0,
            0, PM_T_DUR_FAST, lv_anim_path_ease_in, NULL);
}

static void deck_press_cb(lv_event_t *e)
{
    ui_m_deck_t *m = lv_event_get_user_data(e);
    if (!m || m->busy) return;
    indev_point_local(lv_event_get_target(e), &m->press_pt);
}

static void deck_pressing_cb(lv_event_t *e)
{
    ui_m_deck_t *m = lv_event_get_user_data(e);
    if (!m || m->busy) return;
    lv_obj_t *c = lv_event_get_target(e);
    lv_point_t lp;
    indev_point_local(c, &lp);
    lv_obj_set_style_translate_x(c, clamp_i32(lp.x - m->press_pt.x, -280, 280), 0);
}

static void deck_release_cb(lv_event_t *e)
{
    ui_m_deck_t *m = lv_event_get_user_data(e);
    if (!m || m->busy) return;
    lv_obj_t *c = lv_event_get_target(e);
    int32_t dx = lv_obj_get_style_translate_x(c, 0);
    if (dx > DECK_THRES || dx < -DECK_THRES) {
        m->sel = m->order[0];
        m->busy = 1;
        deck_fly(m, dx > 0 ? 1 : -1);
    } else {
        anim_go(c, xlat_x_exec_cb, dx, 0,
                0, PM_T_DUR_NORMAL, lv_anim_path_overshoot, NULL);   /* 弹回 */
    }
}

static void deck_click_cb(lv_event_t *e)
{
    ui_m_deck_t *m = lv_event_get_user_data(e);
    if (!m || m->busy) return;
    lv_obj_t *c = lv_event_get_target(e);
    int32_t dx = lv_obj_get_style_translate_x(c, 0);
    if (dx > 12 || dx < -12) return;              /* 拖拽过了：RELEASED 已处理 */
    m->sel = m->order[0];
    m->busy = 1;
    deck_fly(m, 1);
}

void ui_m_deck(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
               int n, ui_m_deck_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!parent) return;
    if (!items) n = 0;
    else n = clamp_i32(n, 1, UI_M_DECK_MAX);

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_width(root, lv_pct(100));
    lv_obj_set_height(root, DECK_CARD_H + 48);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    out->root = root;
    out->n = n;
    out->items = items;
    out->sel = 0;
    lv_obj_set_user_data(root, out);
    for (int i = 0; i < n; i++) out->order[i] = i;
    if (!n) return;

    lv_obj_update_layout(root);
    int32_t cx = (lv_obj_get_width(root) - DECK_CARD_W) / 2;

    for (int L = 2; L >= 0; L--) {                /* 创建序 2→1→0 保 z 序 */
        lv_obj_t *c = lv_obj_create(root);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, DECK_CARD_W, DECK_CARD_H);
        lv_obj_set_pos(c, cx, 16);
        lv_obj_set_style_radius(c, 20, 0);
        lv_obj_set_style_bg_color(c, ui_w_tint(accent, 16), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_border_color(c, UI_COLOR_BORDER, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(c, 8, 0);
        lv_obj_set_user_data(c, out);
        lv_obj_add_event_cb(c, deck_press_cb, LV_EVENT_PRESSED, out);
        lv_obj_add_event_cb(c, deck_pressing_cb, LV_EVENT_PRESSING, out);
        lv_obj_add_event_cb(c, deck_release_cb, LV_EVENT_RELEASED, out);
        lv_obj_add_event_cb(c, deck_release_cb, LV_EVENT_PRESS_LOST, out);
        lv_obj_add_event_cb(c, deck_click_cb, LV_EVENT_CLICKED, out);
        out->layers[L] = c;
        out->syms[L] = ui_create_label(c, "", UI_FONT_SYMBOL, accent);
        out->titles[L] = ui_create_label_bind(c, "", UI_FONT_TITLE, UI_COLOR_TEXT);
        out->subs[L] = ui_create_label_bind(c, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    }

    for (int L = 0; L < 3; L++) deck_label_card(out, L);
    deck_apply(out, false);
}

bool ui_m_deck_set(ui_m_deck_t *m, int sel, bool animate)
{
    if (!m || !lv_obj_is_valid(m->root) || !m->n) return false;
    if (sel < 0 || sel >= m->n || sel == m->sel) return false;
    int k = 0;
    while (k < m->n && m->order[k] != sel) k++;
    if (k >= m->n) return false;
    if (m->busy) {                                /* 打断在飞的推进 */
        lv_anim_delete(m->layers[0], NULL);
        m->busy = 0;
        lv_obj_set_style_translate_x(m->layers[0], 0, 0);
    }
    int tmp[UI_M_DECK_MAX];
    for (int i = 0; i < m->n; i++) tmp[i] = m->order[(i + k) % m->n];
    memcpy(m->order, tmp, sizeof(int) * (size_t)m->n);
    m->sel = sel;
    for (int L = 0; L < 3; L++) deck_label_card(m, L);
    deck_apply(m, animate);
    sel_notify(m->root, sel, m->cb, m->cb_ud, false);
    return true;
}

int ui_m_deck_get(const ui_m_deck_t *m)
{
    return m ? m->sel : 0;
}

void ui_m_deck_on_sel(ui_m_deck_t *m, ui_m_sel_cb_t cb, void *user_data)
{
    if (!m) return;
    m->cb = cb;
    m->cb_ud = user_data;
}
