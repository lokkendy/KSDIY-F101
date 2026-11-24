/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 页面管理器（导航栈 / 生命周期 / 缓存 / 外壳）
 *
 * 外壳 = 顶部状态栏 + 中部页面栈 + 底部 Tab 栏（可选）。
 * Tab 根页占栈底 stack[0]，二级页面 pm_push 压栈；
 * 转场交由 page_transition 引擎（双页动画、方向对称、转场锁）。
 *
 * ── 兼容性（sync_fw.sh 整目录同步到 19 个应用）──
 * 旧 page_t{ name, create, destroy, user_data } 四字段与
 * pm_push/pm_pop/pm_pop_to_root/pm_switch_tab/pm_reload/pm_reload_shell/
 * pm_shell_init/pm_current/pm_back_cb 签名不变，旧页面零改动可用；
 * 生命周期/缓存/转场配置全部通过"新增字段 + 指定初始化默认值"提供。
 *
 * 生命周期顺序与缓存/转场规则详见 PAGE_MANAGER_DESIGN.md §5-§7。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "page_transition.h"
#include "statusbar.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_MAX_DEPTH  8       /* 导航栈深（含 Tab 根页）*/
#define PM_MAX_TABS   6
#define PM_CACHE_MAX  4       /* KEEP_ALIVE 弹出缓存上限（LRU）*/
#define PM_PARAMS_MAX 64      /* pm_push_params 值拷贝上限 */

/* ── 缓存策略（page_t.cache；默认 0 = 现行为） ── */
typedef enum {
    PM_CACHE_DESTROY_ON_POP = 0,  /* 栈内隐藏保留；弹出即销毁（默认）*/
    PM_CACHE_KEEP_ALIVE,          /* 弹出后进缓存表，复用时状态/滚动原样保留 */
    PM_CACHE_DESTROY_ON_LEAVE,    /* 被覆盖即销毁对象（大内存页），返回时重建 */
} pm_cache_policy_t;

/**
 * 页面描述符（= 注册机制：静态 const 单例，pm_push(&page_xxx) 引用）
 *
 * 新增字段全部可选：旧代码指定初始化未填项零初始化 →
 * cache=DESTROY_ON_POP、transition=SLIDE、allow_multiple=false、回调 NULL，
 * 行为与旧版完全一致。
 *
 * ── 线程安全 ──
 * 导航类 API（push/pop/pop_to 全家/replace/home/switch_tab/back/reload
 * 全系/retranslate/set_anim/set_chrome）任意任务可调：LVGL 任务内直接执行，
 * 否则经 ui_dispatch 命令环编组到 LVGL 任务按序执行（≤10ms）。
 * 查询类（pm_current/pm_params/pm_is_transitioning/pm_page_is_top/
 * pm_dump_stack）为即时读，跨任务调用请持 ksdiy_lvgl_lock 或在 LVGL 任务。
 */
typedef struct {
    const char *name;
    lv_obj_t *(*create)(lv_obj_t *parent, void *user_data);
    void (*destroy)(void *user_data);

    /* ── 可选生命周期（DESIGN §5； Presenter 在此同步 timer） ── */
    void (*on_enter)(void *user_data);   /* 每次进入：create 后 / 下层返回 */
    void (*on_resume)(void *user_data);  /* 成为顶层且转场完成 → 启动刷新 timer */
    void (*on_pause)(void *user_data);   /* 即将失去顶层（转场开始前）→ 停 timer */
    void (*on_leave)(void *user_data);   /* 彻底离开：销毁前 / 转入缓存前 */
    bool (*on_back)(void *user_data);    /* 返回拦截：true=已处理；false=默认 pop */
    void (*on_language)(void *user_data);/* 语言切换原地重译：动态文案在此重算
                                          * （静态文案交给 tr_bind 自动处理） */

    uint8_t cache       : 2;   /* pm_cache_policy_t */
    uint8_t transition  : 2;   /* pm_transition_t（PUSH/POP 方向由框架镜像） */
    uint8_t allow_multiple;    /* 0=重复 push 栈内已有页 → 自动 popTo 它 */

    void *user_data;
} page_t;

/** Tab 定义（name 为中文键, 经 tr() 翻译; icon 为 LV_SYMBOL_*） */
typedef struct {
    const char *name;
    const char *icon;
    const page_t *page;
} pm_tab_def_t;

