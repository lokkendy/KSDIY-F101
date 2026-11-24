/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 全局状态栏实现
 */
#include "statusbar.h"
#include "i18n.h"
#include "ui_common.h"
#include "ui_screen.h"   /* ── 应用私有扩展：横屏状态栏高度 ── */
#include "ksdiy_status_icons.h"
#include "HAL_WiFi.h"
#include <stdio.h>
#include <string.h>
#include <time.h>


/* time(NULL) 早于 2024-01-01 视为未对时（SNTP 尚未成功，模拟器宿主时间为真实值） */
#define TIME_SYNCED_EPOCH 1704038400

typedef struct {
    const sb_item_t *def;
    lv_obj_t *obj;      /* 主对象（label） */
    lv_obj_t *aux;      /* SB_HOST: 状态圆点 */
    bool     last_host;
} sb_item_rt_t;

typedef struct {
    lv_obj_t *bar;
    sb_item_rt_t items[SB_MAX_ITEMS * 2];   /* 左侧 + 右侧 */
    size_t n_items;
    lv_timer_t *timer;
} statusbar_t;

static statusbar_t s_sb;
static bridge_watch_h_t s_sb_watch;   /* WIFI/BATT/HOST 字段组订阅（外壳重建时重挂） */

static void item_update(sb_item_rt_t *it, const app_bridge_t *b);

/* 快照拷出后逐条差量更新（定时器与 watch 回调共用；UI 永远不长期持有桥锁） */
static void sb_refresh(statusbar_t *sb)
{
    if (!sb || !sb->bar || !lv_obj_is_valid(sb->bar)) {
        return;
    }
    app_bridge_t b;
    bridge_get(&b);

    for (size_t i = 0; i < sb->n_items; i++) {
        item_update(&sb->items[i], &b);
    }
}

static void statusbar_update_cb(lv_timer_t *t)
{
    sb_refresh((statusbar_t *)lv_timer_get_user_data(t));
}

/* bridge_watch 回调（ui_dispatch 排水尾 = LVGL 上下文，与导航命令同线程同序）：
 * WIFI/BATT/HOST 任一字段组变化 → 立即刷；节流 200ms 合并抖动 */
static void sb_watch_cb(void *user_data)
{
    statusbar_t *sb = (statusbar_t *)user_data;
    if (!sb || !sb->bar || !lv_obj_is_valid(sb->bar)) {
        return;   /* 外壳已拆未重建（防御，正常流程重建时已重挂订阅） */
    }
    sb_refresh(sb);
}

static void item_update(sb_item_rt_t *it, const app_bridge_t *b)
{
    switch (it->def->type) {
    case SB_WIFI: {
        ksdiy_link_state_t link = b->wifi_connected ? KSDIY_LINK_CONNECTED :
                                  HAL_WiFi_GetState() == WIFI_STATE_CONNECTING ? KSDIY_LINK_WAITING : KSDIY_LINK_OFF;
        ksdiy_status_link_set(it->obj, link);
        break;
    }

    case SB_BATT:
        ksdiy_status_battery_refresh(it->obj);
        break;

    case SB_CLOCK: {
        time_t now = time(NULL);
        char time_text[8] = "--:--";
        if (now > TIME_SYNCED_EPOCH) {
            struct tm *clock = localtime(&now);
            if (clock) strftime(time_text, sizeof(time_text), "%H:%M", clock);
        }
        if (strcmp(lv_label_get_text(it->obj), time_text)) lv_label_set_text(it->obj, time_text);
        break;
    }

    case SB_HOST:
        if (b->host_online != it->last_host) {
            it->last_host = b->host_online;
            lv_color_t c = b->host_online ? UI_COLOR_SUCCESS : UI_COLOR_TEXT_SEC;
            lv_obj_set_style_bg_color(it->aux, c, 0);
            lv_obj_set_style_text_color(it->obj, c, 0);
        }
        break;

    case SB_CUSTOM:
        if (it->def->update_cb) {
            it->def->update_cb(it->obj, b, it->def->user_data);
        }
        break;

    case SB_BRAND:
    default:
        break;
    }
}

static ksdiy_status_colors_t status_colors(void)
{
    return (ksdiy_status_colors_t){
        .ink = UI_COLOR_TEXT,
        .muted = UI_COLOR_TEXT_SEC,
        .surface = UI_COLOR_BG_BAR,
        .connected = UI_COLOR_SUCCESS,
        .waiting = UI_COLOR_ACCENT,
        .low = UI_COLOR_DANGER,
    };
}

