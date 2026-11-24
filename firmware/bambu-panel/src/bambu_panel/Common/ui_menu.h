/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 创意动画菜单组件库（ui_m_*）
 *
 * 在 ui_w_* 视觉组件之上提供"动效菜单"组合件：
 *   辐轮扇区菜单(pie) / 旋钮选择器(dial) / 无限循环轮播(loop) /
 *   中心聚焦列表(focus) / 鱼眼程序坞(dock) / 封面流转(flow) /
 *   入场编排(stagger)。
 *
 * 每控件四件套：ui_m_<w>() 创建 + _set(选中,可动画) + _get + _on_sel(回调)。
 * out-struct 由调用方持有（页面生命周期）；root 的 user_data 回指 out。
 *
 * 约束：
 *  - 共享代码禁用 lv_scale（模拟器 LV_USE_SCALE=0），刻度用 lv_line；
 *  - 禁任意角旋转（P4 无加速路径），径向感用极坐标 translate 表达；
 *  - 缩放只经 transform_scale_x/y(256=100%) 且仅打在少数可见项（≤5），
 *    上限 300；ui_m_set_scale_fx(false) 一键全局降级为 translate+opa；
 *  - 阴影昂贵一律不用；时长只用 PM_T_DUR_* 令牌；
 *  - overshoot 仅保留给"弹出"（pie 选中/dock 回弹）。
 */
#pragma once

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────── 容量 ──────────────────── */
#define UI_M_PIE_MAX        8     /* 辐轮扇区数（2..8） */
#define UI_M_DIAL_TICKS_MAX 13    /* 旋钮刻度线数（含首尾） */
#define UI_M_LOOP_MAX       6     /* 循环轮播真实条目数（2..6，内部克隆 3 份） */
#define UI_M_FOCUS_MAX      16    /* 聚焦列表行数 */
#define UI_M_DOCK_MAX       10    /* 程序坞图标数 */
#define UI_M_FLOW_MAX       12    /* 封面流转卡数 */

/* ──────────────────── 通用 ──────────────────── */

/** 一个菜单条目：symbol 用 LV_SYMBOL_*（UI_FONT_SYMBOL 渲染，NULL 可）；
 *  zh 为 i18n 中文键（创建即 tr()+tr_bind()）；zh_sub 副标题键，NULL 可 */
typedef struct {
    const char *symbol;
    const char *zh;
    const char *zh_sub;
} ui_m_item_t;

/** 选中回调：widget = 各控件 root；programmatic _set 与交互选中都会触发 */
typedef void (*ui_m_sel_cb_t)(lv_obj_t *widget, int index, void *user_data);

/** 全局缩放特效开关（默认 true）。设备性能不足时置 false：
 *  所有 scale 派生量退化为 translate+opa（focus/loop/flow/dock/弹出） */
void ui_m_set_scale_fx(bool on);

/* ──────────────────── 入场编排（任意容器通用） ──────────────────── */

/** 子对象逐个入场：第 i 个延迟 i*step_ms，translate_y dy→0 + opa 0→255，
 *  ease_out。调用后立即生效首帧姿态（无终态闪帧）。
 *  仅用于"子对象自身不持有 translate/scale 样式"的普通容器（如卡片列表）。
 *  建议 dy=24 / step=20 / dur=PM_T_DUR_NORMAL */
void ui_m_stagger(lv_obj_t *container, int32_t dy, uint16_t step_ms, uint16_t dur);

/* ──────────────────── 1) 辐轮扇区菜单 ui_m_pie ────────────────────
 * N 个 lv_arc 楔形环段围成一圈，中心 hub 显示选中项文字。
 * 按住拖动 = 手指所在扇区实时高亮；轻点 = 选中。选中扇区沿径向弹出。
 * 楔内只放 symbol（CJK 在 n>=6 放不下），中文住中心 hub。 */

