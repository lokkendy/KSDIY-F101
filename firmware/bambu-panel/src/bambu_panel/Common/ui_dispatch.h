/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - UI 线程编组层（Looper/Handler 模式）
 *
 * LVGL 非线程安全 → 本层把"任意任务想动 UI"统一编组到 LVGL 任务执行：
 *  - ui_post(fn, arg)：immediate 语义（Android runOnUiThread）——
 *      当前已在 LVGL 任务 → 直接同步调用；否则入定长命令环，
 *      由 LVGL 任务里的排水 timer（10ms）统一执行
 *  - ui_post_msg()：带导航负载的定长消息（page_manager 用；
 *      参数 ≤UI_PARAMS_MAX 字节值拷贝进环，调用方栈帧可回收）
 *  - ui_post_cancel(fn, arg)：撤销未执行的投递（页面销毁时清在途回调，
 *      从结构上取代手写 alive 标志——单消费者 + 撤销 = 无悬挂）
 *  - 排水 timer 首跳自校准 LVGL 任务句柄（ui_in_ui_ctx 判据）；
 *    校准前的投递一律入环，稍后按序执行，不会丢也不会走错线程
 *
 * 排水时机 = LVGL 任务 lv_timer 回调（adapter 持锁跑 handler，
 * 故执行中的命令天然持锁；命令内再拿 ksdiy_lvgl_lock 也安全——递归锁）。
 * 队列满 → 拒绝新消息 + LOGW，调用方回收参数（16 深 × 人速导航，实际打不满）。
 *
 * 线程约束：投递/撤销任意任务可调（内部自旋锁保护环）；ISR 不可用
 * （HAL 回调均为任务上下文）。bridge_watch 订阅分发也挂在本排水尾。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_DISPATCH_PERIOD_MS 10   /* 排水 timer 周期（跨线程投递最大延迟） */
#define UI_DISPATCH_QDEPTH   16    /* 命令环深度 */
#define UI_PARAMS_MAX        64    /* 导航参数值拷贝上限（与 page_manager PM_PARAMS_MAX 对齐） */

typedef void (*ui_work_fn)(void *arg);

/* 消息操作码：UI_OP_FN 为通用投递；其余 = 导航语义，执行器由 page_manager 注册 */
enum {
    UI_OP_FN = 0,        /* fn(arg) 通用延迟执行 */
    UI_OP_NAV_PUSH,      /* ptr=page_t*, params/len=入参 */
    UI_OP_NAV_POP,
    UI_OP_NAV_POP_TO,    /* ptr=page_t* */
    UI_OP_NAV_POP_TO_ROOT,
    UI_OP_NAV_REPLACE,   /* ptr=page_t* */
    UI_OP_NAV_HOME,
    UI_OP_NAV_BACK,      /* 统一返回链 */
    UI_OP_NAV_TAB,       /* iarg=tab 下标 */
    UI_OP_PM_RETRANSLATE,/* 语言原地重译 */
    UI_OP_PM_RELOAD,     /* 语义重建（兼容旧语言切换路径） */
    UI_OP_PM_RELOAD_SHELL,
    UI_OP_PM_ANIM,       /* iarg=动画开关 */
    UI_OP_PM_CHROME,     /* iarg=chrome 可见 */
};

/** 定长命令（入环值拷贝，调用方无须保活） */
typedef struct {
    uint8_t op;
    uint8_t _r[3];
    ui_work_fn fn;             /* UI_OP_FN */
    void *arg;                 /* UI_OP_FN */
    const void *ptr;           /* 导航：page_t*（以 void* 解耦头依赖） */
    int32_t iarg;              /* 整型参数（tab 下标/开关量） */
    uint16_t len;              /* params 有效长度 */
    uint8_t params[UI_PARAMS_MAX];
} ui_msg_t;

/** 导航消息执行器（page_manager 在 pm_shell_init 里注册） */
typedef void (*ui_nav_exec_t)(const ui_msg_t *m);

/**
 * @brief 初始化（建排水 timer；幂等）
 *
 * 须在 LVGL 锁内调用（pm_shell_init 已自动做；lv_timer_create 非线程安全）。
 */
void ui_dispatch_init(void);

/** 注册导航执行器（幂等覆盖；pm_shell_init 自动做） */
void ui_dispatch_set_nav_exec(ui_nav_exec_t exec);

/** 当前在 LVGL 任务？（校准前恒 false —— 投递方此时应入环而非直调） */
bool ui_in_ui_ctx(void);

/** 通用投递：LVGL 任务内直调，否则入环。false=环满，调用方仍持有 arg */
bool ui_post(ui_work_fn fn, void *arg);

/** 带导航负载的投递（值拷贝入环；队满返回 false）。false=不可达（有兜底） */
bool ui_post_msg(const ui_msg_t *m);

/** 撤销未执行的 UI_OP_FN 投递（fn+arg 都匹配才撤）；已执行/不存在则空操作 */
void ui_post_cancel(ui_work_fn fn, void *arg);

/** 命令环当前深度（调试/压测用） */
uint32_t ui_dispatch_pending(void);

#ifdef __cplusplus
}
#endif
