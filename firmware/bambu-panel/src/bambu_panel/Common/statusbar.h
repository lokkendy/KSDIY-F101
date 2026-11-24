/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 全局状态栏（配置注入式）
 *
 * 状态栏内容不写死：应用在 app_shell_cfg.c 里用 sb_item_t 数组声明
 * 左侧/右侧条目（品牌 / WiFi / 电量 / 时钟 / 主机指示 / 自定义），
 * statusbar_create() 按表构建。
 * 刷新双通道：WIFI/BATT/HOST 字段组变化经 bridge_watch 推送（≤10ms +
 * 200ms 节流合并）；时钟靠周期 timer（时间自走无版本号，默认 1000ms）。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "data_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SB_MAX_ITEMS  8   /* 单侧最多条目数 */

typedef enum {
    SB_BRAND = 0,   /* 品牌文字（item.text, 缺省 "KSDIY"） */
    SB_WIFI,        /* 无线连接状态 */
    SB_BATT,        /* 电池电量与充电 / 外接供电 */
    SB_CLOCK,       /* 已对时显示 HH:MM */
    SB_HOST,        /* 状态圆点 + 文字，颜色随 host_online */
    SB_CUSTOM,      /* 自定义：update_cb 全权更新 item 对象 */
} sb_item_type_t;

typedef struct {
    sb_item_type_t type;
    const char *text;   /* BRAND 文字 / HOST 文字 / CUSTOM 初始文本 */
    lv_obj_t *(*create_cb)(lv_obj_t *parent);
    /** CUSTOM 条目每周期回调（LVGL 上下文；b 为快照副本，不持锁） */
    void (*update_cb)(lv_obj_t *item, const app_bridge_t *b, void *user_data);
    void *user_data;
} sb_item_t;

typedef struct {
    const sb_item_t *left;      /* 左侧条目（品牌区），可为 NULL */
    size_t n_left;
    const sb_item_t *right;     /* 右侧条目（图标+时钟+电量），可为 NULL */
    size_t n_right;
    uint32_t period_ms;         /* 刷新周期, 0 = 1000 */
} statusbar_cfg_t;

/**
 * @brief 创建状态栏（挂到外壳顶部；页面管理器内部调用）
 * @note  必须在 LVGL 锁内调用; 重复调用会先清旧定时器（外壳重建场景）
 */
lv_obj_t *statusbar_create(lv_obj_t *parent, const statusbar_cfg_t *cfg);

/** 语言切换后立即重刷（CUSTOM 条目会重新 tr()；与定时器刷新共用逻辑） */
void statusbar_refresh(void);

#ifdef __cplusplus
}
#endif
