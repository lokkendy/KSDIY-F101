/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 页面转场引擎
 *
 * PageManager 决定"去哪"，本引擎决定"怎么过去"：
 *  - 双页同时动画（outgoing + incoming），方向严格对称：
 *      PUSH: 旧页左移淡出 / 新页从右滑入；POP 完全镜像
 *      TAB_*: 短距滑动（±25%W）+ 透明度，比整页推入轻
 *      FADE: 交叉淡化（位移不动）
 *  - 转场期间挂全屏透明输入挡板（防连点穿透）
 *  - 唯一完成回调（引擎内聚，不会重复触发）
 *  - pm_trans_finish_now(): 瞬间到终态并触发回调（语义操作 snap 用）
 *
 * 动画只做 translate_x / opa（整页位移+透明度，MCU 廉价），
 * scale 类（Modal 卡片）由 ui_modal 自行实现，不走本引擎。
 * 设计依据: PAGE_MANAGER_DESIGN.md §6
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 转场方向（引擎据此决定动画几何，页面不关心） */
typedef enum {
    PM_DIR_PUSH = 0,     /* 进入下一层：新页从右进，旧页左移让位 */
    PM_DIR_POP,          /* 返回上一层：与 PUSH 严格镜像 */
    PM_DIR_TAB_NEXT,     /* Tab 右切：短距滑出/滑入 */
    PM_DIR_TAB_PREV,     /* Tab 左切：镜像 */
} pm_dir_t;

/* 转场类型（page_t.transition 引用；PUSH/POP 方向由 dir 决定） */
typedef enum {
    PM_TRANS_SLIDE = 0,  /* 默认：位移 + 轻微透明度层级感 */
    PM_TRANS_FADE,       /* 交叉淡化（位移不动）*/
    PM_TRANS_NONE,       /* 无动画（仍走完成回调，生命周期不变）*/
    PM_TRANS_SLIDE_UP,   /* 底部滑入（PUSH 新页自屏幕下方升起，旧页轻微压暗；
                          * POP 镜像回落）—— 详情页/抽屉/半屏页语义 */
} pm_transition_t;

/* 全局动画时长（毫秒）——业务代码禁止自写毫秒，见 DESIGN §6 */
#define PM_T_DUR_FAST    140     /* Tab / 轻量切换 */
#define PM_T_DUR_NORMAL  220     /* push / pop 默认 */
#define PM_T_DUR_SLOW    280     /* Modal / 首屏 */

typedef struct {
    lv_obj_t *from;              /* 旧页；NULL = 无旧页（首屏进场） */
    lv_obj_t *to;                /* 新页；必须非 NULL */
    pm_dir_t dir;
    pm_transition_t type;
    uint16_t duration;           /* 0 = 按 dir 取默认（TAB=FAST 其余=NORMAL） */
    void (*on_done)(void *ctx);  /* 完成回调（含无动画路径，只触发一次） */
    void *ctx;
} pm_trans_ctx_t;

/** 初始化（shield_parent = 页面容器，挡板常驻其内，转场期间可见） */
void pm_trans_init(lv_obj_t *shield_parent);

/** 外壳即将删除时调用：丢弃挡板/在途转场状态，不触发 on_done（对象已随 shell 销毁） */
void pm_trans_detach_shell(void);

/** 启动一次转场；引擎忙（上次未完）返回 false，调用方应放弃本次请求 */
bool pm_trans_run(const pm_trans_ctx_t *ctx);

/** 转场进行中？ */
bool pm_trans_busy(void);

/** 瞬间完成当前转场（跳到终态并触发 on_done）；空闲时安全空操作 */
void pm_trans_finish_now(void);

/** 全局动画开关（false = 所有转场瞬时完成，生命周期/回调顺序不变） */
void pm_trans_set_enabled(bool on);
bool pm_trans_is_enabled(void);

#ifdef __cplusplus
}
#endif
