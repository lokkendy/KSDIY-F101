/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 应用装配层实现
 */
#include "App.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "HAL.h"

#include "app_i18n.h"
#include "app_shell_cfg.h"
#include "app_voice.h"
#include "boot_avi.h"
#include "data_bridge.h"
#include "i18n.h"
#include "page_manager.h"
#include "app_settings.h"
#include "ui_events.h"
#include "ui_power_menu.h"
#include "ui_theme.h"
#include "ui_touch_feedback.h"
#include "voice.h"
#include "bambu_profile.h"
#include "bambu_cloud.h"
#include "bambu_service.h"
#include "ui_bambu.h"
#include "bambu_history.h"
#include "ui_bambu_command.h"

#include "ksdiy_lvgl_port.h"

static const char *TAG = "app";

/** 全局 1s：刷新当前 Tab 打印机 UI（Tab 切走后各页 timer 会 pause/destroy） */
static void app_printer_ui_tick(lv_timer_t *t)
{
    (void)t;
    ui_bambu_commands_poll();
    pm_refresh_printer_ui();
}

/* ──────────────────── 电源键长按（AXP2101 PEK → 电源菜单） ──────────────────── */

/* HAL_Power 事件任务上下文：经桥事件泵转入 LVGL 上下文再开菜单（WIFI_EVT 同款） */
static void app_pwr_key_cb(hal_pwr_key_evt_t evt)
{
    if (evt == HAL_PWR_KEY_LONG) {
        bridge_post_event(PWR_EVT_KEY_LONG, 0);
    }
}

/* ui_events 泵回调（LVGL 上下文）：弹关机/重启/取消三钮菜单 */
static bool app_pwr_evt_cb(uint8_t id, int16_t arg, void *user_data)
{
    (void)arg;
    (void)user_data;
    if (id != PWR_EVT_KEY_LONG) {
        return false;
    }
    ui_power_menu_open();
    return true;
}

/* ──────────────────── 桥接状态刷新任务 ──────────────────── */

/**
 * @brief 后台任务：周期读电源/内存水位写入 data_bridge
 *
 * 页面定时器从 bridge 读快照刷新 UI，UI 与服务完全解耦。
 */
