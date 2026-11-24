/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 数据桥实现
 */
#include "data_bridge.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static app_bridge_t s_bridge;
static SemaphoreHandle_t s_lock;

/* ──────────────────── 分组版本号（bridge_watch 变更检测） ────────────────────
 * setter 在锁内自增；LVGL 泵无锁读取（对齐 uint32 读原子）比较后分发。
 * 索引与 BRIDGE_GRP_* 位一一对应（bit n → s_ver[n]）。 */
#define BRIDGE_GRP_NUM 6
static volatile uint32_t s_ver[BRIDGE_GRP_NUM];

/* ──────────────────── 桥锁(导出给应用扩展数据共用) ──────────────────── */

void bridge_lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

void bridge_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

/* ──────────────────── 事件小环(容量 8, 满则丢最旧) ──────────────────── */

static bridge_evt_t s_evt_q[8];
static uint8_t s_evt_rd, s_evt_wr;

void bridge_post_event(uint8_t id, int16_t arg)
{
    if (!id || !s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t next = (s_evt_wr + 1) & 7;
    if (next == s_evt_rd) {
        s_evt_rd = (s_evt_rd + 1) & 7;    /* 丢最旧 —— 事件是提示性质, 允许丢 */
    }
    s_evt_q[s_evt_wr].id = id;
    s_evt_q[s_evt_wr].arg = arg;
    s_evt_wr = next;
    xSemaphoreGive(s_lock);
}

void bridge_purge_event(uint8_t id)
{
    if (!id || !s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t rd = s_evt_rd;
    uint8_t wr = s_evt_wr;
    bridge_evt_t out[8];
    uint8_t n = 0;
    while (rd != wr) {
        bridge_evt_t e = s_evt_q[rd];
        rd = (rd + 1) & 7;
        if (e.id != id && n < sizeof(out)) {
            out[n++] = e;
        }
    }
    s_evt_rd = 0;
    s_evt_wr = 0;
    for (uint8_t i = 0; i < n; i++) {
        s_evt_q[s_evt_wr] = out[i];
        s_evt_wr = (s_evt_wr + 1) & 7;
    }
    xSemaphoreGive(s_lock);
}

bridge_evt_t bridge_poll_event(void)
{
    if (!s_lock) {
        bridge_evt_t none = { EVT_NONE, 0 };
        return none;
    }
    bridge_evt_t evt = { EVT_NONE, 0 };
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_evt_rd != s_evt_wr) {
        evt = s_evt_q[s_evt_rd];
        s_evt_rd = (s_evt_rd + 1) & 7;
    }
    xSemaphoreGive(s_lock);
    return evt;
}

/* ──────────────────── 运行日志环 ──────────────────── */

static char s_log[BRIDGE_LOG_LINES][BRIDGE_LOG_LINE_LEN];
static size_t s_log_wr = 0;    /* 下一写入行 */
static size_t s_log_count = 0; /* 有效行数(<= BRIDGE_LOG_LINES) */

void bridge_add_log(const char *fmt, ...)
{
    if (!s_lock) {
        return;
    }
    char line[BRIDGE_LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_log[s_log_wr], line, BRIDGE_LOG_LINE_LEN);
    s_log[s_log_wr][BRIDGE_LOG_LINE_LEN - 1] = '\0';
    s_log_wr = (s_log_wr + 1) % BRIDGE_LOG_LINES;
    if (s_log_count < BRIDGE_LOG_LINES) {
        s_log_count++;
    }
    xSemaphoreGive(s_lock);
}

size_t bridge_log_count(void)
{
    if (!s_lock) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_log_count;
    xSemaphoreGive(s_lock);
    return n;
}

size_t bridge_log_read(char (*out)[BRIDGE_LOG_LINE_LEN], size_t max_lines)
{
    if (!out || !max_lines || !s_lock) {
        return 0;
    }
    size_t copied = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* 最早的一行: 环未满时为 0, 满后为 s_log_wr */
    size_t start = (s_log_count < BRIDGE_LOG_LINES) ? 0 : s_log_wr;
    size_t want = s_log_count < max_lines ? s_log_count : max_lines;
    for (size_t i = 0; i < want; i++) {
        memcpy(out[i], s_log[(start + i) % BRIDGE_LOG_LINES], BRIDGE_LOG_LINE_LEN);
        out[i][BRIDGE_LOG_LINE_LEN - 1] = '\0';
        copied++;
    }
    xSemaphoreGive(s_lock);
    return copied;
}

void bridge_log_clear(void)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_log_wr = 0;
    s_log_count = 0;
    xSemaphoreGive(s_lock);
}

/* ──────────────────── 初始化 + 快照 + 分组 setter ──────────────────── */

void bridge_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    memset(&s_bridge, 0, sizeof(s_bridge));
    s_evt_rd = s_evt_wr = 0;
    s_log_wr = 0;
    s_log_count = 0;
    s_bridge.batt_pct = -1;
}

