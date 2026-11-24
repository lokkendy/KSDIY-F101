/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - WiFi 扫描页 Presenter 实现
 */
#include "WifiScan.h"
#include "WifiScanModel.h"
#include "WifiScanView.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"

#include "../Settings/Settings.h"
#include "i18n.h"
#include "data_bridge.h"
#include "ui_dispatch.h"
#include "ui_events.h"

static const char *TAG = "page_wifi_scan";

typedef struct {
    WifiScanView_t view;
    char selected_ssid[33];
    uint8_t selected_auth;
    char last_pass[65];       /* 失败重试用（重试同密码 / 回填编辑） */
    bool connecting;          /* 连接流程进行中：锁定列表点击与刷新 */
    char conn_text[56];       /* "正在连接 %s" 基底，省点动画只拼后缀 */
    lv_timer_t *dots_timer;
    uint8_t dots;
    lv_timer_t *pop_timer;    /* 连接成功后 800ms 自动返回，一次性 */
    volatile bool alive;
} wifi_scan_presenter_t;

static wifi_scan_presenter_t s_scan;

static bool wifi_evt_cb(uint8_t id, int16_t arg, void *user_data);
static void on_fail_retry(lv_event_t *e);
static void on_fail_reinput(lv_event_t *e);
static void on_connect_click(lv_event_t *e);

/* ── 连接中动画与状态 ── */

static void dots_stop(void)
{
    if (s_scan.dots_timer) {
        lv_timer_del(s_scan.dots_timer);
        s_scan.dots_timer = NULL;
    }
}

static void dots_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_scan.view.lbl_status || !s_scan.connecting) {
        return;
    }
    s_scan.dots = (s_scan.dots + 1) % 4;
    char buf[72];
    snprintf(buf, sizeof(buf), "%s%.*s", s_scan.conn_text, (int)s_scan.dots, "...");
    WifiScanView_SetStatus(&s_scan.view, buf);
}

/* ── 密码弹窗 ── */

static void on_cancel_pwd(lv_event_t *e)
{
    (void)e;
    WifiScanView_ClosePwdDialog(&s_scan.view);
}

static void begin_connect(const char *ssid, const char *pass)
{
    WifiScanView_ClosePwdDialog(&s_scan.view);
    WifiScanView_CloseFailDialog(&s_scan.view);

    strlcpy(s_scan.selected_ssid, ssid, sizeof(s_scan.selected_ssid));
    strlcpy(s_scan.last_pass, pass ? pass : "", sizeof(s_scan.last_pass));
    s_scan.connecting = true;
    s_scan.dots = 0;

    WifiScanModel_Connect(ssid, pass);
    settings_wifi_fill_fields(ssid, pass ? pass : "");

    snprintf(s_scan.conn_text, sizeof(s_scan.conn_text), tr("正在连接 %s"), ssid);
    WifiScanView_SetStatus(&s_scan.view, s_scan.conn_text);
    dots_stop();
    s_scan.dots_timer = lv_timer_create(dots_cb, 400, NULL);
    bridge_add_log(tr("[WiFi] 扫描连接: %s"), ssid);
}

/* 连接成功：一次性延时返回，让用户看清"已连接"再退页 */
static void pop_later_cb(lv_timer_t *t)
{
    s_scan.pop_timer = NULL;
    lv_timer_del(t);
    if (s_scan.alive) {
        pm_pop();
    }
}

/* ── 桥事件订阅：连接结果（页面在场时优先于 App.c 全局兜底） ── */

static bool wifi_evt_cb(uint8_t id, int16_t arg, void *user_data)
{
    (void)user_data;
    if (!s_scan.alive || !s_scan.connecting) {
        return false;   /* 非本页发起的连接交给全局兜底提示 */
    }

    if (id == WIFI_EVT_CONNECTED) {
        s_scan.connecting = false;
        dots_stop();
        WifiScanView_SetStatus(&s_scan.view, tr("WiFi 已连接"));
        ui_toast(tr("WiFi 已连接"), UI_TOAST_SUCCESS);
        if (!s_scan.pop_timer) {
            s_scan.pop_timer = lv_timer_create(pop_later_cb, 800, NULL);
            lv_timer_set_repeat_count(s_scan.pop_timer, 1);
        }
        return true;
    }
    if (id == WIFI_EVT_CONNECT_FAIL) {
        s_scan.connecting = false;
        dots_stop();
        WifiScanView_SetStatus(&s_scan.view, tr("连接失败，请重试"));
        WifiScanView_ShowFailDialog(&s_scan.view, s_scan.selected_ssid,
                                    HAL_WiFi_ReasonText(arg), arg,
                                    on_fail_retry, on_fail_reinput);
        return true;
    }
    return false;   /* LOST 等仍交全局兜底（页内无连接时不拦） */
}

