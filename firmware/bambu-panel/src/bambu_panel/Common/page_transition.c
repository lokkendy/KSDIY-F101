/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 页面转场引擎实现
 *
 * 双页动画细节（W/H = 页面容器宽/高）：
 *   PUSH      from: x 0→-22%W, opa 255→210     to: x +W→0
 *   POP       from: x 0→+W                     to: x -22%W→0, opa 210→255
 *   TAB_NEXT  from: x 0→-25%W, opa 255→154     to: x +25%W→0, opa 154→255
 *   TAB_PREV  镜像
 *   FADE      from: opa 255→0                  to: opa 0→255
 *   SLIDE_UP  PUSH: to y +H→0, from opa 255→180（压暗让位）
 *             POP : from y 0→+H, to opa 180→255（镜像回落）
 * 曲线：进 ease_out / 出 ease_in / Tab ease_in_out。
 *
 * 安全规则（DESIGN §6/§10）：
 *  - 框架层保证不在转场中删除页面对象；lv_obj 析构自动 lv_anim_delete
 *    （lv_obj.c:614）双保险，转场对象被删也不会 UAF
 *  - on_done 只由 to 侧动画 completed_cb 触发一次；finish_now 幂等
 *  - 转场期间挡板置顶可见，吃掉所有指针事件（连点/穿透）
 */
#include "page_transition.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "pm_trans";

/* ──────────────────── 引擎状态 ──────────────────── */

static lv_obj_t *s_shield;                 /* 常驻输入挡板（转场期间置顶可见） */
static bool s_busy;
static bool s_enabled = true;

typedef struct {
    pm_trans_ctx_t c;
    int32_t ref_w;                          /* 页面容器宽（动画几何基准） */
    int32_t ref_h;                          /* 页面容器高（SLIDE_UP 几何基准） */
    lv_obj_t *from_obj;                     /* 动画中的对象（finish_now 删动画用） */
    lv_obj_t *to_obj;
} pm_run_t;

static pm_run_t s_run;

/* ──────────────────── 双属性动画执行器 ──────────────────── */

/* p: 0..1000 进度；每帧按方向/类型同时插值 translate_x/translate_y 与 opa，
 * 一侧只挂一个 lv_anim，省一半动画对象 */
static void set_xopa_cb(void *var, int32_t p)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    pm_trans_ctx_t *c = &s_run.c;
    bool is_from = (obj == s_run.from_obj);

    int32_t x0, x1, y0, y1, o0, o1;
    if (c->type == PM_TRANS_FADE) {
        x0 = x1 = y0 = y1 = 0;
        if (is_from) { o0 = LV_OPA_COVER; o1 = 0; }
        else         { o0 = 0;             o1 = LV_OPA_COVER; }
    } else if (c->type == PM_TRANS_SLIDE_UP) {
        x0 = x1 = 0;
        if (c->dir == PM_DIR_POP) {
            /* 出栈：旧顶滑落出屏，下层从压暗中恢复 */
            if (is_from) { y0 = 0;            y1 = s_run.ref_h; o0 = LV_OPA_COVER; o1 = LV_OPA_COVER; }
            else         { y0 = 0;            y1 = 0;            o0 = 180;          o1 = LV_OPA_COVER; }
        } else {   /* PUSH/TAB：新页自底部升起，旧页轻微压暗 */
            if (is_from) { y0 = 0;            y1 = 0;            o0 = LV_OPA_COVER; o1 = 180; }
            else         { y0 = s_run.ref_h;  y1 = 0;            o0 = LV_OPA_COVER; o1 = LV_OPA_COVER; }
        }
    } else if (c->dir == PM_DIR_PUSH) {
        y0 = y1 = 0;
        if (is_from) { x0 = 0;        x1 = -s_run.ref_w * 22 / 100; o0 = LV_OPA_COVER; o1 = 210; }
        else         { x0 = s_run.ref_w; x1 = 0;                    o0 = LV_OPA_COVER; o1 = LV_OPA_COVER; }
    } else if (c->dir == PM_DIR_POP) {
        y0 = y1 = 0;
        if (is_from) { x0 = 0;        x1 = s_run.ref_w;             o0 = LV_OPA_COVER; o1 = LV_OPA_COVER; }
        else         { x0 = -s_run.ref_w * 22 / 100; x1 = 0;        o0 = 210;          o1 = LV_OPA_COVER; }
    } else if (c->dir == PM_DIR_TAB_NEXT) {
        y0 = y1 = 0;
        if (is_from) { x0 = 0;        x1 = -s_run.ref_w * 25 / 100; o0 = LV_OPA_COVER; o1 = 154; }
        else         { x0 = s_run.ref_w * 25 / 100; x1 = 0;         o0 = 154;          o1 = LV_OPA_COVER; }
    } else {   /* TAB_PREV */
        y0 = y1 = 0;
        if (is_from) { x0 = 0;        x1 = s_run.ref_w * 25 / 100;  o0 = LV_OPA_COVER; o1 = 154; }
        else         { x0 = -s_run.ref_w * 25 / 100; x1 = 0;        o0 = 154;          o1 = LV_OPA_COVER; }
    }

    int32_t x = x0 + (x1 - x0) * p / 1000;
    int32_t y = y0 + (y1 - y0) * p / 1000;
    int32_t o = o0 + (o1 - o0) * p / 1000;
    lv_obj_set_style_translate_x(obj, x, 0);
    lv_obj_set_style_translate_y(obj, y, 0);
    lv_obj_set_style_opa(obj, (lv_opa_t)o, 0);
}

