/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 数据桥(跨线程状态交换: mutex + 快照 + 分组 setter)
 *
 * HAL/工作线程写入, UI(LVGL)只读快照 —— 单向数据流, 无锁竞争热点。
 * 平台无关(不依赖 HAL), PC 模拟器直接复用。
 *
 * 应用扩展数据：自带 POD 结构体 + getter/setter，内部借用本桥导出的
 * bridge_lock()/bridge_unlock() 共用一把锁（免多锁死锁、免改公共结构）。
 * 范式：static POD 结构体 + getter/setter，写侧 bridge_lock/bridge_unlock。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 全量快照(UI 渲染的唯一数据源) */
typedef struct {
    /* 身份 */
    char device_id[24];

    /* 网络 */
    bool wifi_connected;
    int8_t rssi;                       /* dBm, 未连接 0 */
    char ip_str[16];
    char ssid[33];

    /* 电源 */
    int8_t batt_pct;                   /* -1 = 未知/无电池 */
    bool batt_charging;

    /* 时间已同步(SNTP 成功后置位, 状态栏切换真实时钟) */
    bool time_synced;

    /* 主机/外设连接指示(应用自定义用途, 状态栏 HOST 圆点) */
    bool host_online;

    /* 系统 */
    uint32_t heap_free, heap_min;      /* 字节; min 为历史最低水位 */
    uint32_t uptime_s;
} app_bridge_t;

void bridge_init(void);
void bridge_get(app_bridge_t *out);

/** 供应用扩展数据结构共用的桥锁（App 扩展字段的 getter/setter 内使用） */
void bridge_lock(void);
void bridge_unlock(void);

/* ── 一次性事件(小环形队列, 非 LVGL 任务投递, ui_events 泵消费) ──
 * 事件 ID 由应用自定义枚举(0 保留为"无"); 框架级事件用 0x80+ 区段
 * (如 HAL_WiFi 的 WIFI_EVT_*)。arg 携带小整数参数(如断开原因码)。 */
#define EVT_NONE 0

typedef struct {
    uint8_t id;     /* EVT_NONE = 无 */
    int16_t arg;    /* 事件参数(如 WiFi 断开 reason) */
} bridge_evt_t;

void bridge_post_event(uint8_t id, int16_t arg);
/** 从队列移除指定事件 */
void bridge_purge_event(uint8_t id);
/** 取一个待处理事件(FIFO), 无则返回 .id == EVT_NONE */
bridge_evt_t bridge_poll_event(void);

/* ── 运行日志环(64 行 x 128 字节, 满则覆盖最旧) ── */
#define BRIDGE_LOG_LINES     64
#define BRIDGE_LOG_LINE_LEN  128

/** 追加一行日志(printf 格式, 超长截断) */
void bridge_add_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/** 当前日志行数(0..BRIDGE_LOG_LINES) */
size_t bridge_log_count(void);
/** 拷出日志(旧→新), 返回实际拷出行数 */
size_t bridge_log_read(char (*out)[BRIDGE_LOG_LINE_LEN], size_t max_lines);
/** 清空日志 */
void bridge_log_clear(void);

/* ── 分组 setter(全部内部加锁) ── */
void bridge_set_device_id(const char *device_id);
void bridge_set_network(bool connected, int8_t rssi, const char *ip, const char *ssid);
void bridge_set_batt(int8_t pct, bool charging);
void bridge_set_time_synced(bool synced);
void bridge_set_host_online(bool online);
void bridge_set_system(uint32_t heap_free, uint32_t heap_min, uint32_t uptime_s);

/* ──────────────────── 变更订阅（bridge_watch，推送式数据链） ────────────────────
 *
 * 与 1s 轮询 bridge_get 并存：订阅者只在关心的字段组变化时被回调，
 * 回调恒在 LVGL 任务执行（ui_dispatch 排水尾分发）——回调里可直接碰 UI。
 * 回调不携带 payload：内部自行 bridge_get() 取最新快照（零拷贝）。
 *
 * - 同组在两拍之间的多次写入合并为一次回调（脏标记语义）
 * - min_interval_ms 节流：超频变更挂 pending，到点补发（终值不丢）
 * - 订阅成功立即回放一次（新订阅者马上拿到当前状态绘制）
 * - 订阅/退订请在 LVGL 任务调用（页面 on_resume 订阅 / on_pause 退订），
 *   与 lv_timer 同生命周期约定；任意任务写 setter 触发分发则完全线程安全
 */

/** 字段组位（watch 掩码；与分组 setter 一一对应） */
#define BRIDGE_GRP_WIFI  0x01   /* wifi_connected/rssi/ip/ssid */
#define BRIDGE_GRP_BATT  0x02   /* batt_pct/batt_charging */
#define BRIDGE_GRP_TIME  0x04   /* time_synced */
#define BRIDGE_GRP_HOST  0x08   /* host_online */
#define BRIDGE_GRP_SYS   0x10   /* heap_free/heap_min/uptime_s */
#define BRIDGE_GRP_DEV   0x20   /* device_id */

#define BRIDGE_MAX_WATCH 12     /* 订阅表容量（状态栏+多页面并行足够） */

typedef uint32_t bridge_watch_h_t;   /* 订阅句柄；0 = 失败 */
typedef void (*bridge_watch_cb_t)(void *user_data);

/**
 * @brief 订阅字段组变更（LVGL 任务调用）
 * @param mask 关心的组掩码（BRIDGE_GRP_* 或组合）
 * @param min_interval_ms 节流下限（0 = 不节流，每拍至多一次）
 * @return 订阅句柄；订阅时立即回放一次 cb
 */
bridge_watch_h_t bridge_watch(uint8_t mask, bridge_watch_cb_t cb,
                              void *user_data, uint32_t min_interval_ms);

/** 退订（LVGL 任务调用；句柄失效后空操作） */
void bridge_unwatch(bridge_watch_h_t h);

/** 泵轮询：比较分组版本号并分发到期订阅（ui_dispatch 排水尾自动调用） */
void bridge_poll_watches(void);

/** 调试：当前活跃订阅数 */
size_t bridge_watch_count(void);

#ifdef __cplusplus
}
#endif
