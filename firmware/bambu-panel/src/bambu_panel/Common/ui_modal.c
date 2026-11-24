/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - Modal 统一层实现
 *
 * 结构（lv_layer_top 之下，z 序：modal_root < keyboard < toast）：
 *   modal_root（惰性创建，move_to_index(0) 压到键盘下）
 *   └── mask_n（全屏遮罩，CLICKABLE 吃输入；层叠时变暗自然加深）
 *       └── card（居中卡片，业务在 build 里自绘）
 *
 * 每层一个堆 ctx（挂 card->user_data）：
 *   - 进/出动画 var 都指 ctx → lv_anim_delete(ctx,NULL) 可掐掉在途动画
 *   - closing 标志防二次关（close_top/业务/出动画完成三路交汇只删一次）
 *
 * 动画（DESIGN §6：SCALE_FADE 只用于小卡片）：
 *   进：card scale 243→256，280ms overshoot；mask bg_opa 0→dim（对象 opa 始终 COVER）
 *   出：card scale 256→232 + bg_opa 淡出，160ms ease_in，完成后删整层、free ctx
 * pivot 未设置时默认取对象中心（lv_obj_draw.c:236），无需手动摆轴心。
 */
#include "ui_modal.h"

#include <stdlib.h>

#include "i18n.h"
#include "page_transition.h"
#include "ui_common.h"

typedef struct {
    ui_modal_builder_t b;      /* 构建描述副本（含 on_close/user_data） */
    lv_obj_t *card;
    lv_obj_t *mask;
    lv_opa_t dim;
    bool closing;
} modal_ctx_t;

static lv_obj_t *s_modal_root = NULL;   /* 惰性：首个 modal 打开时创建 */
static lv_obj_t *s_loading = NULL;      /* Loading 单例 */

/* ──────────────────── 动画 ──────────────────── */