typedef struct {
    lv_obj_t *root;                          /* 顶层容器（回传给回调） */
    lv_obj_t *overlay;                       /* 全幅透明输入层（PRESSING 跟踪） */
    lv_obj_t *hub;                           /* 中心圆 */
    lv_obj_t *hub_title, *hub_sub;           /* hub 标题/副题（随选择 tr_bind 换键） */
    lv_obj_t *sectors[UI_M_PIE_MAX];         /* lv_arc 楔形环段 */
    int       n, sel;                        /* 扇区数 / 选中下标 */
    lv_point_t press_pt;                     /* 按压起点（tap/drag 判定） */
    int       pend;                          /* 滞回候选下标 */
    uint8_t   pend_cnt;
    const ui_m_item_t *items;                /* 调用方持有，不拷贝 */
    ui_m_sel_cb_t cb;
    void     *cb_ud;
} ui_m_pie_t;

/** @param size 外径（220..360，居中布点）；items/n 可 NULL/0（空壳） */
void ui_m_pie(lv_obj_t *parent, lv_color_t accent, int32_t size,
              const ui_m_item_t *items, int n, ui_m_pie_t *out);
bool ui_m_pie_set(ui_m_pie_t *m, int sel, bool animate);   /* 越界/同值返回 false */
int  ui_m_pie_get(const ui_m_pie_t *m);
void ui_m_pie_on_sel(ui_m_pie_t *m, ui_m_sel_cb_t cb, void *user_data);

/* ──────────────────── 2) 旋钮选择器 ui_m_dial ────────────────────
 * 270° lv_arc + lv_line 刻度。双模式：set_range 连续值（量化到 step）/
 * set_items 离散档位（后调者生效，items 优先）。按住拖动逐档走，
 * 每档 voice_touch_click + 中心文字交叉淡化。 */

typedef struct {
    lv_obj_t *root, *arc;
    lv_obj_t *val_lbl;                        /* 中心大数字/序号 */
    lv_obj_t *lbl_a, *lbl_b;                  /* 离散模式文字交叉淡化双子 */
    lv_obj_t *tick_ln[UI_M_DIAL_TICKS_MAX];   /* lv_line 刻度线 */
    lv_point_precise_t tick_pts[UI_M_DIAL_TICKS_MAX * 2];  /* 刻度坐标（out 持有=lv_line 安全） */
    int32_t   min, max, step, val;            /* 连续模式参数/当前值 */
    const ui_m_item_t *items;                 /* 离散模式条目（NULL=连续） */
    int       n_items, sel;
    int8_t    lbl_show;                       /* 交叉淡化当前显示位：0=lbl_a 1=lbl_b */
    ui_m_sel_cb_t cb;
    void     *cb_ud;
} ui_m_dial_t;

void  ui_m_dial(lv_obj_t *parent, lv_color_t accent, int32_t size, ui_m_dial_t *out);
void  ui_m_dial_set_range(ui_m_dial_t *m, int32_t min, int32_t max, int32_t step);
void  ui_m_dial_set_items(ui_m_dial_t *m, const ui_m_item_t *items, int n);
/** 连续：v 量程内取值；离散：v=下标。钳到合法域；同值返回 false */
bool  ui_m_dial_set_value(ui_m_dial_t *m, int32_t v, bool animate);
int32_t ui_m_dial_get_value(const ui_m_dial_t *m);
int   ui_m_dial_get_index(const ui_m_dial_t *m);   /* 离散选中；连续 -1 */
void  ui_m_dial_on_sel(ui_m_dial_t *m, ui_m_sel_cb_t cb, void *user_data);

/* ──────────────────── 3) 无限循环轮播 ui_m_loop ────────────────────
 * 2..6 条目 × 3 份克隆横排 + snap 居中；滚出中段即"无声换页"跳回
 * （精确周期 → 像素级同帧，不可见）。向一个方向甩多久都到不了头。 */

