/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 */
#include "ui_screen.h"
#include "ui_common.h"

static lv_coord_t s_w = UI_PANEL_W;
static lv_coord_t s_h = UI_PANEL_H;

void ui_screen_apply_rotation(uint16_t degrees)
{
    if (degrees == 90 || degrees == 270) {
        s_w = UI_PANEL_H;
        s_h = UI_PANEL_W;
    } else {
        s_w = UI_PANEL_W;
        s_h = UI_PANEL_H;
    }
}

lv_coord_t ui_screen_w(void)
{
    lv_display_t *d = lv_display_get_default();
    return d ? lv_display_get_horizontal_resolution(d) : s_w;
}

lv_coord_t ui_screen_h(void)
{
    lv_display_t *d = lv_display_get_default();
    return d ? lv_display_get_vertical_resolution(d) : s_h;
}

bool ui_screen_is_landscape(void)
{
    return ui_screen_w() > ui_screen_h();
}

/* ── 应用私有扩展（舰队同步合并保留）── */

lv_coord_t ui_land_status_bar_h(void)
{
    return ui_screen_is_landscape() ? 40 : UI_STATUS_BAR_H;
}

lv_coord_t ui_land_tab_bar_h(void)
{
    /* 含 create_tab_bar 底部 pad，避免 Tab 文字贴屏幕下缘 */
    return ui_screen_is_landscape() ? 0 : (UI_TAB_BAR_H + 6);
}

lv_coord_t ui_page_body_h(void)
{
    lv_coord_t h = ui_screen_h() - ui_land_status_bar_h() - ui_land_tab_bar_h() - UI_PAD_SM * 2;
    if (h < 140) {
        h = 140;
    }
    return h;
}

int ui_land_gap(void)
{
    return ui_screen_is_landscape() ? 10 : UI_PAD;
}

lv_coord_t ui_land_btn_h(void)
{
    return ui_screen_is_landscape() ? 38 : UI_BTN_H;
}

lv_coord_t ui_land_btn_h_sm(void)
{
    return ui_screen_is_landscape() ? 32 : UI_BTN_H_SM;
}

lv_coord_t ui_land_input_h(void)
{
    return ui_screen_is_landscape() ? 36 : 46;
}

void ui_land_cap_scroll_area(lv_obj_t *obj, lv_coord_t reserve_px)
{
    if (!obj || !ui_screen_is_landscape()) {
        return;
    }
    lv_coord_t max_h = ui_page_body_h() - reserve_px;
    if (max_h < 72) {
        max_h = 72;
    }
    lv_obj_set_flex_grow(obj, 0);
    lv_obj_set_height(obj, max_h);
    lv_obj_set_style_max_height(obj, max_h, 0);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_AUTO);
}