/** 应用外壳配置（应用在 app_shell_cfg.c 里静态定义后传给 pm_shell_init） */
typedef struct {
    const pm_tab_def_t *tabs;           /* Tab 表; NULL = 无 Tab 栏单栈模式 */
    size_t n_tabs;
    const page_t *root_page;            /* 无 Tab 模式的主页 */
    const statusbar_cfg_t *statusbar;   /* NULL = 不建状态栏 */
} pm_shell_cfg_t;

/* ──────────────────── 外壳（兼容层，签名不变） ──────────────────── */

/** 创建应用外壳（状态栏 + 页面容器 + Tab 栏）并进入首页；LVGL 锁内调用 */
void pm_shell_init(lv_obj_t *screen, const pm_shell_cfg_t *cfg);

/** 语言切换后重建当前 Tab（须 LVGL 锁内、事件回调外调用） */
void pm_reload(void);

/** 配色切换后重建整个外壳（保持在原 Tab） */
void pm_reload_shell(void);

/** Tab 切换：清栈 → 转场换根页；无 Tab 模式下为空操作 */
void pm_switch_tab(int tab);

/** 全屏页 chrome 控制：显/隐状态栏与 Tab 栏（隐藏后内容区 flex 自动占满全屏；
 *  外壳重建会复位为显示，全屏页在 on_resume/on_create 里再次隐藏即可） */
void pm_set_chrome_visible(bool visible);

/* ──────────────────── 导航（兼容层，签名不变） ──────────────────── */

/** 压入二级页面（转场方向/类型取 page->transition；连点/转场中自动忽略） */
void pm_push(const page_t *page);

/** 弹出当前页面（PUSH 的严格反向动画）*/
void pm_pop(void);

/** 弹出所有二级页面，回到当前 Tab 根页 */
void pm_pop_to_root(void);

/** 当前页面名 */
const char *pm_current(void);

/** 通用返回回调（可直接挂到事件/按钮） */
void pm_back_cb(lv_event_t *e);

/* ──────────────────── 导航（新增能力） ──────────────────── */

/** 带参压栈：值拷贝 ≤PM_PARAMS_MAX 字节；页面 create/on_enter 里 pm_params() 读 */
void pm_push_params(const page_t *page, const void *data, size_t len);

/** 替换栈顶（Splash→Home / 登录→主界面）：旧顶转场后销毁 */
void pm_replace(const page_t *page);

/** 弹到栈内指定页（中间层无动画直拆）；不在栈内返回 false */
bool pm_pop_to(const page_t *page);

/** 快速回家：清栈 + 切回 Tab0（无 Tab 模式 = pop_to_root） */
void pm_home(void);

/**
 * 统一返回链（优先级：键盘 → Modal → 页面 on_back → 栈 pop → 根页忽略）
 * 硬件返回键 / 手势可接此函数；返回 false = 已在根页（调用方可选退出应用）
 */
bool pm_back(void);

/** 转场进行中？（转场期间 pm_push/pm_pop/pm_replace 请求被忽略） */
bool pm_is_transitioning(void);

/** 本次入参（仅页面 create/on_enter 执行期间有效，要长存请自行拷贝） */
const void *pm_params(void);
size_t pm_params_len(void);

/** 全局动画开关（false = 转场瞬时完成，生命周期顺序不变） */
void pm_set_anim_enabled(bool on);

/**
 * 语言切换原地重译（不重建页面，滚动/输入/导航栈全保留）：
 *   1. i18n 绑定注册表（tr_bind 的静态文案标签）原地重设
 *   2. Tab 栏 / 状态栏文案重译
 *   3. 栈内页(含被覆盖)与 KEEP_ALIVE 缓存页的 on_language 钩子
 * 调用前须已 i18n_set_lang() 新语言；任意任务可调（自动编组）。
 * pm_reload() 保留为兜底重建路径（兼容旧调用方）。
 */
void pm_retranslate(void);

/** 页面是当前栈顶？（DESIGN §5：替代页面手写 volatile alive 查询）*/
bool pm_page_is_top(const page_t *page);

/** 一行观测：栈深/缓存数/订阅数/命令环深/空闲堆（调试） */
void pm_stats(void);

/** [NAV]/[STACK] 调试输出（Release 可不调用） */
void pm_dump_stack(void);

/* ── 应用私有扩展（舰队同步合并保留）── */

/** 页面栈容器（模拟器布局审计用） */
lv_obj_t *pm_page_slot(void);

/** App.c 全局 1s tick 落点：栈顶为打印机数据页（首页/任务/历史）时分发刷新 */
void pm_refresh_printer_ui(void);

#ifdef __cplusplus
}
#endif
