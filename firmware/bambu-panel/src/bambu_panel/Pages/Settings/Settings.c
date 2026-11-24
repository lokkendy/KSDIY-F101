/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 系统设置页 Presenter 实现
 */
#include "Settings.h"
#include "SettingsModel.h"
#include "SettingsView.h"
#include "ui_power_menu.h"

#include <stdio.h>
#include <string.h>

#include "../WifiScan/WifiScan.h"
#include "i18n.h"
#include "ui_keyboard.h"
#include "data_bridge.h"
#include "voice.h"

typedef struct {
    SettingsView_t view;
    lv_timer_t *timer;
} settings_presenter_t;

static settings_presenter_t s_settings;
static uint8_t s_pending_rotation;

void settings_wifi_fill_fields(const char *ssid, const char *pass)
{
    if (s_settings.view.txt_ssid && ssid) {
        lv_textarea_set_text(s_settings.view.txt_ssid, ssid);
    }
    if (s_settings.view.txt_pass && pass) {
        lv_textarea_set_text(s_settings.view.txt_pass, pass);
    }
}

/* ── WiFi ── */

static void on_wifi_connect(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(s_settings.view.txt_ssid);
    const char *pass = lv_textarea_get_text(s_settings.view.txt_pass);

    if (!ssid || strlen(ssid) == 0) {
        ui_toast(tr("请输入 WiFi 名称"), UI_TOAST_ERROR);
        return;
    }

    SettingsModel_SaveWifiCreds(ssid, pass);
    SettingsModel_ConnectWifiAsync(ssid, pass);
    ui_toast(tr("正在连接 WiFi..."), UI_TOAST_INFO);
    bridge_add_log(tr("[WiFi] 连接中: %s"), ssid);
}

static void on_wifi_disconnect(lv_event_t *e)
{
    (void)e;
    SettingsModel_DisconnectWifi();
    SettingsModel_ClearWifiCreds();
    lv_textarea_set_text(s_settings.view.txt_pass, "");
    ui_toast(tr("已断开并清除保存的 WiFi"), UI_TOAST_INFO);
}

static void on_wifi_scan(lv_event_t *e)
{
    (void)e;
    wifi_scan_page_open();
}

/* ── 系统 ── */

static void do_reboot(void *ctx)
{
    (void)ctx;
    SettingsModel_Reboot();
}

static void on_reboot(lv_event_t *e)
{
    (void)e;
    ui_confirm_dialog(tr("重启设备"), tr("确定要重启设备吗？"),
                      tr("重启"), do_reboot, NULL);
}

static void do_shutdown(void *ctx)
{
    (void)ctx;
    ui_power_shutdown();
}

static void on_shutdown(lv_event_t *e)
{
    (void)e;
    ui_confirm_dialog(tr("关机"),
                      tr("确定要关机吗？关机后短按电源键开机。"),
                      tr("关机"), do_shutdown, NULL);
}

/* 亮度滑条去抖：VALUE_CHANGED 只做背光预览（不写 NVS），
 * RELEASED 才落盘 —— 拖一次滑条从 ~30 次 5 键 NVS 写降为 1 次 */
static void on_brightness(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    uint8_t val = (uint8_t)lv_slider_get_value(slider);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        SettingsModel_SetBrightness(val);
    } else {
        SettingsModel_ApplyBrightness(val);
    }
}

/* 音量滑条同款去抖：VALUE_CHANGED 只调 DAC 预览，RELEASED 才落盘 */
static void on_volume(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    uint8_t val = (uint8_t)lv_slider_get_value(slider);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        SettingsModel_SetVolume(val);
    } else {
        SettingsModel_ApplyVolume(val);
    }
}