typedef struct {
    lv_obj_t *root;                          /* 固定宽视口（snap CENTER） */
    lv_obj_t *cells[UI_M_LOOP_MAX * 3];      /* 克隆单元格（含真实 label） */
    int       n, sel;                        /* 真实条目数 / 选中（mod n） */
    int32_t   pitch;                         /* cell 宽 + 间距 */
    int32_t   last_sx;                       /* 风暴早退 */
    uint8_t   seeded;                        /* 初次居中完成 */
    const ui_m_item_t *items;
    ui_m_sel_cb_t cb;
    void     *cb_ud;
} ui_m_loop_t;

void ui_m_loop(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
               int n, ui_m_loop_t *out);
bool ui_m_loop_set(ui_m_loop_t *m, int sel, bool animate);
int  ui_m_loop_get(const ui_m_loop_t *m);
void ui_m_loop_on_sel(ui_m_loop_t *m, ui_m_sel_cb_t cb, void *user_data);

/* ──────────────────── 4) 中心聚焦列表 ui_m_focus ────────────────────
 * 纵向列表 + snap 居中；行按离视口中心的距离连续缩放/淡化/右缩，
 * 居中行为选中（badge 满色/标题亮色）。Wear OS ScalingLazyColumn 风格。 */

typedef struct {
    lv_obj_t *root;                          /* 固定高视口（snap CENTER 纵向） */
    lv_obj_t *rows[UI_M_FOCUS_MAX];
    lv_obj_t *badges[UI_M_FOCUS_MAX];
    lv_obj_t *titles[UI_M_FOCUS_MAX];
    uint8_t   far_state[UI_M_FOCUS_MAX];     /* 远态缓存：避免重复设远样式 */
    int       n, sel;
    int32_t   vh, pad_top;                   /* 视口高 / 首尾补垫（解析式坐标用） */
    lv_color_t accent_v;                     /* 实现私有：创建时强调色快照 */
    const ui_m_item_t *items;
    ui_m_sel_cb_t cb;
    void     *cb_ud;
} ui_m_focus_t;

/** @param viewport_h 视口高（260..560；首尾补垫让任意行可达中心） */
void ui_m_focus(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
                int n, int32_t viewport_h, ui_m_focus_t *out);
bool ui_m_focus_set(ui_m_focus_t *m, int sel, bool animate);  /* 滚到中心 */
int  ui_m_focus_get(const ui_m_focus_t *m);
void ui_m_focus_on_sel(ui_m_focus_t *m, ui_m_sel_cb_t cb, void *user_data);

/* ──────────────────── 5) 鱼眼程序坞 ui_m_dock ────────────────────
 * 不可滚图标条；手指在哪，哪片图标按余弦曲线放大并推开邻标（Bederson
 * 鱼眼），松手弹簧回位。轻点图标 = 选中 + 脉冲。 */

typedef struct {
    lv_obj_t *root;                          /* 固定高条带（不可滚动） */
    lv_obj_t *icons[UI_M_DOCK_MAX];
    int32_t   rest_x[UI_M_DOCK_MAX];         /* 静息中心 x（相对条带） */
    int       n, sel;
    lv_point_t press_pt;                     /* 按压起点（tap/drag 判定） */
    uint8_t   rest_ok;                       /* rest_x 已按实测宽初始化 */
    lv_color_t pal_v[5];                     /* 实现私有：图标循环调色板快照 */
    const ui_m_item_t *items;
    ui_m_sel_cb_t cb;
    void     *cb_ud;
} ui_m_dock_t;

void ui_m_dock(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
               int n, ui_m_dock_t *out);
bool ui_m_dock_set(ui_m_dock_t *m, int sel, bool animate);   /* 高亮 + 脉冲 */
int  ui_m_dock_get(const ui_m_dock_t *m);
void ui_m_dock_on_sel(ui_m_dock_t *m, ui_m_sel_cb_t cb, void *user_data);

/* ──────────────────── 6) 封面流转 ui_m_flow ────────────────────
 * 横向卡片流 + snap 居中；侧卡缩小/淡化/下沉（二次 y 弧）营造伪 3D。
 * 卡片不重叠（LVGL 子 z 序固定，重叠需逐帧 move_to_index 会闪）。 */

