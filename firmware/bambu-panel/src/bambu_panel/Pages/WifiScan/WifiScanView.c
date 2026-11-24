/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - WiFi 扫描页 View 实现
 */
#include "WifiScanView.h"
#include "i18n.h"
#include "ui_modal.h"
#include "ui_screen.h"
#include "ui_widgets.h"   /* ui_w_tint：选中热点浅染底 */

#include <stdio.h>
#include <string.h>

#include "esp_wifi.h"   /* WIFI_AUTH_* 常量 */

static const char *auth_short_name(uint8_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:          return tr("开放");
    case WIFI_AUTH_WEP:           return "WEP";
    case WIFI_AUTH_WPA_PSK:       return "WPA";
    case WIFI_AUTH_WPA2_PSK:      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA/2";
    case WIFI_AUTH_WPA3_PSK:      return "WPA3";
    default:                      return "";
    }
}

void WifiScanView_Create(WifiScanView_t *v, lv_obj_t *parent,
                         lv_event_cb_t on_refresh, lv_event_cb_t on_back)
{
    lv_obj_t *root = ui_create_page_root(parent);
    v->root = root;

    lv_obj_t *content = ui_create_content(root);
    ui_create_page_header(content, "Wi-Fi", NULL, on_back);

    lv_obj_t *footer = ui_create_page_footer(root);
    ui_create_btn_bind(footer, LV_SYMBOL_REFRESH " 扫描网络", LV_PCT(100), ui_land_btn_h(),
                       UI_BTN_OUTLINE, on_refresh, NULL);

    v->lbl_status = ui_create_label(content, tr("可用网络"),
                                    UI_FONT_BODY, UI_COLOR_TEXT_SEC);
    lv_obj_set_width(v->lbl_status, LV_PCT(100));

    v->ap_list = lv_obj_create(content);
    lv_obj_set_width(v->ap_list, LV_PCT(100));
    if (ui_screen_is_landscape()) {
        ui_land_cap_scroll_area(v->ap_list, 0);
    } else {
        lv_obj_set_flex_grow(v->ap_list, 1);
        lv_obj_set_style_min_height(v->ap_list, 0, 0);
    }
    lv_obj_set_flex_flow(v->ap_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(v->ap_list, 8, 0);
    lv_obj_set_style_bg_opa(v->ap_list, 0, 0);
    lv_obj_set_style_border_width(v->ap_list, 0, 0);
    lv_obj_set_style_pad_all(v->ap_list, 0, 0);
    lv_obj_set_scrollbar_mode(v->ap_list, LV_SCROLLBAR_MODE_AUTO);
}

void WifiScanView_SetStatus(WifiScanView_t *v, const char *text)
{
    if (v->lbl_status && text) {
        lv_label_set_text(v->lbl_status, text);
    }
}

void WifiScanView_ShowScanning(WifiScanView_t *v)
{
    if (!v->ap_list) return;
    lv_obj_clean(v->ap_list);
    ui_create_label(v->ap_list, tr("正在扫描..."), NULL, UI_COLOR_TEXT_SEC);
}

void WifiScanView_ShowApList(WifiScanView_t *v, const wifi_ap_t *aps, uint16_t count,
                             const char *active_ssid, lv_event_cb_t on_ap_click)
{
    if (!v->ap_list) return;

    lv_obj_clean(v->ap_list);

    if (count == 0 || !aps) {
        ui_create_label(v->ap_list, tr("未发现热点，请点刷新重试"), NULL, UI_COLOR_TEXT_SEC);
        return;
    }

    for (uint16_t i = 0; i < count; i++) {
        /* 当前/上次选择的热点：主色描边 + 浅染底 + 实心圆点（设计稿选中态） */
        bool active = active_ssid && active_ssid[0] &&
                      strcmp(aps[i].ssid, active_ssid) == 0;

        lv_obj_t *btn = lv_btn_create(v->ap_list);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(btn, 14, 0);
        lv_obj_set_style_radius(btn, UI_BTN_RADIUS, 0);
        lv_obj_set_style_bg_color(btn, active ? ui_w_tint(UI_COLOR_PRIMARY, 24)
                                              : UI_COLOR_CARD, 0);
        lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_HI, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, active ? UI_COLOR_PRIMARY : UI_COLOR_BORDER, 0);
        lv_obj_set_style_border_color(btn, UI_COLOR_PRIMARY, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, on_ap_click, LV_EVENT_CLICKED, NULL);

        lv_obj_t *row = lv_obj_create(btn);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

        /* 左侧单选圆点：实心主色 / 空心描边 */
        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_set_size(dot, 18, 18);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 2, 0);
        lv_obj_set_style_border_color(dot, active ? UI_COLOR_PRIMARY : UI_COLOR_BORDER, 0);
        lv_obj_set_style_bg_color(dot, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(dot, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *ssid_lbl = lv_label_create(row);
        lv_label_set_text(ssid_lbl, aps[i].ssid);
        lv_obj_set_style_text_font(ssid_lbl, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(ssid_lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_flex_grow(ssid_lbl, 1);
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);

        char meta[32];
        snprintf(meta, sizeof(meta), "%s  %d dBm",
                 aps[i].authmode == WIFI_AUTH_OPEN ? LV_SYMBOL_WIFI : LV_SYMBOL_EYE_CLOSE,
                 (int)aps[i].rssi);
        lv_obj_t *meta_lbl = lv_label_create(row);
        lv_label_set_text(meta_lbl, meta);
        lv_obj_set_style_text_font(meta_lbl, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(meta_lbl, UI_COLOR_ACCENT, 0);
    }
}

/* 密码明文/掩码切换：连不上时先核对输的啥（reason 15 十有八九是输错） */
static void on_pwd_eye(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_user_data(e);
    bool hidden = lv_textarea_get_password_mode(ta);
    lv_textarea_set_password_mode(ta, !hidden);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_label_set_text(lv_obj_get_child(btn, 0),
                      hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

/* ── 密码弹窗：ui_modal 统一层（DESIGN §9，删除 View 私有遮罩） ── */

typedef struct {
    WifiScanView_t *v;
    char ssid[33];
    uint8_t authmode;
    lv_event_cb_t on_cancel;
    lv_event_cb_t on_connect;
} pwd_dlg_ctx_t;

/* ui_modal 关闭前回调：收键盘 + 清字段 + 释放 ctx（唯一释放点） */
static void pwd_on_close(lv_obj_t *card, void *ud)
{
    (void)card;
    pwd_dlg_ctx_t *c = (pwd_dlg_ctx_t *)ud;
    ui_keyboard_hide();
    c->v->pwd_card = NULL;
    c->v->pwd_ta = NULL;
    lv_free(c);
}

static void pwd_build(lv_obj_t *card, void *ud)
{
    pwd_dlg_ctx_t *c = (pwd_dlg_ctx_t *)ud;
    WifiScanView_t *v = c->v;

    /* 偏上放置：键盘弹出时不遮挡输入行 */
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 72);

    char title[64];
    snprintf(title, sizeof(title), tr("连接 %s"), c->ssid);
    ui_create_label(card, title, UI_FONT_TITLE, UI_COLOR_TEXT);

    const char *hint = (c->authmode == WIFI_AUTH_OPEN)
                           ? tr("开放网络，密码可留空")
                           : auth_short_name(c->authmode);
    ui_create_label(card, hint, NULL, UI_COLOR_TEXT_SEC);

    lv_obj_t *pwd_row = ui_create_row(card);
    v->pwd_ta = lv_textarea_create(pwd_row);
    lv_obj_set_flex_grow(v->pwd_ta, 1);
    lv_obj_set_height(v->pwd_ta, ui_land_input_h());
    lv_textarea_set_one_line(v->pwd_ta, true);
    lv_textarea_set_placeholder_text(v->pwd_ta, tr("WiFi 密码"));
    lv_textarea_set_password_mode(v->pwd_ta, true);
    ui_style_textarea_field(v->pwd_ta);

    ui_create_btn(pwd_row, LV_SYMBOL_EYE_OPEN, 46, 46,
                  UI_BTN_OUTLINE, on_pwd_eye, v->pwd_ta);

    ui_keyboard_bind(v->pwd_ta);

    lv_obj_t *btn_row = ui_create_row(card);
    ui_create_btn(btn_row, tr("取消"), 0, UI_BTN_H_SM, UI_BTN_OUTLINE,
                  c->on_cancel, NULL);
    ui_create_btn(btn_row, tr(LV_SYMBOL_OK " 连接"), 0, UI_BTN_H_SM,
                  UI_BTN_FILLED, c->on_connect, NULL);

    ui_keyboard_attach(v->pwd_ta);
}

void WifiScanView_ShowPwdDialog(WifiScanView_t *v, const char *ssid, uint8_t authmode,
                                lv_event_cb_t on_cancel, lv_event_cb_t on_connect)
{
    if (!ssid || ssid[0] == '\0') return;

    WifiScanView_ClosePwdDialog(v);   /* 单例 */

    pwd_dlg_ctx_t *c = lv_malloc_zeroed(sizeof(pwd_dlg_ctx_t));
    c->v = v;
    snprintf(c->ssid, sizeof(c->ssid), "%s", ssid);
    c->authmode = authmode;
    c->on_cancel = on_cancel;
    c->on_connect = on_connect;

    ui_modal_builder_t b = {
        .build = pwd_build,
        .on_close = pwd_on_close,
        .user_data = c,
        .dim = LV_OPA_60,
    };
    v->pwd_card = ui_modal_open(&b);
}

void WifiScanView_ClosePwdDialog(WifiScanView_t *v)
{
    ui_keyboard_hide();
    if (v->pwd_card) {
        ui_modal_close(v->pwd_card);   /* on_close 里清 pwd_card/pwd_ta */
    }
    v->pwd_ta = NULL;   /* 立即失效，防出动画期间被读 */
}

/* ── 连接失败弹窗：ui_modal 统一层（原因 + 重试 / 重新输入密码 / 取消） ── */

typedef struct {
    WifiScanView_t *v;
    char ssid[33];
    const char *reason_text;   /* 静态串（词典/HAL 返回），不拷贝 */
    int reason;
    lv_event_cb_t on_retry;
    lv_event_cb_t on_reinput;
} fail_dlg_ctx_t;

static void fail_on_close(lv_obj_t *card, void *ud)
{
    (void)card;
    fail_dlg_ctx_t *c = (fail_dlg_ctx_t *)ud;
    c->v->fail_card = NULL;
    lv_free(c);
}

static void on_fail_dismiss(lv_event_t *e)
{
    fail_dlg_ctx_t *c = (fail_dlg_ctx_t *)lv_event_get_user_data(e);
    WifiScanView_CloseFailDialog(c->v);
}

static void fail_build(lv_obj_t *card, void *ud)
{
    fail_dlg_ctx_t *c = (fail_dlg_ctx_t *)ud;

    ui_create_label(card, tr("连接失败"), UI_FONT_TITLE, UI_COLOR_TEXT);
    ui_create_label(card, c->ssid, NULL, UI_COLOR_TEXT);
    ui_create_label(card, c->reason_text, NULL, UI_COLOR_TEXT_SEC);

    char code[32];
    snprintf(code, sizeof(code), tr("原因码 %d"), c->reason);
    ui_create_label(card, code, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

    /* 主操作整行：重新输入密码（密码错误是最高频原因） */
    ui_create_btn(card, tr("重新输入密码"), 0, UI_BTN_H_SM, UI_BTN_FILLED,
                  c->on_reinput, NULL);
    /* 次操作行：取消 + 重试（同密码再连一次） */
    lv_obj_t *row = ui_create_row(card);
    ui_create_btn(row, tr("取消"), 0, UI_BTN_H_SM, UI_BTN_SUBTLE,
                  on_fail_dismiss, c);
    ui_create_btn(row, tr("重试"), 0, UI_BTN_H_SM, UI_BTN_OUTLINE,
                  c->on_retry, NULL);
}

void WifiScanView_ShowFailDialog(WifiScanView_t *v, const char *ssid,
                                 const char *reason_text, int reason,
                                 lv_event_cb_t on_retry, lv_event_cb_t on_reinput)
{
    if (!ssid || ssid[0] == '\0') return;

    WifiScanView_CloseFailDialog(v);   /* 单例 */

    fail_dlg_ctx_t *c = lv_malloc_zeroed(sizeof(fail_dlg_ctx_t));
    c->v = v;
    snprintf(c->ssid, sizeof(c->ssid), "%s", ssid);
    c->reason_text = reason_text ? reason_text : HAL_WiFi_ReasonText(0);
    c->reason = reason;
    c->on_retry = on_retry;
    c->on_reinput = on_reinput;

    ui_modal_builder_t b = {
        .build = fail_build,
        .on_close = fail_on_close,
        .user_data = c,
        .dim = LV_OPA_60,
    };
    v->fail_card = ui_modal_open(&b);
}

void WifiScanView_CloseFailDialog(WifiScanView_t *v)
{
    if (v->fail_card) {
        ui_modal_close(v->fail_card);   /* on_close 里清 fail_card */
    }
}