/* ──────────────────── 完成/清理 ──────────────────── */

static void trans_cleanup(void)
{
    /* 双页恢复几何干净状态（from 的隐藏/删除由 PageManager 在 on_done 里做） */
    if (s_run.from_obj) {
        lv_obj_set_style_translate_x(s_run.from_obj, 0, 0);
        lv_obj_set_style_translate_y(s_run.from_obj, 0, 0);
        lv_obj_set_style_opa(s_run.from_obj, LV_OPA_COVER, 0);
    }
    if (s_run.to_obj) {
        lv_obj_set_style_translate_x(s_run.to_obj, 0, 0);
        lv_obj_set_style_translate_y(s_run.to_obj, 0, 0);
        lv_obj_set_style_opa(s_run.to_obj, LV_OPA_COVER, 0);
    }
}

static void trans_finish(void)
{
    if (!s_busy) {
        return;   /* 幂等 */
    }

    /* 删在途动画（lv_anim_start 拷贝入链表，按对象删即可） */
    if (s_run.from_obj) {
        lv_anim_delete(s_run.from_obj, NULL);
    }
    if (s_run.to_obj) {
        lv_anim_delete(s_run.to_obj, NULL);
    }

    trans_cleanup();
    if (s_shield && lv_obj_is_valid(s_shield)) {
        lv_obj_add_flag(s_shield, LV_OBJ_FLAG_HIDDEN);
    }
    s_busy = false;

    void (*cb)(void *) = s_run.c.on_done;
    void *ctx = s_run.c.ctx;
    memset(&s_run, 0, sizeof(s_run));
    if (cb) {
        cb(ctx);   /* 回调里可能立刻发起下一次转场 */
    }
}

static void to_anim_completed_cb(lv_anim_t *a)
{
    (void)a;
    trans_finish();
}

/* ──────────────────── 输入挡板 ──────────────────── */