/* ── 失败弹窗动作 ── */

static void on_fail_retry(lv_event_t *e)
{
    (void)e;
    begin_connect(s_scan.selected_ssid, s_scan.last_pass);
}

static void on_fail_reinput(lv_event_t *e)
{
    (void)e;
    WifiScanView_CloseFailDialog(&s_scan.view);
    WifiScanView_ShowPwdDialog(&s_scan.view, s_scan.selected_ssid,
                               s_scan.selected_auth, on_cancel_pwd, on_connect_click);
    /* 回填上次密码：改错字而非重敲整串（密码模式显示掩码） */
    if (s_scan.view.pwd_ta && s_scan.last_pass[0]) {
        lv_textarea_set_text(s_scan.view.pwd_ta, s_scan.last_pass);
    }
}

static void on_connect_click(lv_event_t *e)
{
    (void)e;
    /* 必须先拷出密码：关弹窗会删 textarea，其缓冲随遮罩释放，
     * 删后再读 = use-after-free，密码尾部混入堆残渣字节（渲染成删不掉
     * 的"O"，backspace 触发 lv_text_cut 越界崩溃），存进 NVS 后恶性循环 */
    char pass[65];
    if (s_scan.view.pwd_ta) {
        strlcpy(pass, lv_textarea_get_text(s_scan.view.pwd_ta), sizeof(pass));
    } else {
        pass[0] = '\0';
    }
    if (s_scan.selected_auth != WIFI_AUTH_OPEN && pass[0] == '\0') {
        ui_toast(tr("请输入 WiFi 密码"), UI_TOAST_ERROR);
        return;
    }
    begin_connect(s_scan.selected_ssid, pass);
}

static void on_ap_click(lv_event_t *e)
{
    if (s_scan.connecting) {
        ui_toast(tr("设备忙，请稍候"), UI_TOAST_INFO);
        return;
    }
    /* current_target = 注册回调的按钮；target 可能是内部 label */
    lv_obj_t *btn = lv_event_get_current_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    const wifi_ap_t *ap = WifiScanModel_GetAp((uint16_t)idx);
    if (!ap) {
        return;
    }
    strlcpy(s_scan.selected_ssid, ap->ssid, sizeof(s_scan.selected_ssid));
    s_scan.selected_auth = ap->authmode;
    WifiScanView_ShowPwdDialog(&s_scan.view, ap->ssid, ap->authmode,
                               on_cancel_pwd, on_connect_click);
}

/* ── 扫描 ── */

static void on_scan_done(uint16_t count, esp_err_t err, void *ctx);

static void start_scan(void)
{
    if (WifiScanModel_IsScanning()) {
        WifiScanView_SetStatus(&s_scan.view, tr("正在扫描..."));
        return;
    }
    WifiScanView_SetStatus(&s_scan.view, tr("正在扫描..."));
    WifiScanView_ShowScanning(&s_scan.view);

    esp_err_t ret = WifiScanModel_StartScan(on_scan_done, NULL);
    if (ret != ESP_OK) {
        WifiScanView_SetStatus(&s_scan.view, tr("无法启动扫描"));
        ui_toast(tr("扫描忙或未就绪"), UI_TOAST_ERROR);
    }
}

static void on_refresh(lv_event_t *e)
{
    (void)e;
    if (s_scan.connecting) {
        ui_toast(tr("设备忙，请稍候"), UI_TOAST_INFO);
        return;
    }
    WifiScanView_ClosePwdDialog(&s_scan.view);
    start_scan();
}

