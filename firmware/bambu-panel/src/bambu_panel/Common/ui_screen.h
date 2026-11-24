/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * LVGL 逻辑分辨率（随 PPA 90° 横屏切换；物理面板恒为 480×800）
 */
#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_PANEL_W  480
#define UI_PANEL_H  800

/** 在显示/LVGL 初始化前调用：0=竖屏，90=横屏（显示与触摸同步旋转） */
void ui_screen_apply_rotation(uint16_t degrees);

lv_coord_t ui_screen_w(void);
lv_coord_t ui_screen_h(void);
bool ui_screen_is_landscape(void);

/* ── 应用私有扩展（舰队同步合并保留）── */
/** Tab 页内容区可用高度（扣状态栏 + Tab 栏 + 边距） */
lv_coord_t ui_page_body_h(void);

/** 横屏间距 / 控件高度（竖屏回退到 UI_PAD、UI_BTN_H*） */
int ui_land_gap(void);
lv_coord_t ui_land_btn_h(void);
lv_coord_t ui_land_btn_h_sm(void);
lv_coord_t ui_land_input_h(void);

/** 横屏：限制 flex 子项高度并启用内部滚动，避免把底部按钮挤出屏幕 */
void ui_land_cap_scroll_area(lv_obj_t *obj, lv_coord_t reserve_px);

lv_coord_t ui_land_status_bar_h(void);
lv_coord_t ui_land_tab_bar_h(void);

#define UI_SCREEN_W ui_screen_w()
#define UI_SCREEN_H ui_screen_h()

#ifdef __cplusplus
}
#endif