static sb_item_rt_t *sb_add_item(statusbar_t *sb, lv_obj_t *parent,
                                 const sb_item_t *def)
{
    if (sb->n_items >= SB_MAX_ITEMS * 2) {
        return NULL;
    }
    sb_item_rt_t *it = &sb->items[sb->n_items++];
    memset(it, 0, sizeof(*it));
    it->def = def;

    if (def->type == SB_CUSTOM && def->create_cb) {
        it->obj = def->create_cb(parent);
        return it;
    }

    switch (def->type) {
    case SB_BRAND: {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, def->text ? def->text : "KSDIY");
        lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, 0);
        lv_obj_set_style_text_color(lbl, UI_COLOR_ACCENT, 0);
        it->obj = lbl;
        break;
    }
    case SB_WIFI:
        it->obj = ksdiy_status_wifi_create(parent, status_colors());
        break;

    case SB_BATT:
        it->obj = ksdiy_status_battery_create(parent, UI_FONT_TITLE, status_colors());
        break;

    case SB_CLOCK: {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, "--:--");
        lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, 0);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        it->obj = lbl;
        break;
    }
    case SB_HOST:
        it->aux = ui_create_dot(parent, UI_COLOR_TEXT_SEC);
        it->obj = lv_label_create(parent);
        if (def->text) {
            lv_label_set_text(it->obj, def->text);
        } else {
            tr_bind(it->obj, "主机");   /* 语言切换原地重译（pm_retranslate 走注册表） */
        }
        lv_obj_set_style_text_font(it->obj, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(it->obj, UI_COLOR_TEXT_SEC, 0);
        break;

    case SB_CUSTOM:
    default: {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, def->text ? def->text : "");
        lv_obj_set_style_text_font(lbl, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        it->obj = lbl;
        break;
    }
    }
    return it;
}

lv_obj_t *statusbar_create(lv_obj_t *parent, const statusbar_cfg_t *cfg)
{
    statusbar_t *sb = &s_sb;
    if (s_sb_watch) {
        bridge_unwatch(s_sb_watch);   /* 外壳重建：旧实例订阅先撤（定时器同款处理） */
        s_sb_watch = 0;
    }
    if (sb->timer) {
        lv_timer_delete(sb->timer);   /* 外壳重建：lv_timer 不随对象删除，先清旧实例 */
    }
    memset(sb, 0, sizeof(*sb));

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), ui_land_status_bar_h());
    lv_obj_set_style_bg_color(bar, UI_COLOR_BG_BAR, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, UI_PAD, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, UI_COLOR_BORDER, 0);
    sb->bar = bar;

    /* 左侧条目（品牌区）: 左对齐 flex 行 */
    if (cfg->n_left > 0 && cfg->left) {
        lv_obj_t *left = lv_obj_create(bar);
        lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(left, 0, 0);
        lv_obj_set_style_radius(left, 0, 0);
        lv_obj_set_style_pad_all(left, 0, 0);
        lv_obj_set_style_pad_column(left, 12, 0);
        lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        for (size_t i = 0; i < cfg->n_left && i < SB_MAX_ITEMS; i++) {
            sb_add_item(sb, left, &cfg->left[i]);
        }
    }

    /* 右侧条目（图标 + 时钟 + 电量）: 右对齐 flex 行
       时钟文本变宽时图标自动左移，不会与时间重叠 */
    if (cfg->n_right > 0 && cfg->right) {
        lv_obj_t *right = lv_obj_create(bar);
        lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(right, 0, 0);
        lv_obj_set_style_radius(right, 0, 0);
        lv_obj_set_style_pad_all(right, 0, 0);
        lv_obj_set_style_pad_column(right, 10, 0);
        lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        for (size_t i = 0; i < cfg->n_right && i < SB_MAX_ITEMS; i++) {
            sb_add_item(sb, right, &cfg->right[i]);
        }
    }

    sb->timer = lv_timer_create(statusbar_update_cb,
                                cfg->period_ms ? cfg->period_ms : 1000, sb);

    /* 推送订阅：WIFI/BATT/HOST 变化 ≤10ms 编组到 LVGL 任务立即刷
     * （订阅即回放一次 = 初次刷新；时钟仍靠周期 timer，时间自走无版本号） */
    s_sb_watch = bridge_watch(BRIDGE_GRP_WIFI | BRIDGE_GRP_BATT | BRIDGE_GRP_HOST,
                              sb_watch_cb, sb, 200);
    sb_refresh(sb);
    return bar;
}

void statusbar_refresh(void)
{
    if (!s_sb.bar || !lv_obj_is_valid(s_sb.bar)) {
        return;
    }
    sb_refresh(&s_sb);
}
