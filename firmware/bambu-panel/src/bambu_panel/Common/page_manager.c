/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 页面管理器实现
 *
 * 核心规则（与 PAGE_MANAGER_DESIGN.md 逐条对应）：
 *  - 栈底 stack[0] 恒为当前 Tab 根页；pm_push 压二级页
 *  - 生命周期顺序：
 *      push: from.on_pause → to.create/on_enter → 转场 → from 处置 → to.on_resume
 *      pop : from.on_pause → to.on_enter(先复显) → 反向转场 → from.on_leave+处置 → to.on_resume
 *    框架从不删正在动画的对象；on_resume/on_leave 恒在转场完成后
 *  - 转场期间导航请求 IGNORE（连点只进一页）；pm_switch_tab/pm_pop_to_root/
 *    pm_home/pm_reload* 为语义操作：先 snap-finish 再执行
 *  - KEEP_ALIVE 弹出进缓存表（≤PM_CACHE_MAX LRU），复用时状态原样保留
 *  - allow_multiple=false（默认）：push 栈内已有页 → 自动 popTo
 */
#include "page_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "i18n.h"
#include "ui_common.h"
#include "ui_bambu.h"
#include "bambu_cloud.h"
#include "ui_dispatch.h"
#include "ui_keyboard.h"
#include "ui_modal.h"
#include "ui_screen.h"   /* ── 应用私有扩展：横屏 Tab 栏高度 ── */

/* ── 应用私有扩展：全局 1s 打印机 UI 刷新分发（App.c app_printer_ui_tick） ── */
#include "History.h"
#include "Home.h"
#include "Job.h"

#include "data_bridge.h"
#include "statusbar.h"

static const char *TAG = "page_mgr";

/* ──────────────────── 内部状态 ──────────────────── */

/* 页面生命周期状态（栈条目内部） */
enum {
    ST_ENTERING = 1,   /* 进场中（转场未完成）*/
    ST_ACTIVE,         /* 顶层活跃 */
    ST_PAUSED,         /* 被覆盖（对象在树上 HIDDEN，或 DESTROY_ON_LEAVE 已拆）*/
    ST_LEAVING,        /* 退场中（转场未完成，完成即销毁/入缓存）*/
};

typedef struct {
    const page_t *page;
    lv_obj_t *obj;      /* NULL = 对象已拆（DESTROY_ON_LEAVE），返回时重建 */
    uint8_t state;
} pm_entry_t;

typedef struct {
    const page_t *page;
    lv_obj_t *obj;      /* 挂在树上 HIDDEN，随时可复用 */
    uint32_t stamp;     /* LRU 戳 */
} pm_cache_ent_t;

static const pm_shell_cfg_t *s_cfg = NULL;

static pm_entry_t s_stack[PM_MAX_DEPTH];
static int s_top = -1;

static pm_cache_ent_t s_cache[PM_CACHE_MAX];
static uint32_t s_stamp = 0;

static uint8_t s_params[PM_PARAMS_MAX];
static size_t s_params_len = 0;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_shell = NULL;               /* 外壳容器（配色重建时删除） */
static lv_obj_t *s_content = NULL;             /* 页面容器 */
static lv_obj_t *s_statusbar_obj = NULL;       /* 外壳状态栏（全屏页隐藏用） */
static lv_obj_t *s_tab_bar_obj = NULL;         /* 外壳 Tab 栏（全屏页隐藏用） */
static lv_obj_t *s_tab_btns[PM_MAX_TABS];
static lv_obj_t *s_tab_icons[PM_MAX_TABS];     /* 图标/文字指针单独存：子对象顺序含指示条 */
static lv_obj_t *s_tab_labels[PM_MAX_TABS];
static lv_obj_t *s_tab_indicators[PM_MAX_TABS];
static int s_cur_tab = 0;

/* 在途导航上下文（转场锁保证同时最多一个；on_done 异步读取） */
typedef struct {
    pm_entry_t *from;
    pm_entry_t *to;
    pm_entry_t *from_slot;   /* replace/tab 换根：旧顶所在槽（完成后整槽释放） */
} pm_nav_t;
static pm_nav_t s_nav;

/* 缓存/暂停页若仅 HIDDEN 仍与当前页同坐标叠放；LVGL blur_walk 失效时会
 * 遍历相交区域上的整棵子树，引发 lv_obj_invalidate 递归卡死 lvgl 任务。 */
#define PM_OFFSCREEN_X  (-4096)
#define PM_OFFSCREEN_Y  (-4096)

static void page_hide_offscreen(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(obj, PM_OFFSCREEN_X, PM_OFFSCREEN_Y);
}

static void page_show_onscreen(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(obj, 0, 0);
}

/* 整页销毁/重建：关闭 invalidation，避免 blur_walk 对 act_scr 反复全树遍历卡死 lvgl */
static void bulk_ui_begin(lv_display_t **disp_out)
{
    lv_display_t *disp = lv_display_get_default();
    if (disp_out) {
        *disp_out = disp;
    }
    if (disp) {
        lv_display_enable_invalidation(disp, false);
    }
}

static void bulk_ui_end(lv_display_t *disp)
{
    if (!disp) {
        return;
    }
    lv_display_enable_invalidation(disp, true);
    lv_obj_t *scr = lv_scr_act();
    if (scr) {
        lv_obj_invalidate(scr);
    }
}

static void dismiss_chrome_overlays(void)
{
    ui_loading_hide();
    if (ui_keyboard_active()) {
        ui_keyboard_hide();
    }
    while (ui_modal_is_open()) {
        ui_modal_close_top();
    }
}