typedef struct {
    lv_obj_t *root;
    lv_obj_t *cards[UI_M_FLOW_MAX];
    int       n, sel;
    int32_t   pitch, last_sx;
    uint8_t   seeded;                        /* 初次居中完成 */
    lv_color_t accent_v;                     /* 实现私有：创建时强调色快照 */
    const ui_m_item_t *items;
    ui_m_sel_cb_t cb;
    void     *cb_ud;
} ui_m_flow_t;

void ui_m_flow(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
               int n, ui_m_flow_t *out);
bool ui_m_flow_set(ui_m_flow_t *m, int sel, bool animate);
int  ui_m_flow_get(const ui_m_flow_t *m);
void ui_m_flow_on_sel(ui_m_flow_t *m, ui_m_sel_cb_t cb, void *user_data);

/* ──────────────────── 第二批（效果件 + 三控件） ──────────────────── */

/* ── 通用效果件：可挂到任意对象 / 独立一次性 ── */

/** 按下涟漪：给 obj 挂一次（Material 风格），每次 PRESSED 从触点扩一圈
 *  淡出圆后自删（≤4 个并发）。实现用 set_size 逐帧重定位，无 transform */
void ui_m_ripple(lv_obj_t *obj);

/** 按压果冻：给 obj 挂一次，PRESSED 压扁(scale_x↓/y↑, 底心 pivot)，
 *  松手 overshoot 弹回。挂接后该对象 transform_scale 样式归本件管理 */
void ui_m_squish(lv_obj_t *obj);

#define UI_M_ODO_MAX 6     /* 翻数器位数 */

/** 里程表式翻数器：每位 = clip 列 + 双十年条带，只前向滚动，
 *  进位跨带完成后无声回卷（与 loop 换页同一把戏，像素级同帧） */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *lbl[UI_M_ODO_MAX];   /* 每位竖条带 label（0..9 0..9） */
    int32_t   val;                 /* 当前值 */
    int       n;                   /* 位数 */
    int32_t   line_h;              /* 实现私有：字行高（断言可读） */
    int32_t   line[UI_M_ODO_MAX];  /* 实现私有：各条带当前行号（0..9，跨带回卷后） */
    uint32_t  max_val;             /* 10^n - 1 */
} ui_m_odo_t;

void    ui_m_odo(lv_obj_t *parent, int n_digits, ui_m_odo_t *out);
void    ui_m_odo_set(ui_m_odo_t *m, int32_t v, bool animate);   /* 钳到 0..max */
int32_t ui_m_odo_get(const ui_m_odo_t *m);

/** 一次性彩带：在 parent 坐标 (x,y) 迸发 ≤12 个短命粒子（抛物线+尾部
 *  淡出，结束自删）。700ms 为粒子特效有意例外时长（同 loop 原生滚动） */
void ui_m_confetti(lv_obj_t *parent, int32_t x, int32_t y);

/* ──────────────────── 7) 辐射弹出菜单 ui_m_burst ────────────────────
 * 中心圆键按住弹出：图标沿极角错峰弹簧飞出（Path/FAB 风格），
 * 点图标 = 选中 + 收拢；再点中心键未选也收拢。收起时零常驻开销。 */

#define UI_M_BURST_MAX 8

typedef struct {
    lv_obj_t *root, *hub, *hub_sym;         /* 中心键 + 图标(+/-) */
    lv_obj_t *icons[UI_M_BURST_MAX];         /* 图标容器（圆+说明列） */
    lv_obj_t *backdrop;                      /* 展开时的兜底点击层（展开即建，收拢即删） */
    int       n, sel;
    uint8_t   is_open;
    const ui_m_item_t *items;
    ui_m_sel_cb_t cb;
    void     *cb_ud;
} ui_m_burst_t;

/** @param size 展开直径（200..420），图标环半径 = size/2-34 */
void ui_m_burst(lv_obj_t *parent, lv_color_t accent, int32_t size,
                const ui_m_item_t *items, int n, ui_m_burst_t *out);