static void on_voice(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    SettingsModel_SetVoice(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void on_touch_sound(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    SettingsModel_SetTouchSound(on);
    if (on) {
        voice_touch_click();
    }
}

/* ── 语言切换：落盘 + 切全局语言 + 原地重译
 * （静态文案走 tr_bind 注册表，动态文案走 page_t.on_language 钩子；
 *  不销毁任何对象 → 事件回调链内直调安全，滚动/输入/导航栈全保留） ── */

static void settings_refresh(void);

static void on_language(lv_event_t *e)
{
    /* 胶囊二选一：user_data=选项序号（intptr） */
    uint8_t sel = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (sel == SettingsModel_GetLanguage()) return;

    SettingsModel_SetLanguage(sel);
    i18n_set_lang((app_lang_t)sel);
    pm_retranslate();
}

/* 动态文案重算（on_language 钩子，pm_retranslate 自动回调）：
 * WiFi 状态快照 + 输入框占位符 + 方向下拉选项（非 label 控件不进绑定表） */
static void settings_on_language(void *user_data)
{
    (void)user_data;
    SettingsView_t *v = &s_settings.view;

    settings_refresh();
    lv_textarea_set_placeholder_text(v->txt_ssid, tr("WiFi 名称 (SSID)"));
    lv_textarea_set_placeholder_text(v->txt_pass, tr("WiFi 密码"));

    uint8_t rot = SettingsModel_GetRotation();
    lv_dropdown_set_options(v->dd_rot, tr("竖屏\n横屏 90°\n竖屏 180°\n横屏 270°"));
    lv_dropdown_set_selected(v->dd_rot, rot <= 3 ? rot : 0);
}

/* ── 配色切换：先落盘 + 换活动调色板，再延迟一拍重建外壳
 * （状态栏/Tab 栏配色也要跟着变，走 pm_reload_shell 而非页面级 reload） ── */

static void theme_reload_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    pm_reload_shell();
}

static void on_theme(lv_event_t *e)
{
    /* 胶囊二选一：user_data=选项序号（intptr） */
    uint8_t sel = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (sel == SettingsModel_GetTheme()) return;

    SettingsModel_SetTheme(sel);
    ui_theme_set_mode((ui_theme_mode_t)sel);
    /* 略延迟：当前点击/主题 pill 事件链结束后再重建，避免与 LVGL 内部重入 */
    lv_timer_t *t = lv_timer_create(theme_reload_cb, 32, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void rotation_reboot_ok(void *ctx)
{
    (void)ctx;
    SettingsModel_SetRotation(s_pending_rotation);
    if(SettingsModel_GetRotation()!=s_pending_rotation) {
        ui_toast(tr("屏幕方向保存失败，请重试"),UI_TOAST_ERROR);
        return;
    }
    SettingsModel_Reboot();
}

/* 延迟一拍开确认弹窗：下拉 VALUE_CHANGED 事件链内直接建 modal 会被
 * 同一条释放事件链的残留 CLICKED 误触其按钮（sim 实测弹窗一开即被
 * 自动"确认"触发 esp_restart；真机则表现为弹窗冻死点不动）——
 * 仿 on_language/on_theme 先例，等事件链完全散去再开。 */
static void rotation_confirm_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    ui_confirm_dialog(tr("屏幕方向"),
                      tr("切换横/竖屏需重启后生效（显示与触摸同步旋转），是否立即重启？"),
                      tr("重启"), rotation_reboot_ok, NULL);
}

static void on_rotation(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint8_t sel = (uint8_t)lv_dropdown_get_selected(dd);
    if (sel == SettingsModel_GetRotation()) {
        return;
    }

    s_pending_rotation = sel;
    lv_dropdown_set_selected(dd, SettingsModel_GetRotation());
    lv_timer_t *t = lv_timer_create(rotation_confirm_cb, 1, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/* ── 状态刷新 ── */

static void settings_refresh(void)
{
    SettingsView_t *v = &s_settings.view;
    settings_net_snapshot_t snap;
    SettingsModel_GetNetSnapshot(&snap);

    if (snap.wifi_connected) {
        lv_label_set_text(v->lbl_wifi_state, snap.wifi_ssid);
        lv_obj_set_style_text_color(v->lbl_wifi_state, UI_COLOR_SUCCESS, 0);
        lv_label_set_text(v->lbl_wifi_ip, snap.wifi_ip);
    } else {
        lv_label_set_text(v->lbl_wifi_state, tr("未连接"));
        lv_obj_set_style_text_color(v->lbl_wifi_state, UI_COLOR_TEXT_SEC, 0);
        lv_label_set_text(v->lbl_wifi_ip, "");
    }

    lv_label_set_text(v->lbl_devid, snap.device_id);
}

static void settings_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    settings_refresh();
}

/* ── 页面生命周期 ── */

static lv_obj_t *settings_page_create(lv_obj_t *parent, void *user_data)
{
    (void)user_data;
    memset(&s_settings, 0, sizeof(s_settings));

    char saved_ssid[33] = "";
    char saved_pass[65] = "";
    SettingsModel_LoadWifiCreds(saved_ssid, sizeof(saved_ssid), saved_pass, sizeof(saved_pass));

    SettingsView_Create(&s_settings.view, parent, saved_ssid, saved_pass,
                        SettingsModel_GetBrightness(), SettingsModel_GetLanguage(),
                        SettingsModel_GetTheme(), SettingsModel_GetRotation(),
                        SettingsModel_GetVoice(), SettingsModel_GetTouchSound(),
                        SettingsModel_GetVolume(),
                        on_wifi_scan, on_wifi_connect, on_wifi_disconnect,
                        on_brightness, on_language, on_theme, on_rotation,
                        on_voice, on_touch_sound, on_volume, on_reboot, on_shutdown);

    s_settings.timer = lv_timer_create(settings_timer_cb, 1000, NULL);
    settings_refresh();
    return s_settings.view.root;
}

static void settings_page_destroy(void *user_data)
{
    (void)user_data;
    if (s_settings.timer) {
        lv_timer_delete(s_settings.timer);
        s_settings.timer = NULL;
    }
    /* 键盘挂在顶层图层，不随页面销毁，需显式收起 */
    ui_keyboard_hide();
}

/* 被覆盖/转场开始 → 暂停刷新 + 收键盘（KEEP_ALIVE 弹出不走 destroy） */
static void settings_on_pause(void *user_data)
{
    (void)user_data;
    if (s_settings.timer) {
        lv_timer_pause(s_settings.timer);
    }
    ui_keyboard_hide();
}

/* 回到顶层且转场完成 → 恢复刷新并立即刷一次 */
static void settings_on_resume(void *user_data)
{
    (void)user_data;
    if (s_settings.timer) {
        lv_timer_resume(s_settings.timer);
    }
    settings_refresh();
}

const page_t page_settings = {
    .name = "设置",
    .create = settings_page_create,
    .destroy = settings_page_destroy,
    .on_pause = settings_on_pause,
    .on_resume = settings_on_resume,
    .on_language = settings_on_language,
    /* 长表单：弹出/切 Tab 进缓存，返回滚动位置与输入原样保留 */
    .cache = PM_CACHE_KEEP_ALIVE,
    .user_data = NULL,
};