/* ──────────────────── 小工具 ──────────────────── */

static pm_entry_t *top_entry(void)
{
    return s_top >= 0 ? &s_stack[s_top] : NULL;
}

/* ──────────────────── 跨线程编组（ui_dispatch 桥） ────────────────────
 * 公开导航 API 的 LVGL 上下文实现体（*_impl）与编组外壳分离：
 * 外壳判 ui_in_ui_ctx() —— 在 LVGL 任务直接执行（零开销快路径），
 * 否则打包容进 ui_dispatch 命令环，由 LVGL 排水 timer 按序执行。
 * 内部互调（如 home→switch_tab）一律走 *_impl，避免二次入环。 */

static void push_impl(const page_t *page, const void *data, size_t len);
static void pop_impl(void);
static bool pop_to_impl(const page_t *page);
static void pop_to_root_impl(void);
static void replace_impl(const page_t *page);
static void home_impl(void);
static bool back_impl(void);
static void switch_tab_impl(int tab);
static void reload_impl(void);
static void reload_shell_impl(void);
static void retranslate_impl(void);
static void set_chrome_impl(bool visible);

/** 打包导航命令入环（值拷贝 ≤UI_PARAMS_MAX 字节参数） */
static bool post_nav(uint8_t op, const void *ptr, int32_t iarg,
                     const void *data, size_t len)
{
    ui_msg_t m;
    memset(&m, 0, sizeof(m));
    m.op = op;
    m.ptr = ptr;
    m.iarg = iarg;
    if (data && len) {
        m.len = len > UI_PARAMS_MAX ? UI_PARAMS_MAX : len;
        memcpy(m.params, data, m.len);
    }
    return ui_post_msg(&m);
}

/** ui_dispatch 排水回调：执行队列里的导航/框架命令（LVGL 任务） */
static void nav_exec(const ui_msg_t *m)
{
    switch (m->op) {
    case UI_OP_NAV_PUSH:
        push_impl((const page_t *)m->ptr, m->len ? m->params : NULL, m->len);
        break;
    case UI_OP_NAV_POP:
        pop_impl();
        break;
    case UI_OP_NAV_POP_TO:
        if (!pop_to_impl((const page_t *)m->ptr)) {
            ESP_LOGI(TAG, "[NAV] queued pop_to('%s') target not in stack",
                     ((const page_t *)m->ptr)->name);
        }
        break;
    case UI_OP_NAV_POP_TO_ROOT:
        pop_to_root_impl();
        break;
    case UI_OP_NAV_REPLACE:
        replace_impl((const page_t *)m->ptr);
        break;
    case UI_OP_NAV_HOME:
        home_impl();
        break;
    case UI_OP_NAV_BACK:
        back_impl();
        break;
    case UI_OP_NAV_TAB:
        switch_tab_impl(m->iarg);
        break;
    case UI_OP_PM_RETRANSLATE:
        retranslate_impl();
        break;
    case UI_OP_PM_RELOAD:
        reload_impl();
        break;
    case UI_OP_PM_RELOAD_SHELL:
        reload_shell_impl();
        break;
    case UI_OP_PM_ANIM:
        pm_trans_set_enabled(m->iarg != 0);
        break;
    case UI_OP_PM_CHROME:
        set_chrome_impl(m->iarg != 0);
        break;
    default:
        break;
    }
}

static const char *st_name(uint8_t st)
{
    switch (st) {
    case ST_ENTERING: return "ENTERING";
    case ST_ACTIVE:   return "ACTIVE";
    case ST_PAUSED:   return "PAUSED";
    case ST_LEAVING:  return "LEAVING";
    default:          return "-";
    }
}

static const page_t *cur_root_page(void)
{
    if (!s_cfg) {
        return NULL;
    }
    if (s_cfg->tabs && s_cfg->n_tabs > 0) {
        return s_cfg->tabs[s_cur_tab].page;
    }
    return s_cfg->root_page;
}

/* 生命周期转发（判空集中一处） */
static void call_pause(const pm_entry_t *e)
{
    if (e && e->page && e->page->on_pause) {
        e->page->on_pause(e->page->user_data);
    }
}
static void call_enter(const pm_entry_t *e)
{
    if (e && e->page && e->page->on_enter) {
        e->page->on_enter(e->page->user_data);
    }
}
static void call_resume(const pm_entry_t *e)
{
    if (e && e->page && e->page->on_resume) {
        e->page->on_resume(e->page->user_data);
    }
}

/* ──────────────────── 条目处置 ──────────────────── */

/* 彻底销毁：on_leave → destroy 回调 → 删对象 → 槽位清空 */
static void entry_destroy(pm_entry_t *e)
{
    if (!e->page) {
        return;
    }
    if (e->page->on_leave) {
        e->page->on_leave(e->page->user_data);
    }
    if (e->page->destroy) {
        e->page->destroy(e->page->user_data);
    }
    if (e->obj) {
        lv_obj_delete(e->obj);   /* 析构自动 lv_anim_delete（lv_obj.c:614）*/
    }
    e->obj = NULL;
    e->page = NULL;
    e->state = 0;
}