void ui_m_burst_open(ui_m_burst_t *m, bool animate);
void ui_m_burst_close(ui_m_burst_t *m, bool animate);
bool ui_m_burst_is_open(const ui_m_burst_t *m);
bool ui_m_burst_set(ui_m_burst_t *m, int sel, bool animate);   /* 高亮选中（不自动展开） */
int  ui_m_burst_get(const ui_m_burst_t *m);
void ui_m_burst_on_sel(ui_m_burst_t *m, ui_m_sel_cb_t cb, void *user_data);

/* ──────────────────── 8) 老虎机滚轮 ui_m_slot ────────────────────
 * 竖向滚筒（3n 克隆 + snap 居中）：手指可拨，_set(animate=true) 触发
 * 转盘物理——加速扫格、长尾减速、逐格 voice 咔哒、目标格过冲回落定格。
 * 亮带装饰不随滚筒滚动（外层固定 root + 内层 scr）。 */

#define UI_M_SLOT_MAX 10

typedef struct {
    lv_obj_t *root, *scr, *band;             /* 固定外层 / 滚筒 / 选中亮带 */
    lv_obj_t *cells[UI_M_SLOT_MAX * 3];
    int       n, sel;
    int32_t   vh, pad;                       /* 视口高 / 首尾补垫 */
    int32_t   last_sy;                       /* 实现私有：滚动 2px 早退 */
    uint8_t   spinning;                      /* 自旋中：SCROLL_END 静默 */
    int32_t   spin_y0;                       /* 实现私有：wrap 锚（回卷基准 y） */
    int32_t   spin_y1;                       /* 实现私有：加速段终点 */
    int32_t   spin_end;                      /* 实现私有：定格 scroll_y */
    int32_t   tick_mark;                     /* 实现私有：上次咔哒格 */
    uint32_t  tick_ms;                       /* 实现私有：咔哒节流 */
    const ui_m_item_t *items;
    ui_m_sel_cb_t cb;
    void     *cb_ud;
} ui_m_slot_t;

void ui_m_slot(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
               int n, int32_t viewport_h, ui_m_slot_t *out);
/** animate=false 即时定格；true = 旋转落定（约 1.8s） */
bool ui_m_slot_set(ui_m_slot_t *m, int sel, bool animate);
int  ui_m_slot_get(const ui_m_slot_t *m);
void ui_m_slot_on_sel(ui_m_slot_t *m, ui_m_sel_cb_t cb, void *user_data);

/* ──────────────────── 9) 卡牌飞出 ui_m_deck ────────────────────
 * 顶层卡跟手横拖：过阈值飞出淡出、底层错峰上浮放大接力（循环补位，
 * 对象只建 3 张、推进时原地换文案——零创建销毁）。轻点 = 选中飞出。 */

#define UI_M_DECK_MAX 12

typedef struct {
    lv_obj_t *root;
    lv_obj_t *layers[3];                     /* 顶层/中层/底层（创建序 2,1,0 保 z 序） */
    lv_obj_t *titles[3], *subs[3], *syms[3]; /* 换文案所需句柄 */
    int       order[UI_M_DECK_MAX];          /* 队列：order[0] 为顶层 */
    int       n, sel;                        /* sel = 最近选中的项 */
    lv_point_t press_pt;
    uint8_t   busy;                          /* 实现私有：飞出动画中 */
    const ui_m_item_t *items;
    ui_m_sel_cb_t cb;
    void     *cb_ud;
} ui_m_deck_t;

void ui_m_deck(lv_obj_t *parent, lv_color_t accent, const ui_m_item_t *items,
               int n, ui_m_deck_t *out);
/** 程序化选中：把 idx 转到顶层（即时换序）+ 通知 */
bool ui_m_deck_set(ui_m_deck_t *m, int sel, bool animate);
int  ui_m_deck_get(const ui_m_deck_t *m);
void ui_m_deck_on_sel(ui_m_deck_t *m, ui_m_sel_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