static void shield_create(lv_obj_t *parent)
{
    if (!parent) {
        return;
    }
    if (s_shield && !lv_obj_is_valid(s_shield)) {
        s_shield = NULL;
    }
    if (s_shield) {
        return;
    }
    lv_obj_t *sh = lv_obj_create(parent);
    s_shield = sh;
    lv_obj_set_size(sh, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(sh, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sh, 0, 0);
    lv_obj_set_style_radius(sh, 0, 0);
    lv_obj_set_style_pad_all(sh, 0, 0);
    lv_obj_add_flag(sh, LV_OBJ_FLAG_CLICKABLE);   /* 吃掉指针事件 */
    lv_obj_clear_flag(sh, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sh, LV_OBJ_FLAG_HIDDEN);      /* 平时隐藏 */
}

/* 挡板置顶（新页面 create 后会排到挡板之上，每次转场前重排） */
static void shield_raise(void)
{
    if (s_shield && lv_obj_is_valid(s_shield)) {
        lv_obj_move_to_index(s_shield, -1);   /* -1 = 最后一个 = 最顶层 */
    }
}

/* ──────────────────── 公开 API ──────────────────── */

void pm_trans_init(lv_obj_t *shield_parent)
{
    shield_create(shield_parent);
}

void pm_trans_detach_shell(void)
{
    if (s_run.from_obj && lv_obj_is_valid(s_run.from_obj)) {
        lv_anim_delete(s_run.from_obj, NULL);
    }
    if (s_run.to_obj && lv_obj_is_valid(s_run.to_obj)) {
        lv_anim_delete(s_run.to_obj, NULL);
    }
    s_shield = NULL;
    s_busy = false;
    memset(&s_run, 0, sizeof(s_run));
}

bool pm_trans_busy(void)
{
    return s_busy;
}

void pm_trans_set_enabled(bool on)
{
    s_enabled = on;
    if (!on) {
        pm_trans_finish_now();   /* 关动画时立即收敛在途转场 */
    }
}

bool pm_trans_is_enabled(void)
{
    return s_enabled;
}

void pm_trans_finish_now(void)
{
    trans_finish();
}

bool pm_trans_run(const pm_trans_ctx_t *ctx)
{
    if (!ctx || !ctx->to) {
        ESP_LOGE(TAG, "invalid ctx");
        return false;
    }
    if (s_busy) {
        return false;   /* 调用方（PageManager）负责 IGNORE 策略 */
    }

    memset(&s_run, 0, sizeof(s_run));
    s_run.c = *ctx;
    pm_trans_ctx_t *c = &s_run.c;
    s_run.from_obj = c->from;
    s_run.to_obj = c->to;

    if (c->duration == 0) {
        c->duration = (c->dir == PM_DIR_TAB_NEXT || c->dir == PM_DIR_TAB_PREV)
                      ? PM_T_DUR_FAST : PM_T_DUR_NORMAL;
    }
    /* 参考几何：取 to 的父容器（页面容器）实际宽高 */
    lv_obj_t *parent = lv_obj_get_parent(c->to);
    s_run.ref_w = parent ? lv_obj_get_width(parent) : 480;
    s_run.ref_h = parent ? lv_obj_get_height(parent) : 480;

    shield_raise();
    if (s_shield && lv_obj_is_valid(s_shield)) {
        lv_obj_clear_flag(s_shield, LV_OBJ_FLAG_HIDDEN);
    }

    /* 无动画路径：摆好终态 → 直接触发完成（生命周期/回调顺序不变） */
    if (c->type == PM_TRANS_NONE || !s_enabled) {
        s_busy = true;
        trans_finish();
        return true;
    }

    /* 初始姿态（先摆好再动画，避免首帧闪终态） */
    set_xopa_cb(c->to, 0);
    if (c->from) {
        set_xopa_cb(c->from, 0);
    }

    s_busy = true;

    /* to 侧动画：completed_cb 是唯一完成源（进 ease-out，Tab 用 ease-in-out） */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c->to);
    lv_anim_set_exec_cb(&a, set_xopa_cb);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, c->duration);
    lv_anim_set_path_cb(&a,
                        (c->dir == PM_DIR_TAB_NEXT || c->dir == PM_DIR_TAB_PREV)
                            ? lv_anim_path_ease_in_out : lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, to_anim_completed_cb);
    lv_anim_start(&a);

    /* from 侧动画（出 ease-in；无 from 则只有 to） */
    if (c->from) {
        lv_anim_t af;
        lv_anim_init(&af);
        lv_anim_set_var(&af, c->from);
        lv_anim_set_exec_cb(&af, set_xopa_cb);
        lv_anim_set_values(&af, 0, 1000);
        lv_anim_set_duration(&af, c->duration);
        lv_anim_set_path_cb(&af, lv_anim_path_ease_in);
        lv_anim_start(&af);
    }

    return true;
}