/* 对象入缓存（KEEP_ALIVE 弹出/换根用）：on_leave → HIDDEN → LRU 表 */
static bool entry_to_cache(pm_entry_t *e)
{
    if (!e->page || !e->obj) {
        return false;
    }
    if (e->page->on_leave) {
        e->page->on_leave(e->page->user_data);
    }
    page_hide_offscreen(e->obj);

    /* 找空位；满则 LRU 逐出最旧（彻底销毁） */
    int slot = -1, oldest = 0;
    for (int i = 0; i < PM_CACHE_MAX; i++) {
        if (!s_cache[i].page) {
            slot = i;
            break;
        }
        if (s_cache[i].stamp < s_cache[oldest].stamp) {
            oldest = i;
        }
    }
    if (slot < 0) {
        pm_cache_ent_t *ev = &s_cache[oldest];
        ESP_LOGI(TAG, "cache LRU evict: %s", ev->page->name);
        if (ev->page->destroy) {
            ev->page->destroy(ev->page->user_data);
        }
        lv_obj_delete(ev->obj);
        slot = oldest;
    }
    s_cache[slot].page = e->page;
    s_cache[slot].obj = e->obj;
    s_cache[slot].stamp = ++s_stamp;
    e->obj = NULL;
    e->page = NULL;
    e->state = 0;
    return true;
}

/* 从缓存取出（push/换根复用）：命中则摘表返回对象（调用方复显） */
static lv_obj_t *cache_take(const page_t *page)
{
    for (int i = 0; i < PM_CACHE_MAX; i++) {
        if (s_cache[i].page == page) {
            lv_obj_t *obj = s_cache[i].obj;
            s_cache[i].page = NULL;
            s_cache[i].obj = NULL;
            page_show_onscreen(obj);
            ESP_LOGI(TAG, "cache reuse: %s", page->name);
            return obj;
        }
    }
    return NULL;
}

/* 缓存全清（语言/配色重建：缓存文案/样式已过期） */
static void cache_clear(void)
{
    for (int i = 0; i < PM_CACHE_MAX; i++) {
        if (s_cache[i].page) {
            if (s_cache[i].page->destroy) {
                s_cache[i].page->destroy(s_cache[i].page->user_data);
            }
            lv_obj_delete(s_cache[i].obj);
            s_cache[i].page = NULL;
            s_cache[i].obj = NULL;
        }
    }
}

/* 生成/复用页面对象（缓存命中复用；否则 create） */
static lv_obj_t *entry_spawn(const page_t *page)
{
    lv_obj_t *obj = cache_take(page);
    if (!obj) {
        obj = page->create(s_content, page->user_data);
    }
    return obj;
}

/* ──────────────────── 转场完成回调 ──────────────────── */

/* push 完成：处置 from（隐藏/销毁）→ to 转 ACTIVE + resume */
static void push_done(void *ctx)
{
    (void)ctx;
    pm_entry_t *from = s_nav.from;
    pm_entry_t *to = s_nav.to;
    s_nav.from = s_nav.to = NULL;

    if (from && from->page) {
        if ((pm_cache_policy_t)from->page->cache == PM_CACHE_DESTROY_ON_LEAVE) {
            pm_entry_t tmp = *from;
            from->page = NULL;              /* 槽位保留（栈序不变），对象处置 */
            from->obj = NULL;
            from->state = ST_PAUSED;
            entry_destroy(&tmp);            /* on_leave + destroy + delete */
        } else {
            if (from->obj) {
                page_hide_offscreen(from->obj);
            }
            from->state = ST_PAUSED;
        }
    }
    if (to && to->page) {
        to->state = ST_ACTIVE;
        call_resume(to);
    }
}

/* pop 完成：栈收缩 → from 销毁/入缓存 → to 转 ACTIVE + resume */
static void pop_done(void *ctx)
{
    (void)ctx;
    pm_entry_t *from = s_nav.from;          /* = 旧栈顶 */
    pm_entry_t *to = s_nav.to;              /* = 新栈顶（s_top-1） */
    s_nav.from = s_nav.to = NULL;

    s_top--;                                /* 先收缩：from 槽变为"已弹出" */
    if (from->page) {
        if ((pm_cache_policy_t)from->page->cache == PM_CACHE_KEEP_ALIVE) {
            entry_to_cache(from);
        } else {
            entry_destroy(from);
        }
    }
    if (to && to->page) {
        to->state = ST_ACTIVE;
        call_resume(to);
    }
}

/* replace / tab 换根完成：from_slot 整槽释放并写入 to */
static void swap_done(void *ctx)
{
    (void)ctx;
    pm_entry_t *from = s_nav.from;          /* 旧根（临时展示用） */
    pm_entry_t *to = s_nav.to;              /* 新根（暂存于 from_slot 之外的槽） */
    pm_entry_t *slot = s_nav.from_slot;     /* 目标槽（= 旧根槽位） */
    s_nav.from = s_nav.to = s_nav.from_slot = NULL;

    if (from->page) {
        if ((pm_cache_policy_t)from->page->cache == PM_CACHE_KEEP_ALIVE) {
            entry_to_cache(from);
        } else {
            entry_destroy(from);
        }
    }
    if (to && to->page) {
        *slot = *to;                        /* 新根落位（含对象/状态） */
        to->page = NULL;                    /* 清临时槽 */
        to->obj = NULL;
        slot->state = ST_ACTIVE;
        call_resume(slot);
    }
}

/* ──────────────────── 导航实现 ──────────────────── */

void pm_push(const page_t *page)
{
    pm_push_params(page, NULL, 0);
}

void pm_push_params(const page_t *page, const void *data, size_t len)
{
    if (!page || !s_content) {
        ESP_LOGE(TAG, "invalid push");
        return;
    }
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_NAV_PUSH, page, 0, data, len);
        return;
    }
    push_impl(page, data, len);
}