/* 扫描结果（WiFi 任务上下文）→ ui_dispatch 编组到 LVGL 任务刷新。
 * 单槽回执（同屏只有一次扫描在途）+ destroy 时 ui_post_cancel，
 * 页面销毁后回调不再执行 —— 替代旧手写 lock(5000)+alive 守卫。 */
static struct {
    uint16_t count;
    esp_err_t err;
    bool posted;
} s_scan_rst;

static void scan_done_ui(void *arg)
{
    (void)arg;
    s_scan_rst.posted = false;
    if (!s_scan.alive || s_scan.connecting) {
        return;
    }
    if (s_scan_rst.err != ESP_OK) {
        WifiScanView_SetStatus(&s_scan.view, tr("扫描失败，请稍后重试"));
        ui_toast(tr("扫描失败，请稍后重试"), UI_TOAST_ERROR);
        ESP_LOGW(TAG, "scan UI err: %s", esp_err_to_name(s_scan_rst.err));
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), tr("发现 %u 个热点"), (unsigned)s_scan_rst.count);
        WifiScanView_SetStatus(&s_scan.view, buf);
        WifiScanView_ShowApList(&s_scan.view, WifiScanModel_GetApList(),
                                WifiScanModel_GetApCount(), s_scan.selected_ssid,
                                on_ap_click);
    }
}

static void on_scan_done(uint16_t count, esp_err_t err, void *ctx)
{
    (void)ctx;
    s_scan_rst.count = count;
    s_scan_rst.err = err;
    s_scan_rst.posted = true;
    ui_post(scan_done_ui, NULL);
}

/* ── 页面生命周期 ── */

static lv_obj_t *wifi_scan_page_create(lv_obj_t *parent, void *user_data)
{
    (void)user_data;
    memset(&s_scan, 0, sizeof(s_scan));
    memset(&s_scan_rst, 0, sizeof(s_scan_rst));
    s_scan.alive = true;

    WifiScanView_Create(&s_scan.view, parent, on_refresh, pm_back_cb);
    start_scan();

    ESP_LOGI(TAG, "WiFi scan page ready");
    return s_scan.view.root;
}

static void wifi_scan_page_destroy(void *user_data)
{
    (void)user_data;
    s_scan.alive = false;
    /* 在途扫描结果作废：销毁后回调不再执行（单槽回执一并撤） */
    if (s_scan_rst.posted) {
        ui_post_cancel(scan_done_ui, NULL);
        s_scan_rst.posted = false;
    }
    ui_events_unsubscribe(wifi_evt_cb);
    dots_stop();
    if (s_scan.pop_timer) {
        lv_timer_del(s_scan.pop_timer);
        s_scan.pop_timer = NULL;
    }
    WifiScanView_ClosePwdDialog(&s_scan.view);
    WifiScanView_CloseFailDialog(&s_scan.view);
    memset(&s_scan, 0, sizeof(s_scan));
}

/* 被覆盖/弹出 → 退订连接事件（结果交 App.c 全局兜底提示）+ 停省点动画 */
static void wifi_scan_on_pause(void *user_data)
{
    (void)user_data;
    ui_events_unsubscribe(wifi_evt_cb);
    dots_stop();
}

/* 回到顶层 → 重挂事件订阅；连接仍在进行则恢复省点动画 */
static void wifi_scan_on_resume(void *user_data)
{
    (void)user_data;
    ui_events_subscribe(wifi_evt_cb, NULL);
    if (s_scan.connecting && !s_scan.dots_timer) {
        s_scan.dots_timer = lv_timer_create(dots_cb, 400, NULL);
    }
}

void wifi_scan_page_open(void)
{
    pm_push(&page_wifi_scan);
}

const page_t page_wifi_scan = {
    .name = "WiFi扫描",
    .create = wifi_scan_page_create,
    .destroy = wifi_scan_page_destroy,
    /* 事件订阅随顶层状态挂/撤：离开页面时连接结果自动交全局兜底，
     * 消灭跨线程 alive 守卫的存在必要 */
    .on_pause = wifi_scan_on_pause,
    .on_resume = wifi_scan_on_resume,
    .user_data = NULL,
};
