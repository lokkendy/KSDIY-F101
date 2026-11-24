/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - UI 线程编组层实现
 *
 * 环形队列实现要点：
 *  - 自旋锁(portMUX)保护的定长环，队满拒绝新消息，零堆分配
 *  - 单消费者（LVGL 任务排水）+ 多生产者（任意任务投递）
 *  - ui_post_cancel 仅标记 op=UI_OP_DEAD（不搬移条目，避免索引抖动），
 *    排水时跳过 —— 撤销语义为"不再执行"，不保证条目立即出环
 *  - 排水尾挂 bridge_watch 分发：订阅回调与导航命令同线程同序执行
 */
#include "ui_dispatch.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "data_bridge.h"

static const char *TAG = "ui_disp";

enum { UI_OP_DEAD = 0xFF };   /* 已撤销，排水跳过 */

static ui_msg_t s_q[UI_DISPATCH_QDEPTH];
static uint8_t s_rd, s_wr, s_cnt;

static ui_nav_exec_t s_nav_exec;
static lv_timer_t *s_timer;
static volatile void *s_ui_task;   /* LVGL 任务句柄（排水首跳自校准；校准前 NULL） */

/* ──────────────────── 环操作（自旋锁内） ──────────────────── */

static bool q_push(const ui_msg_t *m)
{
    taskENTER_CRITICAL();
    if (s_cnt >= UI_DISPATCH_QDEPTH) {
        taskEXIT_CRITICAL();
        ESP_LOGW(TAG, "queue full, reject new work");
        return false;
    }
    s_q[s_wr] = *m;
    s_wr = (s_wr + 1) % UI_DISPATCH_QDEPTH;
    s_cnt++;
    taskEXIT_CRITICAL();
    return true;
}

static bool q_pop(ui_msg_t *out)
{
    taskENTER_CRITICAL();
    if (s_cnt == 0) {
        taskEXIT_CRITICAL();
        return false;
    }
    *out = s_q[s_rd];
    s_rd = (s_rd + 1) % UI_DISPATCH_QDEPTH;
    s_cnt--;
    taskEXIT_CRITICAL();
    return true;
}

/* ──────────────────── 排水（LVGL 任务） ──────────────────── */

static void ui_dispatch_timer_cb(lv_timer_t *t)
{
    (void)t;

    /* 首跳自校准：timer 回调恒在 LVGL 任务执行 */
    if (s_ui_task == NULL) {
        s_ui_task = (void *)xTaskGetCurrentTaskHandle();
    }

    ui_msg_t m;
    while (q_pop(&m)) {
        if (m.op == UI_OP_DEAD) {
            continue;
        }
        if (m.op == UI_OP_FN) {
            if (m.fn) {
                m.fn(m.arg);
            }
        } else if (s_nav_exec) {
            s_nav_exec(&m);
        }
    }

    /* 订阅分发挂在同一拍：与导航命令同线程同序（回调内可安全碰 LVGL） */
    bridge_poll_watches();
}

/* ──────────────────── 公开 API ──────────────────── */

void ui_dispatch_init(void)
{
    if (s_timer) {
        return;
    }
    s_timer = lv_timer_create(ui_dispatch_timer_cb, UI_DISPATCH_PERIOD_MS, NULL);
    ESP_LOGI(TAG, "dispatch ready (period=%dms depth=%d)",
             UI_DISPATCH_PERIOD_MS, UI_DISPATCH_QDEPTH);
}

void ui_dispatch_set_nav_exec(ui_nav_exec_t exec)
{
    s_nav_exec = exec;
}

bool ui_in_ui_ctx(void)
{
    return s_ui_task != NULL && (void *)xTaskGetCurrentTaskHandle() == (void *)s_ui_task;
}

bool ui_post(ui_work_fn fn, void *arg)
{
    if (!fn) {
        return false;
    }
    if (ui_in_ui_ctx()) {
        fn(arg);   /* immediate 语义：已在 UI 线程，省一次排队 */
        return true;
    }
    ui_msg_t m;
    memset(&m, 0, sizeof(m));
    m.op = UI_OP_FN;
    m.fn = fn;
    m.arg = arg;
    return q_push(&m);
}

bool ui_post_msg(const ui_msg_t *m)
{
    if (!m) {
        return false;
    }
    if (m->op == UI_OP_FN) {
        return ui_post(m->fn, m->arg);
    }
    return q_push(m);
}

void ui_post_cancel(ui_work_fn fn, void *arg)
{
    taskENTER_CRITICAL();
    for (uint8_t i = 0; i < s_cnt; i++) {
        ui_msg_t *e = &s_q[(uint8_t)(s_rd + i) % UI_DISPATCH_QDEPTH];
        if (e->op == UI_OP_FN && e->fn == fn && e->arg == arg) {
            e->op = UI_OP_DEAD;
        }
    }
    taskEXIT_CRITICAL();
}

uint32_t ui_dispatch_pending(void)
{
    uint32_t n;
    taskENTER_CRITICAL();
    n = s_cnt;
    taskEXIT_CRITICAL();
    return n;
}