static void push_impl(const page_t *page, const void *data, size_t len)
{
    if (pm_trans_busy()) {
        ESP_LOGW(TAG, "[NAV] push '%s' ignored (transitioning)", page->name);
        return;
    }
    if (s_top >= PM_MAX_DEPTH - 1) {
        ESP_LOGE(TAG, "stack overflow, cannot push %s", page->name);
        return;
    }

    /* 重复实例保护：栈内已有 → 自动 popTo 它 */
    if (!page->allow_multiple) {
        for (int i = 0; i <= s_top; i++) {
            if (s_stack[i].page == page) {
                ESP_LOGI(TAG, "[NAV] '%s' already at depth %d, popTo",
                         page->name, i);
                pop_to_impl(page);
                return;
            }
        }
    }

    /* 参数值拷贝（页面 create/on_enter 期间经 pm_params() 读） */
    s_params_len = 0;
    if (data && len) {
        s_params_len = len > PM_PARAMS_MAX ? PM_PARAMS_MAX : len;
        memcpy(s_params, data, s_params_len);
    }

    pm_entry_t *from = top_entry();
    call_pause(from);
    if (from) {
        from->state = ST_PAUSED;
    }

    s_top++;
    pm_entry_t *to = &s_stack[s_top];
    to->page = page;
    to->state = ST_ENTERING;
    to->obj = entry_spawn(page);
    call_enter(to);

    ESP_LOGI(TAG, "[NAV] PUSH %s -> %s (depth=%d)",
             from && from->page ? from->page->name : "-", page->name, s_top + 1);

    s_nav.from = from;
    s_nav.to = to;
    pm_trans_ctx_t tc = {
        .from = (from && from->obj) ? from->obj : NULL,
        .to = to->obj,
        .dir = PM_DIR_PUSH,
        .type = (pm_transition_t)page->transition,
        .on_done = push_done,
    };
    if (!pm_trans_run(&tc)) {
        push_done(NULL);   /* 理论不可达（busy 已前置检查）*/
    }
}

/* pop 共体：弹到 idx（含销毁中间层）；要求 !pm_trans_busy() */
static void pop_to_idx(int idx)
{
    pm_entry_t *from = top_entry();
    pm_entry_t *to = &s_stack[idx];

    s_params_len = 0;   /* 上一页残留入参作废 */
    call_pause(from);
    from->state = ST_LEAVING;

    /* 中间层（idx+1..s_top-1）：HIDDEN 无动画，直接处置 */
    for (int i = s_top - 1; i > idx; i--) {
        if ((pm_cache_policy_t)s_stack[i].page->cache == PM_CACHE_KEEP_ALIVE) {
            entry_to_cache(&s_stack[i]);
        } else {
            entry_destroy(&s_stack[i]);
        }
    }

    /* to 复显（DESTROY_ON_LEAVE 被拆过则重建） */
    if (!to->obj) {
        to->obj = to->page->create(s_content, to->page->user_data);
        /* 重建对象排在退场页之上，POP 须让 to 从其下方滑入露出：
         * 挪到 from 正下方 */
        if (from->obj) {
            lv_obj_move_to_index(to->obj, lv_obj_get_index(from->obj));
        }
    } else {
        page_show_onscreen(to->obj);
    }
    to->state = ST_ENTERING;
    call_enter(to);

    ESP_LOGI(TAG, "[NAV] POP %s -> %s (depth=%d)",
             from->page->name, to->page->name, idx + 1);

    s_nav.from = from;
    s_nav.to = to;
    pm_trans_ctx_t tc = {
        .from = from->obj,
        .to = to->obj,
        .dir = PM_DIR_POP,
        .type = (pm_transition_t)from->page->transition,
        .on_done = pop_done,
    };
    if (!pm_trans_run(&tc)) {
        pop_done(NULL);
    }
}

void pm_pop(void)
{
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_NAV_POP, NULL, 0, NULL, 0);
        return;
    }
    pop_impl();
}

static void pop_impl(void)
{
    if (!s_content) {
        return;
    }
    if (pm_trans_busy()) {
        ESP_LOGW(TAG, "[NAV] pop ignored (transitioning)");
        return;
    }
    if (s_top <= 0) {
        ESP_LOGI(TAG, "[NAV] root page protected (no pop)");
        return;
    }
    pop_to_idx(s_top - 1);
}

bool pm_pop_to(const page_t *page)
{
    if (!page || !s_content) {
        return false;
    }
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_NAV_POP_TO, page, 0, NULL, 0);
        return true;   /* 已受理；目标不在栈内则执行时记日志忽略 */
    }
    return pop_to_impl(page);
}

static bool pop_to_impl(const page_t *page)
{
    if (pm_trans_busy()) {
        pm_trans_finish_now();   /* 语义操作：先收敛在途转场 */
    }
    int idx = -1;
    for (int i = 0; i <= s_top; i++) {
        if (s_stack[i].page == page) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return false;
    }
    if (idx == s_top) {
        return true;   /* 已是当前页 */
    }
    if (idx == 0) {
        pop_to_root_impl();
        return true;
    }
    pop_to_idx(idx);
    return true;
}

void pm_pop_to_root(void)
{
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_NAV_POP_TO_ROOT, NULL, 0, NULL, 0);
        return;
    }
    pop_to_root_impl();
}

static void pop_to_root_impl(void)
{
    if (!s_content || s_top <= 0) {
        return;
    }
    if (pm_trans_busy()) {
        pm_trans_finish_now();
    }
    pop_to_idx(0);
}

