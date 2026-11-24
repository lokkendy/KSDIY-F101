/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 输入法键盘（覆盖层）实现
 *
 * 键盘挂载在 lv_layer_top()，跨页面存活；页面销毁时需显式调用
 * ui_keyboard_hide() 收起。弹出时自动滚动目标输入框到可视区。
 * 收起采用延迟一帧判定，避免「焦点从输入框 A 切到 B」时误关键盘。
 */
#include "ui_keyboard.h"

#include <stdbool.h>

/* ──────────────────── 内部状态 ──────────────────── */

static lv_obj_t *s_kb = NULL;   /* 键盘本体 */
static lv_obj_t *s_ta = NULL;   /* 当前绑定的输入框 */

/* ──────────────────── 收起判定 ──────────────────── */

static void hide_deferred_cb(void *arg)
{
    (void)arg;
    if (!s_kb) return;
    /* 绑定输入框仍持有焦点（焦点刚切换过来）：保持键盘 */
    if (s_ta && lv_obj_has_state(s_ta, LV_STATE_FOCUSED)) return;
    ui_keyboard_hide();
}

/* ──────────────────── 键盘事件 ──────────────────── */

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        /* 取消 / 确定：收起键盘（删除时 LVGL 会自动解除输入框焦点） */
        ui_keyboard_hide();
    }
}

static void ta_focus_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        ui_keyboard_attach(ta);
    } else if (code == LV_EVENT_DEFOCUSED && s_ta == ta) {
        /* 焦点可能正切换到另一输入框，延迟一帧再决定是否收起 */
        lv_async_call(hide_deferred_cb, NULL);
    }
}

/* ──────────────────── 键盘创建 ──────────────────── */

static bool kb_create(void)
{
    if (s_kb) return false;

    s_kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_kb, UI_SCREEN_W, UI_KB_H);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* 暗色工业风样式 */
    lv_obj_set_style_bg_color(s_kb, UI_COLOR_BG_BAR, 0);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb, 1, 0);
    lv_obj_set_style_border_color(s_kb, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_side(s_kb, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(s_kb, 0, 0);
    lv_obj_set_style_pad_all(s_kb, 6, 0);
    lv_obj_set_style_pad_row(s_kb, 6, 0);
    lv_obj_set_style_pad_column(s_kb, 5, 0);

    /* 按键样式：加高加大，便于触控 */
    lv_obj_set_style_bg_color(s_kb, UI_COLOR_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb, UI_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(s_kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_min_height(s_kb, UI_KB_KEY_H, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(s_kb, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, UI_COLOR_CARD_HI, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(s_kb, UI_COLOR_PRIMARY, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_kb, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);

    /* 按键字号 28px FreeType；符号走 montserrat fallback */
    lv_obj_set_style_text_font(s_kb, UI_FONT_KB, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_kb, UI_FONT_KB, 0);

    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);
    return true;
}

/* 入场滑动动画：从屏幕底部滑入。
 * 注意：kb_create 设了 LV_ALIGN_BOTTOM_MID，此对齐下 lv_obj_set_y()
 * 的值是相对“贴底位置”的偏移量：0 = 恰好贴底，h = 完全滑出屏幕底。
 * 所以动画必须 h → 0（屏外滑入到贴底）；之前误用了 800 → 800-h
 * 的绝对坐标，两个值都是正偏移，键盘永远停在屏幕外。 */
static void kb_slide_in(lv_obj_t *kb)
{
    /* lv_obj_get_height() 只读 coords，不触发布局刷新；键盘刚创建、
     * 布局未算，coords 高度为 0，必须先刷新布局拿到真实高度。 */
    lv_obj_update_layout(kb);
    lv_coord_t h = lv_obj_get_height(kb);
    if (h <= 0) h = UI_KB_H;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, kb);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, h, 0);
    lv_anim_set_duration(&a, 160);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ──────────────────── 公开接口 ──────────────────── */

void ui_keyboard_bind(lv_obj_t *ta)
{
    if (!ta) return;
    /* LVGL 9 点击聚焦需要显式标志（textarea 构造只加 SCROLL_ON_FOCUS），
     * 否则指针点击不产生 LV_EVENT_FOCUSED，键盘永远不触发。 */
    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_DEFOCUSED, NULL);
}

void ui_keyboard_attach(lv_obj_t *ta)
{
    if (!ta) return;

    bool fresh = kb_create();
    if (!s_kb) return;

    s_ta = ta;
    lv_keyboard_set_textarea(s_kb, ta);

    /* 密码框保持默认全键盘（WiFi 密码含混合字符），
     * 布局可用键盘自带的切换按钮（LV_SYMBOL_KEYBOARD）变换。 */

    /* 键盘可能遮挡输入框，滚动到可视区 */
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);

    if (fresh) {
        kb_slide_in(s_kb);
    }
}

void ui_keyboard_detach(lv_obj_t *ta)
{
    if (!s_kb || !s_ta || s_ta != ta) return;
    lv_async_call(hide_deferred_cb, NULL);
}

void ui_keyboard_hide(void)
{
    s_ta = NULL;
    if (!s_kb) return;
    lv_obj_delete(s_kb);
    s_kb = NULL;
}

bool ui_keyboard_active(void)
{
    return s_kb != NULL;
}