/* 单动画驱动 mask(bg_opa) + card(scale)；对象 opa 保持 COVER，避免 PPA 层透底 */
static void modal_step_cb(void *var, int32_t p)
{
    modal_ctx_t *c = (modal_ctx_t *)var;
    if (!lv_obj_is_valid(c->card)) {
        return;
    }
    if (c->closing) {
        int32_t scale = 256 + (232 - 256) * p / 1000;
        lv_obj_set_style_transform_scale_x(c->card, scale, 0);
        lv_obj_set_style_transform_scale_y(c->card, scale, 0);
        lv_obj_set_style_opa(c->card, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(c->card, (lv_opa_t)(LV_OPA_COVER * (1000 - p) / 1000), 0);
        if (lv_obj_is_valid(c->mask)) {
            lv_obj_set_style_opa(c->mask, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_opa(c->mask, (lv_opa_t)(c->dim * (1000 - p) / 1000), 0);
        }
    } else {
        int32_t scale = 243 + (256 - 243) * p / 1000;
        lv_obj_set_style_transform_scale_x(c->card, scale, 0);
        lv_obj_set_style_transform_scale_y(c->card, scale, 0);
        lv_obj_set_style_opa(c->card, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(c->card, LV_OPA_COVER, 0);
        if (lv_obj_is_valid(c->mask)) {
            lv_obj_set_style_opa(c->mask, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_opa(c->mask, (lv_opa_t)(c->dim * p / 1000), 0);
        }
    }
}

static void modal_in_done_cb(lv_anim_t *a)
{
    modal_ctx_t *c = (modal_ctx_t *)a->var;
    if (!lv_obj_is_valid(c->card)) {
        return;
    }
    lv_obj_set_style_transform_scale_x(c->card, 256, 0);
    lv_obj_set_style_transform_scale_y(c->card, 256, 0);
    lv_obj_set_style_opa(c->card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(c->card, LV_OPA_COVER, 0);
    if (lv_obj_is_valid(c->mask)) {
        lv_obj_set_style_opa(c->mask, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(c->mask, c->dim, 0);
    }
}

static void modal_out_done_cb(lv_anim_t *a)
{
    modal_ctx_t *c = (modal_ctx_t *)a->var;
    lv_obj_t *mask = c->mask;
    lv_free(c);
    if (lv_obj_is_valid(mask)) {
        lv_obj_delete(mask);   /* 连带 card；析构自动清残挂动画 */
    }
    if (s_modal_root && lv_obj_get_child_count(s_modal_root) == 0) {
        lv_obj_delete(s_modal_root);   /* 无 modal 时整根拆除，不占图层 */
        s_modal_root = NULL;
    }
}

/* ──────────────────── Modal 根 ──────────────────── */

static lv_obj_t *modal_root_get(void)
{
    if (s_modal_root) {
        return s_modal_root;
    }
    lv_obj_t *root = lv_obj_create(lv_layer_top());
    s_modal_root = root;
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    /* 压到图层最底：键盘/Toast 建在其上（键盘先建的场合 move 后自动顶上来） */
    lv_obj_move_to_index(root, 0);
    return root;
}

/* ──────────────────── 公开 API ──────────────────── */

static void on_outside_click(lv_event_t *e)
{
    modal_ctx_t *c = (modal_ctx_t *)lv_event_get_user_data(e);
    /* 事件不冒泡（card 未设 EVENT_BUBBLE），到这里的都是点到 mask 空白区 */
    (void)e;
    ui_modal_close(c->card);
}

lv_obj_t *ui_modal_open(const ui_modal_builder_t *b)
{
    if (!b || !b->build) {
        return NULL;
    }
    lv_obj_t *root = modal_root_get();

    lv_obj_t *mask = lv_obj_create(root);
    lv_obj_set_size(mask, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_opa(mask, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_TRANSP, 0);   /* 进场动画淡入 */
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_set_style_pad_all(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(mask, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_CLICKABLE);      /* 吃输入 */

    lv_obj_t *card = lv_obj_create(mask);
    lv_obj_set_size(card, b->width ? b->width : UI_SCREEN_W - 56, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, UI_COLOR_CARD_HI, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_color(card, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_max_height(card, UI_SCREEN_H * 7 / 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);     /* 内容超高可滚 */
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    modal_ctx_t *c = lv_malloc_zeroed(sizeof(modal_ctx_t));
    c->b = *b;
    c->card = card;
    c->mask = mask;
    c->dim = b->dim ? b->dim : LV_OPA_60;
    lv_obj_set_user_data(card, c);

    b->build(card, b->user_data);

    if (b->close_on_outside) {
        lv_obj_add_event_cb(mask, on_outside_click, LV_EVENT_CLICKED, c);
    }

    /* 进场动画（overshoot 从 243 弹到 256）；卡片始终不透明，只淡入遮罩 */
    lv_obj_set_style_transform_scale_x(card, 243, 0);
    lv_obj_set_style_transform_scale_y(card, 243, 0);
    lv_obj_set_style_opa(card, LV_OPA_COVER, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, modal_step_cb);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, PM_T_DUR_SLOW);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_set_completed_cb(&a, modal_in_done_cb);
    lv_anim_start(&a);

    return card;
}

void ui_modal_close(lv_obj_t *card)
{
    if (!card || !lv_obj_is_valid(card)) {
        return;
    }
    modal_ctx_t *c = (modal_ctx_t *)lv_obj_get_user_data(card);
    if (!c || c->closing || c->card != card) {
        return;   /* 非 modal 卡片 / 已在关闭中 */
    }
    if (!s_modal_root || !lv_obj_is_valid(c->mask) ||
        lv_obj_get_parent(c->mask) != s_modal_root) {
        return;
    }

    c->closing = true;
    lv_anim_delete(c, NULL);   /* 掐掉在途进场动画（var=c） */

    /* 关闭回调（card 仍活着，业务可读内容/解绑键盘） */
    if (c->b.on_close) {
        c->b.on_close(card, c->b.user_data);
    }

    /* 出动画期间 mask 不再响应点击（防重复关） */
    lv_obj_clear_flag(c->mask, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, modal_step_cb);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, PM_T_DUR_FAST);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, modal_out_done_cb);
    lv_anim_start(&a);
}

void ui_modal_close_top(void)
{
    if (!s_modal_root) {
        return;
    }
    uint32_t n = lv_obj_get_child_count(s_modal_root);
    if (n == 0) {
        return;
    }
    lv_obj_t *mask = lv_obj_get_child(s_modal_root, n - 1);
    if (lv_obj_get_child_count(mask) == 0) {
        return;
    }
    ui_modal_close(lv_obj_get_child(mask, 0));
}

bool ui_modal_is_open(void)
{
    return s_modal_root && lv_obj_get_child_count(s_modal_root) > 0;
}

/* ──────────────────── Loading ──────────────────── */

/* 三圆点脉动（零小部件依赖，固件 Kconfig 不必开 LV_USE_SPINNER） */
static void dot_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void loading_build(void)
{
    lv_obj_t *wrap = lv_obj_create(lv_layer_top());
    s_loading = wrap;
    lv_obj_set_size(wrap, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wrap, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_60, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_radius(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(wrap, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(wrap, LV_OBJ_FLAG_CLICKABLE);   /* blocking 默认吃输入 */
    lv_obj_move_to_index(wrap, -1);                 /* modal 之上、Toast 下 */

    lv_obj_t *box = lv_obj_create(wrap);
    lv_obj_set_size(box, 132, 116);
    lv_obj_set_style_bg_color(box, UI_COLOR_CARD_HI, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_shadow_width(box, 0, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 12, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *dots = lv_obj_create(box);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dots, 0, 0);
    lv_obj_set_style_pad_all(dots, 0, 0);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dots, 8, 0);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *dot = lv_obj_create(dots);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_40, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        /* 相位错开 200ms 的脉动：40% ↔ 100% */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, dot);
        lv_anim_set_exec_cb(&a, dot_opa_cb);
        lv_anim_set_values(&a, LV_OPA_40, LV_OPA_COVER);
        lv_anim_set_duration(&a, 380);
        lv_anim_set_delay(&a, i * 200);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_repeat_delay(&a, 320);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }

    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, tr("加载中..."));
    lv_obj_set_style_text_font(label, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
}

void ui_loading_show(bool blocking)
{
    if (s_loading) {
        return;   /* 单例：已在显示 */
    }
    loading_build();
    if (!blocking) {
        lv_obj_clear_flag(s_loading, LV_OBJ_FLAG_CLICKABLE);   /* 放行输入 */
    }
}

void ui_loading_hide(void)
{
    if (!s_loading) {
        return;
    }
    lv_obj_t *w = s_loading;
    s_loading = NULL;
    lv_obj_delete(w);   /* 析构自动清 dot 动画 */
}

bool ui_loading_is_open(void)
{
    return s_loading != NULL;
}