void pm_replace(const page_t *page)
{
    if (!page || !s_content) {
        ESP_LOGE(TAG, "invalid replace");
        return;
    }
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_NAV_REPLACE, page, 0, NULL, 0);
        return;
    }
    replace_impl(page);
}

static void replace_impl(const page_t *page)
{
    if (pm_trans_busy()) {
        ESP_LOGW(TAG, "[NAV] replace '%s' ignored (transitioning)", page->name);
        return;
    }
    pm_entry_t *from = top_entry();
    if (!from || !from->page) {
        push_impl(page, NULL, 0);   /* 空栈等价 push */
        return;
    }
    if (from->page == page) {
        return;   /* 已在此页 */
    }

    /* 新根临时占栈顶+1 槽，完成回调里落回 from 槽 */
    if (s_top >= PM_MAX_DEPTH - 1) {
        ESP_LOGE(TAG, "stack overflow, cannot replace %s", page->name);
        return;
    }
    call_pause(from);
    from->state = ST_LEAVING;

    s_params_len = 0;   /* replace 暂无参数入口（需要时加 pm_replace_params） */

    pm_entry_t *to = &s_stack[s_top + 1];   /* 临时槽 */
    to->page = page;
    to->state = ST_ENTERING;
    to->obj = entry_spawn(page);
    call_enter(to);

    ESP_LOGI(TAG, "[NAV] REPLACE %s -> %s", from->page->name, page->name);

    s_nav.from = from;
    s_nav.to = to;
    s_nav.from_slot = from;   /* 完成后 to 落回 from 槽，s_top 不变 */
    pm_trans_ctx_t tc = {
        .from = from->obj,
        .to = to->obj,
        .dir = PM_DIR_PUSH,
        .type = (pm_transition_t)page->transition,
        .on_done = swap_done,
    };
    if (!pm_trans_run(&tc)) {
        swap_done(NULL);
    }
}

void pm_home(void)
{
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_NAV_HOME, NULL, 0, NULL, 0);
        return;
    }
    home_impl();
}

static void home_impl(void)
{
    if (pm_trans_busy()) {
        pm_trans_finish_now();
    }
    if (s_cfg && s_cfg->tabs && s_cfg->n_tabs > 0) {
        switch_tab_impl(0);   /* switch_tab 清栈回根 */
    } else {
        pop_to_root_impl();
    }
}

bool pm_back(void)
{
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_NAV_BACK, NULL, 0, NULL, 0);
        return true;   /* 已受理（跨线程无同步结果，返回链异步执行） */
    }
    return back_impl();
}

static bool back_impl(void)
{
    /* 1. 键盘弹出 → 收起 */
    if (ui_keyboard_active()) {
        ui_keyboard_hide();
        return true;
    }
    /* 2. Modal 开着 → 关最顶 Modal */
    if (ui_modal_is_open()) {
        ui_modal_close_top();
        return true;
    }
    /* 3. 页面自定义拦截 */
    pm_entry_t *t = top_entry();
    if (t && t->page && t->page->on_back) {
        if (t->page->on_back(t->page->user_data)) {
            return true;
        }
    }
    /* 4. 栈 pop */
    if (s_top > 0) {
        pop_impl();
        return true;
    }
    /* 5. 根页保护 */
    ESP_LOGI(TAG, "[NAV] back at root, ignored");
    return false;
}

void pm_back_cb(lv_event_t *e)
{
    (void)e;
    pop_impl();   /* 事件回调恒在 LVGL 上下文 */
}

bool pm_is_transitioning(void)
{
    return pm_trans_busy();
}

const void *pm_params(void)
{
    return s_params_len ? s_params : NULL;
}

size_t pm_params_len(void)
{
    return s_params_len;
}

void pm_set_anim_enabled(bool on)
{
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_PM_ANIM, NULL, on != 0, NULL, 0);
        return;
    }
    pm_trans_set_enabled(on);
}

const char *pm_current(void)
{
    pm_entry_t *t = top_entry();
    return (t && t->page) ? t->page->name : NULL;
}

void pm_dump_stack(void)
{
    printf("[STACK] depth=%d cur_tab=%d\n", s_top + 1, s_cur_tab);
    for (int i = 0; i <= s_top; i++) {
        printf("  [%d] %-14s st=%-8s obj=%s\n", i,
               s_stack[i].page ? s_stack[i].page->name : "(rebuilt)",
               st_name(s_stack[i].state),
               s_stack[i].obj ? "y" : "-");
    }
    int n = 0;
    for (int i = 0; i < PM_CACHE_MAX; i++) {
        if (s_cache[i].page) {
            n++;
        }
    }
    printf("[STACK] cache=%d/%d\n", n, PM_CACHE_MAX);
}

/* ── 应用私有扩展（舰队同步合并保留）── */

lv_obj_t *pm_page_slot(void)
{
    return s_content;
}

/* App.c 全局 1s tick（app_printer_ui_tick）落点：按栈顶页分发刷新。
 * 各页 on_pause 已停自身 timer，覆盖页/缓存页不在此刷；
 * *_ui_refresh() 内部均有 root 有效性守卫。 */
void pm_refresh_printer_ui(void)
{
    pm_entry_t *t = top_entry();
    const page_t *p = t ? t->page : NULL;
    if (p == &page_home) {
        home_ui_refresh();
    } else if (p == &page_job) {
        job_ui_refresh();
    } else if (p == &page_history) {
        history_ui_refresh();
    }
}

/* ──────────────────── Tab 栏 ──────────────────── */