void bridge_get(app_bridge_t *out)
{
    if (!out || !s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_bridge;
    xSemaphoreGive(s_lock);
}

void bridge_set_device_id(const char *device_id)
{
    if (!s_lock || !device_id) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_bridge.device_id, device_id, sizeof(s_bridge.device_id));
    s_ver[5]++;   /* BRIDGE_GRP_DEV */
    xSemaphoreGive(s_lock);
}

void bridge_set_network(bool connected, int8_t rssi, const char *ip, const char *ssid)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_bridge.wifi_connected = connected;
    s_bridge.rssi = rssi;
    if (ip) {
        strlcpy(s_bridge.ip_str, ip, sizeof(s_bridge.ip_str));
    }
    if (ssid) {
        strlcpy(s_bridge.ssid, ssid, sizeof(s_bridge.ssid));
    }
    s_ver[0]++;   /* BRIDGE_GRP_WIFI */
    xSemaphoreGive(s_lock);
}

void bridge_set_batt(int8_t pct, bool charging)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_bridge.batt_pct = pct;
    s_bridge.batt_charging = charging;
    s_ver[1]++;   /* BRIDGE_GRP_BATT */
    xSemaphoreGive(s_lock);
}

void bridge_set_time_synced(bool synced)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_bridge.time_synced = synced;
    s_ver[2]++;   /* BRIDGE_GRP_TIME */
    xSemaphoreGive(s_lock);
}

void bridge_set_host_online(bool online)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_bridge.host_online = online;
    s_ver[3]++;   /* BRIDGE_GRP_HOST */
    xSemaphoreGive(s_lock);
}

void bridge_set_system(uint32_t heap_free, uint32_t heap_min, uint32_t uptime_s)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_bridge.heap_free = heap_free;
    s_bridge.heap_min = heap_min;
    s_bridge.uptime_s = uptime_s;
    s_ver[4]++;   /* BRIDGE_GRP_SYS */
    xSemaphoreGive(s_lock);
}

/* ──────────────────── 变更订阅（bridge_watch） ──────────────────── */

typedef struct {
    bridge_watch_cb_t cb;
    void *ud;
    uint8_t mask;
    uint32_t min_interval_ms;
    uint32_t last_ms;      /* 上次分发时刻（ms）*/
    uint32_t seen[BRIDGE_GRP_NUM];   /* 上次分发时的各组版本号 */
    bool pending;          /* 节流挂起：有变更未送达 */
    bool active;
} bridge_watch_t;

static bridge_watch_t s_watch[BRIDGE_MAX_WATCH];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bridge_watch_h_t bridge_watch(uint8_t mask, bridge_watch_cb_t cb,
                              void *user_data, uint32_t min_interval_ms)
{
    if (!cb || !mask || !s_lock) {
        return 0;
    }
    for (int i = 0; i < BRIDGE_MAX_WATCH; i++) {
        if (!s_watch[i].active) {
            bridge_watch_t *w = &s_watch[i];
            w->cb = cb;
            w->ud = user_data;
            w->mask = mask;
            w->min_interval_ms = min_interval_ms;
            w->last_ms = now_ms();
            w->pending = false;
            w->active = true;
            for (int g = 0; g < BRIDGE_GRP_NUM; g++) {
                w->seen[g] = s_ver[g];   /* 以当前版本为基线 */
            }
            cb(user_data);               /* 订阅即回放：新订阅者立即绘制当前状态 */
            return (bridge_watch_h_t)(i + 1);
        }
    }
    return 0;
}

void bridge_unwatch(bridge_watch_h_t h)
{
    if (h == 0 || h > BRIDGE_MAX_WATCH) {
        return;
    }
    s_watch[h - 1].active = false;
}

size_t bridge_watch_count(void)
{
    size_t n = 0;
    for (int i = 0; i < BRIDGE_MAX_WATCH; i++) {
        if (s_watch[i].active) {
            n++;
        }
    }
    return n;
}

void bridge_poll_watches(void)
{
    if (!s_lock) {
        return;
    }
    uint32_t cur[BRIDGE_GRP_NUM];
    for (int g = 0; g < BRIDGE_GRP_NUM; g++) {
        cur[g] = s_ver[g];
    }
    uint32_t now = now_ms();

    for (int i = 0; i < BRIDGE_MAX_WATCH; i++) {
        bridge_watch_t *w = &s_watch[i];
        if (!w->active) {
            continue;
        }
        bool changed = w->pending;
        if (!changed) {
            for (int g = 0; g < BRIDGE_GRP_NUM; g++) {
                if ((w->mask >> g) & 1 && w->seen[g] != cur[g]) {
                    changed = true;
                    break;
                }
            }
        }
        if (!changed) {
            continue;
        }
        /* 节流：到点前挂 pending，终值不丢（下拍补发）*/
        if (w->min_interval_ms && now - w->last_ms < w->min_interval_ms) {
            w->pending = true;
            continue;
        }
        for (int g = 0; g < BRIDGE_GRP_NUM; g++) {
            w->seen[g] = cur[g];
        }
        w->pending = false;
        w->last_ms = now;
        if (w->active) {              /* 回调内可能退订自身 */
            w->cb(w->ud);
        }
    }
}
