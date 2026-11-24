/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 关于页实现（单文件页：无 Model/View 拆分的最小示例）
 */
#include "About.h"

#include <stdio.h>

#include "App.h"   /* APP_NAME / APP_VERSION_STR / APP_DESC（应用自述） */
#include "i18n.h"
#include "ui_screen.h"

#ifndef APP_DESC
#define APP_DESC "KSDIY 通用应用模板\n页面框架 / 多语言 / 语音 / WiFi 管理"
#endif

static void about_release_notes_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    ui_toast(APP_NAME " v" APP_VERSION_STR, UI_TOAST_INFO);
}

static void on_release_notes(lv_event_t *e)
{
    (void)e;
    /* Toast 不能在事件回调内直接弹？可以——这里延迟一拍仅为演示定时器用法 */
    lv_timer_t *t = lv_timer_create(about_release_notes_cb, 1, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static lv_obj_t *about_page_create(lv_obj_t *parent, void *user_data)
{
    (void)user_data;

    lv_obj_t *root = ui_create_page_root(parent);
    const bool land = ui_screen_is_landscape();
    lv_obj_t *content = ui_create_content(root);
    ui_create_tab_root_header_bind(content, "关于", tr("关于本机与应用"));

    lv_obj_t *col_l = content;
    lv_obj_t *col_r = content;
    if (land) {
        lv_obj_t *cols = lv_obj_create(content);
        lv_obj_remove_style_all(cols);
        lv_obj_set_width(cols, LV_PCT(100));
        lv_obj_set_height(cols, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(cols, UI_PAD_SM, 0);
        lv_obj_clear_flag(cols, LV_OBJ_FLAG_SCROLLABLE);
        col_l = lv_obj_create(cols);
        col_r = lv_obj_create(cols);
        for (lv_obj_t *c = col_l; c; c = (c == col_l) ? col_r : NULL) {
            lv_obj_remove_style_all(c);
            lv_obj_set_width(c, LV_PCT(50));
            lv_obj_set_height(c, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(c, UI_PAD_SM, 0);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    /* ── 应用卡：身份来自各应用 App.h（APP_NAME/APP_DESC），非模板硬编码 ── */
    lv_obj_t *card = ui_create_card(land ? col_l : content);
    ui_create_section_title(card, APP_NAME);

    lv_obj_t *logo_row = ui_create_row(card);
    lv_obj_t *logo = ui_create_label(logo_row, "K", land ? UI_FONT_BODY : UI_FONT_TITLE,
                                     UI_COLOR_ACCENT);
    lv_obj_set_style_text_color(logo, UI_COLOR_ACCENT, 0);
    lv_obj_t *name = ui_create_label(logo_row,
                                     land ? tr("拓竹 LAN 面板") : tr(APP_DESC), NULL,
                                     UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(name, 1);
    if (land) {
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    }

    char ver_row[96];
    snprintf(ver_row, sizeof(ver_row), "%s v%s", APP_NAME, APP_VERSION_STR);
    ui_create_label(card, ver_row, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

    ui_create_btn_bind(card, LV_SYMBOL_LIST " 版本信息", 0, UI_BTN_H_SM,
                       UI_BTN_OUTLINE, on_release_notes, NULL);

    /* ── 硬件卡 ── */
    lv_obj_t *hw_card = ui_create_card(land ? col_r : content);
    ui_create_section_title_bind(hw_card, "硬件");

    lv_obj_t *chip_row = ui_create_row(hw_card);
    ui_create_label_bind(chip_row, "主控", NULL, UI_COLOR_TEXT_SEC);
    lv_obj_t *chip = ui_create_label(chip_row, "F101S3 + AIC8800D40L", UI_FONT_CAPTION, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(chip, 1);

    lv_obj_t *lcd_row = ui_create_row(hw_card);
    ui_create_label_bind(lcd_row, "屏幕", NULL, UI_COLOR_TEXT_SEC);
    char lcd_txt[48];
    if (ui_screen_is_landscape()) {
        snprintf(lcd_txt, sizeof(lcd_txt), "%dx%d RGB / %dx%d",
                 UI_PANEL_W, UI_PANEL_H,
                 (int)ui_screen_w(), (int)ui_screen_h());
    } else {
        snprintf(lcd_txt, sizeof(lcd_txt), "%dx%d RGB", UI_PANEL_W, UI_PANEL_H);
    }
    lv_obj_t *lcd = ui_create_label(lcd_row, lcd_txt, UI_FONT_CAPTION, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(lcd, 1);

    if (!land) {
        lv_obj_t *audio_row = ui_create_row(hw_card);
        ui_create_label_bind(audio_row, "音频", NULL, UI_COLOR_TEXT_SEC);
        lv_obj_t *audio = ui_create_label(audio_row, "ES8311 / 扬声器", UI_FONT_CAPTION,
                                          UI_COLOR_TEXT);
        lv_obj_set_flex_grow(audio, 1);

        lv_obj_t *power_row = ui_create_row(hw_card);
        ui_create_label_bind(power_row, "电源", NULL, UI_COLOR_TEXT_SEC);
        lv_obj_t *power = ui_create_label(power_row, "AXP2101 PMIC", UI_FONT_CAPTION, UI_COLOR_TEXT);
        lv_obj_set_flex_grow(power, 1);
    }

    /* ── 开源卡 ── */
    lv_obj_t *link_card = ui_create_card(content);
    ui_create_section_title_bind(link_card, "开源");
    ui_create_label(link_card,
                    land ? tr("酷世DIY · github.com/kevincoooool/ESP32P4_KSDIY")
                         : tr("淘宝店铺「酷世DIY」\ngithub.com/kevincoooool/ESP32P4_KSDIY\n"
                              "Apache-2.0 License"),
                    NULL, UI_COLOR_TEXT_SEC);

    return root;
}

const page_t page_about = {
    .name = "关于",
    .create = about_page_create,
    .destroy = NULL,
    .user_data = NULL,
};
