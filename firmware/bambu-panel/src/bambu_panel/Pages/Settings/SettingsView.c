/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 系统设置页 View 实现
 */
#include "SettingsView.h"

#include <stdio.h>

#include "i18n.h"
#include "ui_keyboard.h"
#include "ui_screen.h"

/* 统一深色输入框 */
static lv_obj_t *create_input(lv_obj_t *parent, const char *placeholder, bool password)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, ui_land_input_h());
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_one_line(ta, true);
    ui_style_textarea_field(ta);
    if (password) {
        lv_textarea_set_password_mode(ta, true);
    }
    return ta;
}

/* 密码明文/掩码切换：连不上先核对输入（reason 15 十有八九是输错） */
static void on_pwd_eye(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_user_data(e);
    bool hidden = lv_textarea_get_password_mode(ta);
    lv_textarea_set_password_mode(ta, !hidden);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_label_set_text(lv_obj_get_child(btn, 0),
                      hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

/* 胶囊二选一（设计稿「深色/浅色」「中文/English」）：选中实心主色，未选中描边次级。
 * 回调 user_data = 选项序号（intptr），Presenter 直接读序号。 */
static void pill_pair(lv_obj_t *parent, const char *t0, const char *t1,
                      uint8_t sel, lv_event_cb_t cb)
{
    for (int i = 0; i < 2; i++) {
        const char *txt = (i == 0) ? t0 : t1;
        bool on = (sel == i);

        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, ui_screen_is_landscape() ? 34 : 40);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_hor(btn, 6, 0);
        lv_obj_set_style_bg_color(btn, on ? UI_COLOR_PRIMARY : UI_COLOR_CARD, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, on ? 0 : 1, 0);
        lv_obj_set_style_border_color(btn, UI_COLOR_BORDER, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_HI, LV_STATE_PRESSED);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, txt);
        lv_obj_set_style_text_font(lbl, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl,
            on ? (lv_color_luminance(UI_COLOR_PRIMARY) >= 145
                     ? lv_color_hex(0x17202A) : lv_color_white())
               : UI_COLOR_TEXT, 0);
        lv_obj_center(lbl);

        if (cb) {
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }
}

/* 胶囊键绑定版（深色/浅色等可翻译对）：两枚按钮为父容器末两个子对象 */
static void pill_pair_bind(lv_obj_t *parent, const char *k0, const char *k1,
                           uint8_t sel, lv_event_cb_t cb)
{
    pill_pair(parent, tr(k0), tr(k1), sel, cb);
    int32_t n = (int32_t)lv_obj_get_child_count(parent);
    if (n >= 2) {
        tr_bind(lv_obj_get_child(lv_obj_get_child(parent, n - 2), 0), k0);
        tr_bind(lv_obj_get_child(lv_obj_get_child(parent, n - 1), 0), k1);
    }
}

/* 滑条百分数标签：拖动实时刷新（user_data=百分数标签） */
static void on_slider_pct(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *lbl = lv_event_get_user_data(e);
    int32_t v = lv_slider_get_value(slider);
    int32_t max = lv_slider_get_max_value(slider);
    uint8_t pct = 0;
    if (max > 0) {
        int32_t raw = v * 100 / max;
        if (raw < 0) {
            raw = 0;
        } else if (raw > 100) {
            raw = 100;
        }
        pct = (uint8_t)raw;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
    lv_label_set_text(lbl, buf);
}

/* 创建「名称 + 右侧百分数 + 全宽滑条」的设置行（设计稿显示设置版式）；
 * 名称走键绑定（语言切换原地重译） */
static lv_obj_t *create_pct_slider(lv_obj_t *parent, const char *zh_name,
                                   lv_obj_t **pct_lbl, int32_t min, int32_t max,
                                   int32_t value, lv_event_cb_t on_change)
{
    lv_obj_t *head = ui_create_row(parent);
    lv_obj_t *name_lbl = ui_create_label_bind(head, zh_name, NULL, UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(name_lbl, 1);
    *pct_lbl = ui_create_label(head, "--", NULL, UI_COLOR_TEXT);

    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, UI_COLOR_TRACK, 0);
    lv_obj_set_style_bg_color(slider, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UI_COLOR_PRIMARY, LV_PART_KNOB);

    /* 百分数先手动算一次初值，再挂实时刷新（钳 0..100 + %u，
     * 与 on_slider_pct 同款写法——%d 进 char[8] 过不了 -Wformat-truncation） */
    uint8_t pct = 0;
    if (max > 0) {
        int32_t raw = value * 100 / max;
        if (raw < 0) {
            raw = 0;
        } else if (raw > 100) {
            raw = 100;
        }
        pct = (uint8_t)raw;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
    lv_label_set_text(*pct_lbl, buf);
    lv_obj_add_event_cb(slider, on_slider_pct, LV_EVENT_VALUE_CHANGED, *pct_lbl);

    if (on_change) {
        lv_obj_add_event_cb(slider, on_change, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(slider, on_change, LV_EVENT_RELEASED, NULL);
    }
    return slider;
}

void SettingsView_Create(SettingsView_t *v, lv_obj_t *parent,
                         const char *saved_ssid, const char *saved_pass,
                         uint8_t brightness, uint8_t language, uint8_t theme,
                         uint8_t rotation, bool voice_on, bool touch_on,
                         uint8_t volume,
                         lv_event_cb_t on_scan, lv_event_cb_t on_connect,
                         lv_event_cb_t on_disconnect, lv_event_cb_t on_brightness,
                         lv_event_cb_t on_language, lv_event_cb_t on_theme,
                         lv_event_cb_t on_rotation,
                         lv_event_cb_t on_voice, lv_event_cb_t on_touch_sound,
                         lv_event_cb_t on_volume,
                         lv_event_cb_t on_reboot, lv_event_cb_t on_shutdown)
{
    const bool land = ui_screen_is_landscape();

    lv_obj_t *root = ui_create_page_root(parent);
    v->root = root;

    lv_obj_t *content = ui_create_content(root);
    ui_create_tab_root_header_bind(content, "设置", NULL);

    lv_obj_t *col_l = content;
    lv_obj_t *col_r = content;
    if (land) {
        lv_obj_t *cols = lv_obj_create(content);
        lv_obj_remove_style_all(cols);
        lv_obj_set_width(cols, LV_PCT(100));
        ui_land_cap_scroll_area(cols, ui_land_btn_h() + UI_PAD_SM);
        lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(cols, UI_PAD_SM, 0);
        col_l = lv_obj_create(cols);
        col_r = lv_obj_create(cols);
        for (lv_obj_t *c = col_l; c; c = (c == col_l) ? col_r : NULL) {
            lv_obj_remove_style_all(c);
            lv_obj_set_width(c, 0);
            lv_obj_set_flex_grow(c,1);
            lv_obj_set_height(c, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(c, UI_PAD_SM, 0);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    /* ── WiFi 卡 ── */
    lv_obj_t *wifi_card = ui_create_card(land ? col_l : content);
    ui_create_section_title_bind(wifi_card, "WiFi 网络");

    ui_create_btn_bind(wifi_card, LV_SYMBOL_WIFI " 扫描附近 WiFi", LV_PCT(100),
                       land ? ui_land_btn_h_sm() : UI_BTN_H,
                       UI_BTN_OUTLINE, on_scan, NULL);

    v->txt_ssid = create_input(wifi_card, tr("WiFi 名称 (SSID)"), false);
    lv_obj_t *pwd_row = ui_create_row(wifi_card);
    v->txt_pass = create_input(pwd_row, tr("WiFi 密码"), true);
    lv_obj_set_flex_grow(v->txt_pass, 1);
    lv_obj_set_width(v->txt_pass, 0);
    ui_create_btn(pwd_row, LV_SYMBOL_EYE_OPEN, land ? 40 : 46, land ? 36 : 46,
                  UI_BTN_OUTLINE, on_pwd_eye, v->txt_pass);
    ui_keyboard_bind(v->txt_ssid);
    ui_keyboard_bind(v->txt_pass);
    if (saved_ssid[0]) {
        lv_textarea_set_text(v->txt_ssid, saved_ssid);
        lv_textarea_set_text(v->txt_pass, saved_pass);
    }

    lv_obj_t *wifi_btn_row = ui_create_row(wifi_card);
    ui_create_btn_bind(wifi_btn_row, LV_SYMBOL_WIFI " 连接", 0,
                       UI_BTN_H_SM, UI_BTN_FILLED, on_connect, NULL);
    ui_create_btn_bind(wifi_btn_row, "断开并清除", 0, UI_BTN_H_SM,
                       UI_BTN_OUTLINE, on_disconnect, NULL);

    lv_obj_t *wifi_state_row = ui_create_row(wifi_card);
    v->lbl_wifi_state = ui_create_label(wifi_state_row, tr("未连接"), NULL, UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(v->lbl_wifi_state, 1);
    lv_label_set_long_mode(v->lbl_wifi_state, LV_LABEL_LONG_DOT);
    v->lbl_wifi_ip = ui_create_label(wifi_state_row, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

    /* ── 显示卡 ── */
    lv_obj_t *disp_card = ui_create_card(land ? col_r : content);
    ui_create_section_title_bind(disp_card, "显示");

    /* 亮度：名称 + 右侧百分数 + 全宽滑条（设计稿版式） */
    if (brightness < 30) brightness = 30;
    create_pct_slider(disp_card, LV_SYMBOL_TINT " 屏幕亮度",
                      &v->lbl_bri_pct, 30, 255, brightness, on_brightness);

    /* 主题行：胶囊二选一（设计稿「深色/浅色」；切换后由 Presenter 重建外壳） */
    lv_obj_t *theme_row = ui_create_row(disp_card);
    lv_obj_t *theme_lbl = ui_create_label_bind(theme_row, LV_SYMBOL_EYE_OPEN " 主题模式",
                                               NULL, UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(theme_lbl, 1);
    pill_pair_bind(theme_row, "深色", "浅色", theme <= 1 ? theme : 0, on_theme);

    /* 语言行（标签双语言并书不翻译；选项用各自语言的原生名） */
    lv_obj_t *lang_row = ui_create_row(disp_card);
    lv_obj_t *lang_lbl = ui_create_label(lang_row, "语言 / Language", NULL, UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(lang_lbl, 1);
    pill_pair(lang_row, "中文", "English", language <= 1 ? language : 0, on_language);

    /* 屏幕方向（PPA 旋转 0/90/180/270，切换后需重启） */
    lv_obj_t *rot_row = ui_create_row(disp_card);
    lv_obj_set_height(rot_row, LV_SIZE_CONTENT);
    ui_create_label_bind(rot_row, LV_SYMBOL_IMAGE " 屏幕方向", NULL, UI_COLOR_TEXT_SEC);
    v->dd_rot = lv_dropdown_create(rot_row);
    lv_dropdown_set_options(v->dd_rot, tr("竖屏\n横屏 90°\n竖屏 180°\n横屏 270°"));
    lv_dropdown_set_selected(v->dd_rot, rotation <= 3 ? rotation : 0);
    lv_obj_set_width(v->dd_rot, 186);
    ui_style_dropdown(v->dd_rot);
    if (on_rotation) {
        lv_obj_add_event_cb(v->dd_rot, on_rotation, LV_EVENT_VALUE_CHANGED, NULL);
    }

    /* 音量：名称 + 右侧百分数 + 全宽滑条（ES8311 DAC 全局音量） */
    if (volume > 100) volume = 100;
    create_pct_slider(disp_card, LV_SYMBOL_VOLUME_MAX " 音量",
                      &v->lbl_vol_pct, 0, 100, volume, on_volume);

    /* 语音提示开关行（操作结果双语播报） */
    lv_obj_t *voice_row = ui_create_row(disp_card);
    ui_create_label_bind(voice_row, LV_SYMBOL_AUDIO " 语音提示", NULL, UI_COLOR_TEXT_SEC);
    lv_obj_t *sw_voice = lv_switch_create(voice_row);
    lv_obj_set_style_bg_color(sw_voice, UI_COLOR_TRACK, 0);
    lv_obj_set_style_bg_color(sw_voice, UI_COLOR_SUCCESS, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (voice_on) {
        lv_obj_add_state(sw_voice, LV_STATE_CHECKED);
    }
    if (on_voice) {
        lv_obj_add_event_cb(sw_voice, on_voice, LV_EVENT_VALUE_CHANGED, NULL);
    }

    /* 触摸反馈音开关（全页面按键「嘟」声） */
    lv_obj_t *touch_row = ui_create_row(disp_card);
    ui_create_label_bind(touch_row, LV_SYMBOL_BELL " 触摸反馈音", NULL, UI_COLOR_TEXT_SEC);
    lv_obj_t *sw_touch_sound = lv_switch_create(touch_row);
    lv_obj_set_style_bg_color(sw_touch_sound, UI_COLOR_TRACK, 0);
    lv_obj_set_style_bg_color(sw_touch_sound, UI_COLOR_SUCCESS, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (touch_on) {
        lv_obj_add_state(sw_touch_sound, LV_STATE_CHECKED);
    }
    if (on_touch_sound) {
        lv_obj_add_event_cb(sw_touch_sound, on_touch_sound, LV_EVENT_VALUE_CHANGED, NULL);
    }

    /* ── 设备信息卡 ── */
    lv_obj_t *sys_card = ui_create_card(land ? col_l : content);
    ui_create_section_title_bind(sys_card, "设备信息");

    lv_obj_t *chip_row = ui_create_row(sys_card);
    ui_create_label_bind(chip_row, "芯片型号", NULL, UI_COLOR_TEXT_SEC);
    lv_obj_t *lbl_chip = ui_create_label(chip_row, "F101S3 / AIC8800D40L",
                                         UI_FONT_CAPTION, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(lbl_chip, 1);

    lv_obj_t *id_row = ui_create_row(sys_card);
    ui_create_label_bind(id_row, "设备 ID", NULL, UI_COLOR_TEXT_SEC);
    v->lbl_devid = ui_create_label(id_row, "--", UI_FONT_CAPTION, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(v->lbl_devid, 1);
    lv_label_set_long_mode(v->lbl_devid, LV_LABEL_LONG_DOT);

    lv_obj_t *ver_row = ui_create_row(sys_card);
    ui_create_label_bind(ver_row, "固件版本", NULL, UI_COLOR_TEXT_SEC);
    lv_obj_t *lbl_ver = ui_create_label(ver_row, "Bambu Panel R12", UI_FONT_CAPTION, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(lbl_ver, 1);

    if (!land) {
        lv_obj_t *build_row = ui_create_row(sys_card);
        ui_create_label_bind(build_row, "编译时间", NULL, UI_COLOR_TEXT_SEC);
        lv_obj_t *lbl_build = ui_create_label(build_row, __DATE__ " " __TIME__,
                                              UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        lv_obj_set_flex_grow(lbl_build, 1);
    }

    /* ── 关于卡 ── */
    lv_obj_t *about_card = ui_create_card(land ? col_r : content);
    ui_create_section_title_bind(about_card, "关于");
    lv_obj_t *about = land
        ? ui_create_label(about_card, tr("KSDIY · 酷世DIY"), NULL, UI_COLOR_TEXT_SEC)
        : ui_create_label_bind(about_card,
            "KSDIY 通用应用模板\n淘宝店铺「酷世DIY」\ngithub.com/kevincoooool/ESP32P4_KSDIY",
            NULL, UI_COLOR_TEXT_SEC);
    lv_obj_set_width(about, LV_PCT(100));

    /* 电源操作固定在底部，避免被 Tab 栏挡住 */
    lv_obj_t *footer = ui_create_page_footer(root);
    lv_obj_t *pwr_row = ui_create_row(footer);
    lv_obj_set_style_pad_column(pwr_row, 10, 0);
    ui_create_btn_bind(pwr_row, LV_SYMBOL_POWER " 关机", 0, ui_land_btn_h(), UI_BTN_DANGER,
                       on_shutdown, NULL);
    ui_create_btn_bind(pwr_row, LV_SYMBOL_POWER " 重启", 0, ui_land_btn_h(), UI_BTN_OUTLINE,
                       on_reboot, NULL);

    if (!land) {
        /* 设计稿首屏顺序：显示与声音 → 设备信息 → 网络与关于。 */
        lv_obj_move_to_index(disp_card, 1);
        lv_obj_move_to_index(sys_card, 2);
    }
}