static void bridge_updater_task(void *arg)
{
    (void)arg;
    hal_power_info_t pwr;

    /* AXP2101 挂在显示创建的触摸 I2C 总线上，须在显示初始化后使能测量通道 */
    HAL_Power_Init();
    HAL_Power_SetKeyCb(app_pwr_key_cb);   /* PEK 长按 → PWR_EVT_KEY_LONG */

    uint32_t uptime = 0;
    while (1) {
        /* 电池（状态栏显示；无电池报 -1 = "--"） */
        if (HAL_Power_Read(&pwr) == ESP_OK) {
            bridge_set_batt(pwr.pct,
                            pwr.status == HAL_BATT_CHARGING ||
                            pwr.status == HAL_BATT_CHARGED);
        }

        bambu_svc_snapshot_t printer;
        bambu_service_snapshot(&printer);
        bambu_history_observe(&printer,(uint32_t)time(NULL));

        /* 内存水位 */
        bridge_set_system(heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                          uptime);

        uptime += 2;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ──────────────────── 网络任务（后台） ──────────────────── */

/* Network time is started by the native WiFi adapter after DHCP. */
static void time_sync_start(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
}

/**
 * @brief WiFi 联网 + SNTP，全部在独立任务中完成（连接为长耗时阻塞）
 */
static void network_task(void *arg)
{
    (void)arg;

    HAL_WiFi_Init();

    /* 有保存凭据则自动重连 */
    char ssid[33] = { 0 };
    char pass[65] = { 0 };
    if (HAL_WiFi_LoadCreds(ssid, sizeof(ssid), pass, sizeof(pass))) {
        bridge_add_log(tr("[WiFi] 发现已保存网络: %s，自动连接"), ssid);
        HAL_WiFi_ConnectAsync(ssid, pass);
        memset(pass,0,sizeof(pass));
    }

    /* SNTP 内部重试直到拿到时间（未联网时静默等待） */
    time_sync_start();

    /* 已保存的拓竹 LAN 配置：等同网段 STA 拿到 IP 后再连 MQTT（避免 Host unreachable） */
    if (ssid[0]) {
        for (int i = 0; i < 180 && !HAL_WiFi_IsConnected(); i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    if (bambu_profile_start_saved() == ESP_OK) {
        bridge_add_log(tr("[Bambu] 正在连接已保存的打印机…"));
    }

    ESP_LOGI(TAG, "=== 网络任务就绪 ===");
    vTaskDelete(NULL);
}

/* ──────────────────── 按键演示回调（HAL_Button 任务上下文） ──────────────────── */

static void app_button_cb(hal_btn_id_t id, hal_btn_evt_t evt)
{
    static const char *names[] = { "KEY1", "KEY2" };
    static const char *evts[] = { "按下", "松开", "长按" };
    if (id < HAL_BTN_ID_MAX && evt < HAL_BTN_EVT_MAX) {
        bridge_add_log(tr("[KEY] %s %s"), names[id], tr(evts[evt]));
    }

    /* KEY1 松开 = 统一返回链：键盘 → Modal → on_back → pop → 根页忽略
     * （DESIGN §8；按键任务上下文，操作 LVGL 须持锁） */
    if (id == HAL_BTN_KEY1 && evt == HAL_BTN_EVT_UP) {
        ksdiy_lvgl_lock(-1);
        pm_back();
        ksdiy_lvgl_unlock();
    }
}

/* ──────────────────── WiFi 事件全局兜底（ui_events 泵分发） ──────────────────── */

/**
 * @brief WiFi 连接结果全局提示：页面级订阅者（如 WifiScan）优先消费，
 *        未被消费时在此兜底 toast + 语音，保证"任何页面都能感知联网结果"。
 */
static bool app_wifi_evt_cb(uint8_t id, int16_t arg, void *user_data)
{
    (void)user_data;
    char buf[96];

    switch (id) {
    case WIFI_EVT_CONNECTED:
        snprintf(buf, sizeof(buf), "%s", tr("WiFi 已连接"));
        ui_toast(buf, UI_TOAST_SUCCESS);
        bridge_add_log("%s", buf);
        voice_play(APP_VOICE_WIFI_OK);
        return true;
    case WIFI_EVT_LOST:
        snprintf(buf, sizeof(buf), "%s (reason %d)", tr("WiFi 已断开"), arg);
        ui_toast(buf, UI_TOAST_ERROR);
        bridge_add_log("%s", buf);
        voice_play(APP_VOICE_WIFI_FAIL);
        return true;
    case WIFI_EVT_CONNECT_FAIL:
        snprintf(buf, sizeof(buf), "%s (%d)", HAL_WiFi_ReasonText(arg), arg);
        ui_toast(buf, UI_TOAST_ERROR);
        bridge_add_log("%s", buf);
        voice_play(APP_VOICE_WIFI_FAIL);
        return true;
    default:
        return false;
    }
}

/* ──────────────────── 应用初始化 ──────────────────── */

void App_Init(void)
{
    char device_id[32];

    /* ---- Phase 0: 用户配置 + 语言/配色（UI 创建前生效） ---- */
    ESP_ERROR_CHECK(settings_init());
    i18n_register(g_app_i18n_entries, g_app_i18n_count);
    i18n_set_lang((app_lang_t)settings_get()->language);
    ui_theme_set_mode((ui_theme_mode_t)settings_get()->theme);
    ui_theme_set_brand(lv_color_hex(0x00E676), lv_color_hex(0x00E676));

    ESP_LOGI(TAG, "=== %s v%s ===", APP_NAME, APP_VERSION_STR);
    ESP_LOGI(TAG, "编译时间 %s %s", __DATE__, __TIME__);

    /* ---- Phase 1: 状态中枢 ---- */
    bridge_init();

    ESP_ERROR_CHECK(HAL_System_GetDeviceId(device_id, sizeof(device_id)));
    bridge_set_device_id(device_id);
    ESP_LOGI(TAG, "device_id=%s", device_id);
    bridge_add_log(tr("[SYS] 设备启动: %s"), device_id);

    /* ---- Phase 2: 字体预加载（异步）→ 显示 → 开机动画 ---- */
    /* FreeType 字体加载耗数百 ms，异步预载避免首帧白屏 */
    ui_theme_font_preload_async();

    ESP_ERROR_CHECK(HAL_Display_Init());
    HAL_Display_SetBrightness(settings_get()->brightness);
    bridge_add_log(tr("[SYS] 显示初始化完成"));

#if CONFIG_APP_BOOT_AVI_ENABLE
    {
        /* 开机动画：显示就绪后、主界面前全屏播一次；文件缺失/失败均跳过不阻塞 */
        esp_err_t boot_err = boot_avi_play(CONFIG_APP_BOOT_AVI_PATH);
        if (boot_err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "boot avi missing: %s (skip)", CONFIG_APP_BOOT_AVI_PATH);
        } else if (boot_err != ESP_OK) {
            ESP_LOGW(TAG, "boot avi failed: %s", esp_err_to_name(boot_err));
        }
    }
#endif

    /* 语音链路复用显示创建的 I2C 总线（须在显示初始化之后） */
    voice_init(g_app_voice_zh, g_app_voice_en, APP_VOICE_ID_COUNT);

    /* ---- Phase 3: UI 外壳（状态栏 + Tab + 首页） ---- */
    ksdiy_lvgl_lock(-1);
    ui_theme_init();
    pm_shell_init(lv_scr_act(), &g_app_shell_cfg);
    ui_touch_feedback_init();
    ui_events_init();               /* 桥事件泵（先于页面订阅，作全局兜底层） */
    ui_events_subscribe(app_wifi_evt_cb, NULL);
    ui_events_subscribe(app_pwr_evt_cb, NULL);
    /* Bambu 命令应答全局兜底（拒绝时任何页面都能看到） */
    bambu_service_init();
    bambu_cloud_start();
    bambu_history_init();
    lv_timer_create(app_printer_ui_tick, 1000, NULL);
    ksdiy_lvgl_unlock();
    ESP_LOGI(TAG, "UI 外壳已创建（首页/设置/关于）");
    bridge_add_log(tr("[SYS] UI 就绪"));

    /* ---- Phase 4: SD 卡（无卡不阻塞） ---- */
    HAL_SD_Init();
    if (HAL_SD_IsMounted()) {
        bridge_add_log(tr("[SD] SD 卡已挂载: /sdcard"));
    } else {
        bridge_add_log(tr("[SD] SD 卡未检测到"));
    }

    /* ---- Phase 4.5: 按键 + IMU（无器件不阻塞） ---- */
    HAL_Button_Init(app_button_cb);
    if (HAL_IMU_Init() == ESP_OK) {
        bridge_add_log(tr("[IMU] LSM6DS3TR-C 就绪"));
    }

    /* ---- Phase 5: 后台任务 ---- */
    xTaskCreate(bridge_updater_task, "bridge_upd", 4096, NULL, 3, NULL);
    xTaskCreate(network_task, "network", 6144, NULL, 4, NULL);

    ESP_LOGI(TAG, "=== 系统启动完成 ===");
    bridge_add_log(tr("[SYS] 系统启动完成"));
    voice_play(APP_VOICE_READY);
}