static void tab_set_selected(int tab)
{
    if (!s_cfg || !s_cfg->tabs) {
        return;
    }
    for (size_t i = 0; i < s_cfg->n_tabs; i++) {
        bool sel = ((int)i == tab);
        if (s_tab_btns[i]) {
            lv_obj_set_style_bg_color(s_tab_btns[i], sel ? lv_color_mix(UI_COLOR_ACCENT, UI_COLOR_BG_BAR, 28) : UI_COLOR_BG_BAR,
                                      0);
            lv_obj_set_style_bg_opa(s_tab_btns[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(s_tab_btns[i], 0, 0);
            lv_obj_set_style_shadow_width(s_tab_btns[i], 0, 0);
        }
        if (s_tab_icons[i]) {
            lv_obj_set_style_text_color(s_tab_icons[i], sel ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SEC,
                                        0);
        }
        if (s_tab_labels[i]) {
            lv_obj_set_style_text_color(s_tab_labels[i], sel ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SEC,
                                        0);
        }
        if (s_tab_indicators[i]) {
            lv_obj_set_style_bg_opa(s_tab_indicators[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(s_tab_indicators[i], UI_COLOR_ACCENT, 0);
        }
    }
}

/* 建根（shell_init 首屏/无转场场景：瞬时进入） */
static void root_build_instant(void)
{
    const page_t *root = cur_root_page();
    if (!root) {
        return;
    }
    s_top = 0;   /* 根页占栈底（关键：s_top 从 -1 归位，否则首 push 覆盖根槽） */
    pm_entry_t *e = &s_stack[0];
    e->page = root;
    e->state = ST_ACTIVE;
    e->obj = entry_spawn(root);
    call_enter(e);
    call_resume(e);
    tab_set_selected(s_cur_tab);
    ESP_LOGI(TAG, "[PAGE] root: %s", root->name);
}

/* 清整栈（立即，无动画；shell 重建/reload 用） */
static void stack_teardown(void)
{
    while (s_top >= 0) {
        entry_destroy(&s_stack[s_top]);
        s_top--;
    }
    memset(s_stack, 0, sizeof(s_stack));
}

/* Tab 换根（转场版，switch_tab 用）：
 * 旧根转场退场，新根复用缓存或新建，完成后旧根按其策略处置 */
static void tab_switch_root(int tab)
{
    pm_dir_t dir = (tab > s_cur_tab) ? PM_DIR_TAB_NEXT : PM_DIR_TAB_PREV;
    pm_entry_t *from = &s_stack[0];
    const page_t *new_root = s_cfg->tabs[tab].page;

    call_pause(from);
    from->state = ST_LEAVING;

    /* 新根临时占槽 1（栈此时已清空，s_top 归 -1 后重置） */
    s_top = 0;   /* from 仍占槽 0 展示退场；to 用槽 1 */
    pm_entry_t *to = &s_stack[1];
    to->page = new_root;
    to->state = ST_ENTERING;
    to->obj = entry_spawn(new_root);
    call_enter(to);

    s_cur_tab = tab;
    tab_set_selected(tab);

    ESP_LOGI(TAG, "[NAV] TAB%d %s -> %s", tab,
             from->page ? from->page->name : "-", new_root->name);

    s_nav.from = from;
    s_nav.to = to;
    s_nav.from_slot = from;   /* 完成后新根落回槽 0 */
    pm_trans_ctx_t tc = {
        .from = from->obj,
        .to = to->obj,
        .dir = dir,
        /* A full-page blend blocks input noticeably on the F101 LCD path. */
        .type = PM_TRANS_NONE,
        .on_done = swap_done,
    };
    if (!pm_trans_run(&tc)) {
        swap_done(NULL);
    }
}

void pm_switch_tab(int tab)
{
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_NAV_TAB, NULL, tab, NULL, 0);
        return;
    }
    switch_tab_impl(tab);
}

static void switch_tab_impl(int tab)
{
    if (!s_cfg || !s_cfg->tabs || !s_content) {
        return;
    }
    if (tab < 0 || tab >= (int)s_cfg->n_tabs) {
        return;
    }
    if (pm_trans_busy()) {
        pm_trans_finish_now();   /* 语义操作：不被丢弃 */
    }
    ui_loading_hide();           /* 扫描 loading 挂 top 层，切 Tab 必须收起 */
    if (tab == s_cur_tab) {
        /* 重击当前 Tab：仅清二级栈回根（不重建根页，保留根页状态） */
        pop_to_root_impl();
        return;
    }

    /* 清二级栈（立即处置） */
    while (s_top > 0) {
        pm_entry_t *e = &s_stack[s_top];
        if ((pm_cache_policy_t)e->page->cache == PM_CACHE_KEEP_ALIVE) {
            entry_to_cache(e);
        } else {
            entry_destroy(e);
        }
        s_top--;
    }

    if (s_top < 0) {
        /* 无根（异常兜底） */
        s_top = 0;
        s_stack[0] = (pm_entry_t){ 0 };
        s_stack[0].page = cur_root_page();
    }
    tab_switch_root(tab);
}

void pm_reload(void)
{
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_PM_RELOAD, NULL, 0, NULL, 0);
        return;
    }
    reload_impl();
}

static void reload_impl(void)
{
    if (!s_content || s_top < 0) {
        return;
    }
    if (pm_trans_busy()) {
        pm_trans_finish_now();
    }
    lv_display_t *disp = NULL;
    bulk_ui_begin(&disp);
    dismiss_chrome_overlays();
    /* Tab 栏文案跟随语言（name 保持中文键，串口日志不变） */
    if (s_cfg && s_cfg->tabs) {
        for (size_t i = 0; i < s_cfg->n_tabs; i++) {
            if (s_tab_labels[i]) {
                lv_label_set_text(s_tab_labels[i], tr(s_cfg->tabs[i].name));
            }
        }
    }
    cache_clear();       /* 缓存页文案已过期 */
    stack_teardown();
    root_build_instant();
    bulk_ui_end(disp);
}

/* ──────────────────── 语言原地重译（产品级路径） ──────────────────── */

void pm_retranslate(void)
{
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_PM_RETRANSLATE, NULL, 0, NULL, 0);
        return;
    }
    retranslate_impl();
}

static void retranslate_impl(void)
{
    if (!s_cfg || s_top < 0) {
        return;
    }
    if (pm_trans_busy()) {
        pm_trans_finish_now();   /* 语义操作：先收敛在途转场 */
    }

    /* 1) 静态文案标签（tr_bind 注册表；缓存页/被覆盖页对象都活着，一并更新） */
    size_t n = i18n_retranslate_all();

    /* 2) Tab 栏文案（name 恒为中文键） */
    if (s_cfg->tabs) {
        for (size_t i = 0; i < s_cfg->n_tabs; i++) {
            if (s_tab_labels[i]) {
                lv_label_set_text(s_tab_labels[i], tr(s_cfg->tabs[i].name));
            }
        }
    }

    statusbar_refresh();

    /* 3) 动态文案钩子：栈内页（含被覆盖）+ KEEP_ALIVE 缓存页 */
    int hooks = 0;
    for (int i = 0; i <= s_top; i++) {
        if (s_stack[i].page && s_stack[i].page->on_language) {
            s_stack[i].page->on_language(s_stack[i].page->user_data);
            hooks++;
        }
    }
    for (int i = 0; i < PM_CACHE_MAX; i++) {
        if (s_cache[i].page && s_cache[i].page->on_language) {
            s_cache[i].page->on_language(s_cache[i].page->user_data);
            hooks++;
        }
    }

    ESP_LOGI(TAG, "[I18N] retranslate in-place: %u labels, %d hooks, "
             "stack=%d (no rebuild)", (unsigned)n, hooks, s_top + 1);
}

/* ──────────────────── 页面查询 / 观测 ──────────────────── */

bool pm_page_is_top(const page_t *page)
{
    pm_entry_t *t = top_entry();
    return page && t && t->page == page;
}

void pm_stats(void)
{
    int cache_n = 0;
    for (int i = 0; i < PM_CACHE_MAX; i++) {
        if (s_cache[i].page) {
            cache_n++;
        }
    }
    ESP_LOGI(TAG, "[STAT] depth=%d tab=%d cache=%d watch=%u pend=%u binds=%u",
             s_top + 1, s_cur_tab, cache_n,
             (unsigned)bridge_watch_count(), (unsigned)ui_dispatch_pending(),
             (unsigned)i18n_bind_count());
}

/* ──────────────────── 全屏 chrome 控制 ──────────────────── */

void pm_set_chrome_visible(bool visible)
{
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_PM_CHROME, NULL, visible != 0, NULL, 0);
        return;
    }
    set_chrome_impl(visible);
}

static void set_chrome_impl(bool visible)
{
    /* LVGL 布局跳过 HIDDEN 对象：隐藏后内容区(flex_grow=1)自动占满全屏 */
    if (s_statusbar_obj) {
        if (visible) {
            lv_obj_clear_flag(s_statusbar_obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_statusbar_obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_tab_bar_obj) {
        if (visible) {
            lv_obj_clear_flag(s_tab_bar_obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_tab_bar_obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ──────────────────── Tab 栏构建（样式与旧版一致） ──────────────────── */

static void on_tab_click(lv_event_t *e)
{
    int tab = (int)(intptr_t)lv_event_get_user_data(e);
    if ((tab == 1 || tab == 3) && tab != s_cur_tab) {
        home_open_media(tab == 3);
        return;
    }
    pm_switch_tab(tab);
}

static lv_obj_t *create_tab_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    const bool rail = ui_screen_is_landscape();
    lv_obj_set_size(bar, rail ? 72 : LV_PCT(100), rail ? LV_PCT(100) : ui_land_tab_bar_h());
    lv_obj_set_style_bg_color(bar, UI_COLOR_BG_BAR, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_top(bar, 2, 0);
    lv_obj_set_style_pad_bottom(bar, 6, 0);
    lv_obj_set_style_pad_hor(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_side(bar, rail ? LV_BORDER_SIDE_RIGHT : LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_flex_flow(bar, rail ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    const lv_font_t *tab_caption_font = rail || s_cfg->n_tabs > 5 ? UI_FONT_CAPTION : UI_FONT_BODY;

    for (size_t order = 0; order < s_cfg->n_tabs; order++) {
        static const uint8_t positions[6] = {0,1,5,3,2,4};
        size_t i = s_cfg->n_tabs == 6 ? positions[order] : order;
        lv_obj_t *btn = lv_obj_create(bar);
        lv_obj_set_size(btn, rail ? LV_PCT(100) : 0, rail ? 0 : LV_PCT(100));
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_style_bg_opa(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_pad_top(btn, 0, 0);
        lv_obj_set_style_pad_bottom(btn, 2, 0);
        lv_obj_set_style_pad_hor(btn, 0, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(btn, 3, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, on_tab_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *ind = lv_obj_create(btn);
        lv_obj_set_size(ind, rail ? 3 : 28, rail ? 20 : 3);
        if (rail) {
            lv_obj_add_flag(ind, LV_OBJ_FLAG_FLOATING);
            lv_obj_align(ind, LV_ALIGN_LEFT_MID, 2, 0);
        }
        lv_obj_set_style_radius(ind, 2, 0);
        lv_obj_set_style_bg_color(ind, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(ind, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ind, 0, 0);
        lv_obj_clear_flag(ind, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, s_cfg->tabs[i].icon);
        lv_obj_set_style_text_font(icon, UI_FONT_SYMBOL, 0);
        lv_obj_set_style_text_color(icon, UI_COLOR_TEXT_SEC, 0);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, tr(s_cfg->tabs[i].name));
        lv_obj_set_style_text_font(label, tab_caption_font, 0);
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

        s_tab_btns[i] = btn;
        s_tab_icons[i] = icon;
        s_tab_labels[i] = label;
        s_tab_indicators[i] = ind;
    }
    return bar;
}

/* ──────────────────── 应用外壳 ──────────────────── */

void pm_shell_init(lv_obj_t *screen, const pm_shell_cfg_t *cfg)
{
    if (!cfg) {
        ESP_LOGE(TAG, "shell cfg is NULL");
        return;
    }
    s_screen = screen;
    s_cfg = cfg;

    if (pm_trans_busy()) {
        pm_trans_finish_now();
    }

    lv_display_t *disp = NULL;
    bulk_ui_begin(&disp);
    dismiss_chrome_overlays();

    /* 配色切换重建外壳：先跑整栈 + 缓存的 destroy 收尾（timers 等），
     * 再删旧外壳（对象级联删除） */
    stack_teardown();
    cache_clear();
    pm_trans_detach_shell();
    if (s_shell) {
        lv_obj_delete(s_shell);
        s_shell = NULL;
    }
    s_content = NULL;
    s_top = -1;
    s_statusbar_obj = NULL;
    s_tab_bar_obj = NULL;
    memset(s_stack, 0, sizeof(s_stack));
    memset(s_tab_btns, 0, sizeof(s_tab_btns));
    memset(s_tab_icons, 0, sizeof(s_tab_icons));
    memset(s_tab_labels, 0, sizeof(s_tab_labels));
    memset(s_tab_indicators, 0, sizeof(s_tab_indicators));
    s_nav.from = s_nav.to = s_nav.from_slot = NULL;

    /* 外壳容器：纵向 状态栏 / 内容 / Tab栏 */
    lv_obj_t *shell = lv_obj_create(screen);
    s_shell = shell;
    lv_obj_set_size(shell, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(shell, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(shell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(shell, 0, 0);
    lv_obj_set_style_radius(shell, 0, 0);
    lv_obj_set_style_pad_all(shell, 0, 0);
    lv_obj_set_flex_flow(shell, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(shell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(shell, LV_SCROLLBAR_MODE_OFF);

    if (cfg->statusbar) {
        s_statusbar_obj = statusbar_create(shell, cfg->statusbar);
    }

    /* Portrait keeps bottom navigation. Landscape uses a fixed rail, leaving
     * the whole remaining height for the printer workspace. */
    lv_obj_t *workspace = lv_obj_create(shell);
    lv_obj_remove_style_all(workspace);
    lv_obj_set_size(workspace, LV_PCT(100), 0);
    lv_obj_set_flex_grow(workspace, 1);
    lv_obj_set_flex_flow(workspace, ui_screen_is_landscape() ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(workspace, LV_OBJ_FLAG_SCROLLABLE);
    if (ui_screen_is_landscape() && cfg->tabs && cfg->n_tabs)
        s_tab_bar_obj = create_tab_bar(workspace);
    s_content = lv_obj_create(workspace);
    lv_obj_set_size(s_content, ui_screen_is_landscape() ? 0 : LV_PCT(100),
                              ui_screen_is_landscape() ? LV_PCT(100) : 0);
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_style_min_height(s_content, 0, 0);
    lv_obj_set_style_bg_opa(s_content, 0, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_OFF);

    /* 转场引擎：挡板常驻页面容器（置顶吃转场期间的指针事件） */
    pm_trans_init(s_content);

    /* UI 编组层：命令环排水 timer + 导航执行器（pm_* 任意任务可调的地基） */
    ui_dispatch_init();
    ui_dispatch_set_nav_exec(nav_exec);

    if (cfg->tabs && cfg->n_tabs > 0) {
        if (cfg->n_tabs > PM_MAX_TABS) {
            ESP_LOGW(TAG, "n_tabs %u > max %d, truncated",
                     (unsigned)cfg->n_tabs, PM_MAX_TABS);
        }
        if (!ui_screen_is_landscape()) s_tab_bar_obj = create_tab_bar(workspace);
    }

    if (s_cur_tab >= (int)cfg->n_tabs) {
        s_cur_tab = 0;
    }
    root_build_instant();
    ui_theme_apply_lvgl();
    bulk_ui_end(disp);
    ESP_LOGI(TAG, "shell rebuilt (theme=%u tab=%d)",
             (unsigned)ui_theme_get_mode(), s_cur_tab);
}

void pm_reload_shell(void)
{
    if (!ui_in_ui_ctx()) {
        post_nav(UI_OP_PM_RELOAD_SHELL, NULL, 0, NULL, 0);
        return;
    }
    reload_shell_impl();
}

static void reload_shell_impl(void)
{
    if (!s_screen || !s_cfg) {
        return;
    }
    pm_shell_init(s_screen, s_cfg);   /* 内部整栈+缓存+旧外壳销毁后重建 */
}
