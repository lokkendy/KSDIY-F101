/*
 * SPDX-FileCopyrightText: 2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * 矢量符号字体（替换 LVGL 内置 FontAwesome 实心字形）
 *
 * ── 为什么需要它 ────────────────────────────────────────────────
 * LVGL 的 LV_SYMBOL_* 全部来自 montserrat 字体里的 FontAwesome 私有区
 * (U+F000..U+F8FF)。这些字形是 2014 年那套**实心、笔画粗细不均、光学
 * 尺寸不统一**的图标，只要界面出现一个，立刻就能看出"这是 LVGL 做的"。
 *
 * 全部替换调用点不现实（本仓库 71 个工程共 5622 处引用）。本文件改用
 * **替换字形本身**：注册一个 lv_font_t 后端，把同一批码位渲染成我们自己
 * 的线条图标（16×16 设计网格、圆头笔画、统一视觉重量）。挂进主题字体的
 * fallback 链后，所有 LV_SYMBOL_* 自动变成线条图标，调用点零改动。
 *
 * ── 实现要点 ────────────────────────────────────────────────────
 *  · 字形按需光栅化：A8 覆盖率位图，4×4 超采样抗锯齿
 *  · 无堆分配、无额外缓冲：直接写 LVGL 传入的 draw_buf
 *  · 每个图形带包围盒，逐像素只测可能覆盖它的图形
 */
#include "ui_symbol_font.h"

#include <math.h>
#include <string.h>

#include "lvgl.h"

/* ──────────────────── 图形指令 ──────────────────── */

enum {
    OP_LINE = 1,   /* p0,p1 -> p2,p3 ; p4 = 线宽(半单位, 0=自适应) */
    OP_ARC,        /* p0,p1 圆心 ; p2 半径 ; p3=a0/3° ; p4=a1/3° ; p5 线宽 */
    OP_DOT,        /* p0,p1 圆心 ; p2 半径（实心圆） */
    OP_TRI,        /* p0..p5 三顶点（实心三角） */
    OP_RRECT,      /* p0,p1 左上 ; p2,p3 宽高 ; p4 圆角 ; p5 线宽 */
    OP_TLINE,      /* 实心梯形：p0,p1 顶边 ; p2,p3 底边（用四点凸包填充） */
};

/* 坐标单位 = 0.5（即 32 = 16.0 网格单位）；角度单位 = 3° */
typedef struct {
    uint8_t op;
    int8_t  p[6];
} sym_op_t;

typedef struct {
    uint8_t        n;
    const sym_op_t *ops;
} sym_glyph_t;

#define L(x1,y1,x2,y2)      { OP_LINE,  { (x1)*2, (y1)*2, (x2)*2, (y2)*2, 0, 0 } }
#define LW(x1,y1,x2,y2,w)   { OP_LINE,  { (x1)*2, (y1)*2, (x2)*2, (y2)*2, (w), 0 } }
#define A(cx,cy,r,a0,a1)    { OP_ARC,   { (cx)*2, (cy)*2, (r)*2, ((a0)/3), ((a1)/3), 0 } }
#define D(cx,cy,r)          { OP_DOT,   { (cx)*2, (cy)*2, (r)*2, 0, 0, 0 } }
#define T(x1,y1,x2,y2,x3,y3){ OP_TRI,   { (x1)*2, (y1)*2, (x2)*2, (y2)*2, (x3)*2, (y3)*2 } }
#define RR(x,y,w,h,r)       { OP_RRECT, { (x)*2, (y)*2, (w)*2, (h)*2, (r)*2, 0 } }
#define RRW(x,y,w,h,r,sw)   { OP_RRECT, { (x)*2, (y)*2, (w)*2, (h)*2, (r)*2, (sw) } }
#define TL(x1,y1,x2,y2)     { OP_TLINE, { (x1)*2, (y1)*2, (x2)*2, (y2)*2, 0, 0 } }

/* ──────────────────── 图标定义（16×16 网格） ──────────────────── */

/* 喇叭主体（共用）：实心梯形（箱体+锥面一体），小字号下最清晰 */
#define SPEAKER_BODY                                                          \
    T(1.6f,6.2f,1.6f,9.8f,6.4f,13.2f), T(1.6f,6.2f,6.4f,13.2f,6.4f,2.8f)

/* 音频/喇叭（双声波） */
static const sym_op_t g_audio[] = {
    SPEAKER_BODY, A(6.4f,8,3.0f,300,60), A(6.4f,8,5.6f,325,35),
};
/* 视频/摄像机 */
static const sym_op_t g_video[] = {
    RR(1.8f,4.6f,8.6f,6.8f,1.4f),
    L(10.4f,7.2f,14.2f,4.6f), L(14.2f,4.6f,14.2f,11.4f), L(14.2f,11.4f,10.4f,8.8f),
};
/* 列表 */
static const sym_op_t g_list[] = {
    D(3,4,1.1f), D(3,8,1.1f), D(3,12,1.1f),
    L(6,4,14,4), L(6,8,14,8), L(6,12,14,12),
};
/* 对勾 */
static const sym_op_t g_ok[] = { L(3,8.5f,6.5f,12), L(6.5f,12,13,4) };
/* 关闭 × */
static const sym_op_t g_close[] = { L(4,4,12,12), L(12,4,4,12) };
/* 电源 */
static const sym_op_t g_power[] = {
    A(8,8.6f,5.6f,300,240), L(8,1.5f,8,7),
};
/* 设置（齿轮简化为环+四齿） */
static const sym_op_t g_settings[] = {
    A(8,8,4.2f,0,360),
    L(8,1.2f,8,3.4f), L(8,12.6f,8,14.8f),
    L(1.2f,8,3.4f,8), L(12.6f,8,14.8f,8),
    D(8,8,1.3f),
};
/* 首页 */
static const sym_op_t g_home[] = {
    L(1.8f,7.6f,8,2), L(8,2,14.2f,7.6f),
    L(3.8f,6.6f,3.8f,14), L(3.8f,14,12.2f,14), L(12.2f,14,12.2f,6.6f),
};
/* 下载 */
static const sym_op_t g_download[] = {
    L(8,2,8,10), L(4.5f,6.5f,8,10), L(11.5f,6.5f,8,10), L(2.5f,13.5f,13.5f,13.5f),
};
/* 上传 */
static const sym_op_t g_upload[] = {
    L(8,10,8,2), L(4.5f,5.5f,8,2), L(11.5f,5.5f,8,2), L(2.5f,13.5f,13.5f,13.5f),
};
/* 硬盘/驱动 */
static const sym_op_t g_drive[] = {
    RR(1.8f,9,12.4f,4.5f,1.2f), L(3.5f,9,4.5f,4.5f), L(4.5f,4.5f,11.5f,4.5f),
    L(11.5f,4.5f,12.5f,9), D(11,11.6f,0.9f),
};
/* 刷新 */
static const sym_op_t g_refresh[] = {
    A(8,8,5,20,300), L(12.6f,3.2f,12.8f,6.4f), L(12.6f,3.2f,9.4f,3.4f),
};
/* 静音 */
static const sym_op_t g_mute[] = {
    SPEAKER_BODY, L(8.8f,5.8f,14.0f,10.2f), L(14.0f,5.8f,8.8f,10.2f),
};
/* 音量中 */
static const sym_op_t g_vol_mid[] = {
    SPEAKER_BODY, A(6.4f,8,3.0f,300,60),
};
/* 音量最大 */
static const sym_op_t g_vol_max[] = {
    SPEAKER_BODY, A(6.4f,8,3.0f,300,60), A(6.4f,8,5.6f,325,35),
};
/* 图片 */
static const sym_op_t g_image[] = {
    RR(1.8f,3.2f,12.4f,9.6f,1.4f), D(5.4f,6.6f,1.15f),
    L(3,11.4f,6.4f,8), L(6.4f,8,9.4f,11.2f), L(9.4f,11.2f,11.2f,9.4f),
    L(11.2f,9.4f,13,11.4f),
};
/* 水滴：上方尖角 + 下方圆（用一个圆 + 两条边包成泪滴） */
static const sym_op_t g_tint[] = {
    A(8,9.6f,4.0f,0,360),
    L(4.6f,7.0f,8,1.8f), L(8,1.8f,11.4f,7.0f),
};
/* 上一首 */
static const sym_op_t g_prev[] = {
    LW(3.2f,3,3.2f,13,1.6f), T(12.5f,3.4f,12.5f,12.6f,4.8f,8),
};
/* 下一首 */
static const sym_op_t g_next[] = {
    LW(12.8f,3,12.8f,13,1.6f), T(3.5f,3.4f,3.5f,12.6f,11.2f,8),
};
/* 播放 */
static const sym_op_t g_play[] = {
    T(4.6f,2.8f,4.6f,13.2f,13.0f,8),
};
/* 暂停 */
static const sym_op_t g_pause[] = {
    LW(5,3,5,13,4), LW(11,3,11,13,4),
};
/* 停止 */
static const sym_op_t g_stop[] = {
    { OP_RRECT, { 4*2, 4*2, 8*2, 8*2, 1*2, 0 } },
};
/* 弹出 */
static const sym_op_t g_eject[] = {
    T(8,2.4f,13,8.4f,3,8.4f), L(3,11.6f,13,11.6f),
};
/* 左 / 右 */
static const sym_op_t g_left[]  = { L(10,3,4.5f,8), L(4.5f,8,10,13) };
static const sym_op_t g_right[] = { L(6,3,11.5f,8), L(11.5f,8,6,13) };
/* 上 / 下 */
static const sym_op_t g_up[]   = { L(3,10,8,4.5f), L(8,4.5f,13,10) };
static const sym_op_t g_down[] = { L(3,6,8,11.5f), L(8,11.5f,13,6) };
/* 加 / 减 */
static const sym_op_t g_plus[]  = { L(8,2.6f,8,13.4f), L(2.6f,8,13.4f,8) };
static const sym_op_t g_minus[] = { L(2.6f,8,13.4f,8) };
/* 眼睛开：杏眼（上下两段弧围成尖角）+ 实心瞳 */
static const sym_op_t g_eye_open[] = {
    A(8,9.0f,7.4f,208,332), A(8,9.0f,7.4f,28,152), D(8,8.6f,2.4f),
};
/* 眼睛闭：向下弯的弧 + 两侧睫毛 */
static const sym_op_t g_eye_close[] = {
    A(8,3.4f,6.6f,28,152), L(2.2f,10.6f,4.4f,8.8f), L(13.8f,10.6f,11.6f,8.8f),
};
/* 警告三角 */
static const sym_op_t g_warning[] = {
    L(8,2,14.2f,13.4f), L(14.2f,13.4f,1.8f,13.4f), L(1.8f,13.4f,8,2),
    L(8,6.2f,8,9.6f), D(8,11.6f,0.85f),
};
/* 随机 */
static const sym_op_t g_shuffle[] = {
    L(2,4,5,4), L(5,4,11,12), L(11,12,14,12),
    L(2,12,5,12), L(5,12,11,4), L(11,4,14,4),
    L(12,2.6f,14,4), L(14,4,12,5.4f), L(12,10.6f,14,12), L(14,12,12,13.4f),
};
/* 循环 */
static const sym_op_t g_loop[] = {
    A(8,8,5.2f,200,340), A(8,8,5.2f,20,160),
    L(11.6f,1.6f,11.9f,4.4f), L(11.6f,1.6f,8.8f,1.9f),
    L(4.4f,14.4f,4.1f,11.6f), L(4.4f,14.4f,7.2f,14.1f),
};
/* 目录 */
static const sym_op_t g_directory[] = {
    L(1.8f,4.4f,6,4.4f), L(1.8f,4.4f,1.8f,12.4f), L(1.8f,12.4f,14.2f,12.4f),
    L(14.2f,12.4f,14.2f,5.4f), L(14.2f,5.4f,7.4f,5.4f), L(7.4f,5.4f,6.2f,4.4f),
};
/* 电话 */
static const sym_op_t g_call[] = {
    L(3,2.6f,5.6f,2.6f), L(5.6f,2.6f,6.4f,6), L(6.4f,6,4.6f,7.6f),
    L(4.6f,7.6f,8.4f,11.4f), L(8.4f,11.4f,10,9.6f), L(10,9.6f,13.4f,10.4f),
    L(13.4f,10.4f,13.4f,13), L(13.4f,13,11,13.4f), L(11,13.4f,3,5.4f), L(3,5.4f,3,2.6f),
};
/* 剪切 */
static const sym_op_t g_cut[] = {
    A(4,3.4f,2,0,360), A(4,12.6f,2,0,360), L(5.4f,4.8f,13,12.6f), L(5.4f,11.2f,13,3.4f),
};
/* 复制 */
static const sym_op_t g_copy[] = {
    RR(2,2,8,8,1.2f), RR(6,6,8,8,1.2f),
};
/* 保存 */
static const sym_op_t g_save[] = {
    L(2,2.4f,12,2.4f), L(12,2.4f,14,4.4f), L(14,4.4f,14,13.6f), L(14,13.6f,2,13.6f),
    L(2,13.6f,2,2.4f), RR(5,2.4f,6,4,0.6f), RR(4.6f,9,6.8f,4.6f,0.8f),
};
/* 汉堡菜单 */
static const sym_op_t g_bars[] = { L(2.4f,4.4f,13.6f,4.4f), L(2.4f,8,13.6f,8), L(2.4f,11.6f,13.6f,11.6f) };
/* 信封 */
static const sym_op_t g_envelope[] = {
    RR(1.8f,3.8f,12.4f,8.4f,1.2f), L(2.4f,4.6f,8,8.6f), L(8,8.6f,13.6f,4.6f),
};
/* 闪电/充电 */
static const sym_op_t g_charge[] = {
    L(9.4f,1.6f,4.6f,8.8f), L(4.6f,8.8f,7.8f,8.8f), L(7.8f,8.8f,6.4f,14.4f),
    L(6.4f,14.4f,11.4f,7.4f), L(11.4f,7.4f,8.2f,7.4f), L(8.2f,7.4f,9.4f,1.6f),
};
/* 粘贴 */
static const sym_op_t g_paste[] = {
    RR(2.4f,3,11.2f,11,1.2f), RR(5.4f,1.6f,5.2f,3,0.8f), RR(4.6f,6,6.8f,6,0.8f),
};
/* 铃铛：上环 + 两肩 + 底边 + 下摆弧 + 顶部挂钩 */
static const sym_op_t g_bell[] = {
    A(8,8.0f,4.0f,190,350), L(4.0f,8.6f,4.0f,11.0f), L(12.0f,8.6f,12.0f,11.0f),
    L(2.8f,11.0f,13.2f,11.0f), A(8,11.0f,1.7f,0,180), L(8,4.2f,8,2.4f),
};
/* 键盘 */
static const sym_op_t g_keyboard[] = {
    RR(1.8f,4,12.4f,8,1.4f), L(4.6f,10,11.4f,10),
    D(4.8f,6.6f,0.7f), D(8,6.6f,0.7f), D(11.2f,6.6f,0.7f), D(4.8f,8.4f,0.7f), D(11.2f,8.4f,0.7f),
};
/* GPS 定位针 */
static const sym_op_t g_gps[] = {
    A(8,6.4f,4.6f,155,385), L(4.3f,9.6f,8,14.6f), L(11.7f,9.6f,8,14.6f), D(8,6.4f,1.6f),
};
/* 文件 */
static const sym_op_t g_file[] = {
    L(3.4f,1.8f,9.4f,1.8f), L(9.4f,1.8f,12.6f,5), L(12.6f,5,12.6f,14.2f),
    L(12.6f,14.2f,3.4f,14.2f), L(3.4f,14.2f,3.4f,1.8f),
    L(9.4f,1.8f,9.4f,5), L(9.4f,5,12.6f,5),
};
/* WiFi */
static const sym_op_t g_wifi[] = {
    A(8,12.4f,1.5f,200,340), A(8,12.4f,4.6f,215,325), A(8,12.4f,7.8f,222,318),
};
/* 电池 */
static const sym_op_t g_bat_full[] = {
    RRW(1.4f,4.4f,11.8f,7.2f,1,0), L(14.2f,6.8f,14.2f,9.2f),
    { OP_TRI, { 12.6f*2, 5.6f*2, 12.6f*2, 10.4f*2, 3.2f*2, 10.4f*2 } },
    { OP_TRI, { 12.6f*2, 5.6f*2, 3.2f*2, 10.4f*2, 3.2f*2, 5.6f*2 } },
};
static const sym_op_t g_bat_3[] = {
    RRW(1.4f,4.4f,11.8f,7.2f,1,0), L(14.2f,6.8f,14.2f,9.2f),
    { OP_RRECT, { 3.2f*2, 5.6f*2, 7.2f*2, 4.8f*2, 0, 0 } },
};
static const sym_op_t g_bat_2[] = {
    RRW(1.4f,4.4f,11.8f,7.2f,1,0), L(14.2f,6.8f,14.2f,9.2f),
    { OP_RRECT, { 3.2f*2, 5.6f*2, 4.8f*2, 4.8f*2, 0, 0 } },
};
static const sym_op_t g_bat_1[] = {
    RRW(1.4f,4.4f,11.8f,7.2f,1,0), L(14.2f,6.8f,14.2f,9.2f),
    { OP_RRECT, { 3.2f*2, 5.6f*2, 2.4f*2, 4.8f*2, 0, 0 } },
};
static const sym_op_t g_bat_empty[] = {
    RRW(1.4f,4.4f,11.8f,7.2f,1,0), L(14.2f,6.8f,14.2f,9.2f),
};
/* USB */
static const sym_op_t g_usb[] = {
    L(8,14,8,3), D(8,2.2f,1.1f), L(8,11,4,8.6f), D(4,8.2f,1.1f),
    L(8,9.4f,12,7), { OP_RRECT, { 11.2f*2, 5.2f*2, 1.6f*2, 1.6f*2, 0, 0 } },
};
/* 蓝牙 */
static const sym_op_t g_bluetooth[] = {
    L(5,4.4f,11,11.6f), L(11,11.6f,8,14.4f), L(8,14.4f,8,1.6f),
    L(8,1.6f,11,4.4f), L(11,4.4f,5,11.6f),
};
/* 垃圾桶 */
static const sym_op_t g_trash[] = {
    L(2.4f,4.4f,13.6f,4.4f), L(6.2f,4.4f,6.8f,2), L(6.8f,2,9.2f,2),
    L(9.2f,2,9.8f,4.4f), L(4.2f,4.4f,5,14), L(5,14,11,14), L(11,14,11.8f,4.4f),
    L(6.8f,7,6.8f,11.4f), L(9.2f,7,9.2f,11.4f),
};
/* 编辑 */
static const sym_op_t g_edit[] = {
    L(2.6f,13.4f,3.4f,10.2f), L(3.4f,10.2f,10.4f,3.2f), L(10.4f,3.2f,12.8f,5.6f),
    L(12.8f,5.6f,5.8f,12.6f), L(5.8f,12.6f,2.6f,13.4f),
};
/* 退格 */
static const sym_op_t g_backspace[] = {
    L(5.6f,3.6f,14,3.6f), L(14,3.6f,14,12.4f), L(14,12.4f,5.6f,12.4f),
    L(5.6f,12.4f,1.6f,8), L(1.6f,8,5.6f,3.6f), L(6.8f,6.2f,11,9.8f), L(11,6.2f,6.8f,9.8f),
};
/* SD 卡 */
static const sym_op_t g_sd[] = {
    L(3.6f,1.8f,11,1.8f), L(11,1.8f,12.6f,3.4f), L(12.6f,3.4f,12.6f,14.2f),
    L(12.6f,14.2f,3.6f,14.2f), L(3.6f,14.2f,3.6f,1.8f),
    L(5.8f,3.6f,5.8f,6.4f), L(8,3.6f,8,6.4f), L(10.2f,3.6f,10.2f,6.4f),
};
/* 换行 */
static const sym_op_t g_newline[] = {
    L(2,3.6f,11,3.6f), L(11,3.6f,11,10), L(11,10,4.2f,10), L(6.2f,7.4f,4,10), L(4,10,6.2f,12.6f),
};
/* 项目符号 */
static const sym_op_t g_bullet[] = { D(8,8,2.2f) };

/* ──────────────────── 码位 → 字形 映射 ──────────────────── */

#define G(a) { (uint8_t)(sizeof(a) / sizeof((a)[0])), (a) }

typedef struct {
    uint32_t   cp;
    sym_glyph_t g;
} sym_map_t;

static const sym_map_t s_map[] = {
    { 0x2022, G(g_bullet)     },
    { 0xF001, G(g_audio)      },
    { 0xF008, G(g_video)      },
    { 0xF00B, G(g_list)       },
    { 0xF00C, G(g_ok)         },
    { 0xF00D, G(g_close)      },
    { 0xF011, G(g_power)      },
    { 0xF013, G(g_settings)   },
    { 0xF015, G(g_home)       },
    { 0xF019, G(g_download)   },
    { 0xF01C, G(g_drive)      },
    { 0xF021, G(g_refresh)    },
    { 0xF026, G(g_mute)       },
    { 0xF027, G(g_vol_mid)    },
    { 0xF028, G(g_vol_max)    },
    { 0xF03E, G(g_image)      },
    { 0xF043, G(g_tint)       },
    { 0xF048, G(g_prev)       },
    { 0xF04B, G(g_play)       },
    { 0xF04C, G(g_pause)      },
    { 0xF04D, G(g_stop)       },
    { 0xF051, G(g_next)       },
    { 0xF052, G(g_eject)      },
    { 0xF053, G(g_left)       },
    { 0xF054, G(g_right)      },
    { 0xF067, G(g_plus)       },
    { 0xF068, G(g_minus)      },
    { 0xF06E, G(g_eye_open)   },
    { 0xF070, G(g_eye_close)  },
    { 0xF071, G(g_warning)    },
    { 0xF074, G(g_shuffle)    },
    { 0xF077, G(g_up)         },
    { 0xF078, G(g_down)       },
    { 0xF079, G(g_loop)       },
    { 0xF07B, G(g_directory)  },
    { 0xF093, G(g_upload)     },
    { 0xF095, G(g_call)       },
    { 0xF0C4, G(g_cut)        },
    { 0xF0C5, G(g_copy)       },
    { 0xF0C7, G(g_save)       },
    { 0xF0C9, G(g_bars)       },
    { 0xF0E0, G(g_envelope)   },
    { 0xF0E7, G(g_charge)     },
    { 0xF0EA, G(g_paste)      },
    { 0xF0F3, G(g_bell)       },
    { 0xF11C, G(g_keyboard)   },
    { 0xF124, G(g_gps)        },
    { 0xF158, G(g_file)       },
    { 0xF1EB, G(g_wifi)       },
    { 0xF240, G(g_bat_full)   },
    { 0xF241, G(g_bat_3)      },
    { 0xF242, G(g_bat_2)      },
    { 0xF243, G(g_bat_1)      },
    { 0xF244, G(g_bat_empty)  },
    { 0xF287, G(g_usb)        },
    { 0xF293, G(g_bluetooth)  },
    { 0xF2ED, G(g_trash)      },
    { 0xF304, G(g_edit)       },
    { 0xF55A, G(g_backspace)  },
    { 0xF7C2, G(g_sd)         },
    { 0xF8A2, G(g_newline)    },
};

#define SYM_MAP_N ((int)(sizeof(s_map) / sizeof(s_map[0])))
#define SYM_GRID  16.0f

static int sym_lookup(uint32_t cp)
{
    for (int i = 0; i < SYM_MAP_N; i++) {
        if (s_map[i].cp == cp) {
            return i;
        }
    }
    return -1;
}

/* ──────────────────── 光栅化 ──────────────────── */

#define SS 4   /* 每像素 4×4 超采样 */
#define SS_N (SS * SS)

typedef struct {
    float x, y, w, h;   /* 包围盒（像素，含线宽） */
} bbox_t;

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* 点到线段距离 */
static float dist_seg(float px, float py, float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len2 = dx * dx + dy * dy;
    if (len2 < 1e-6f) {
        return hypotf(px - x1, py - y1);
    }
    float t = clampf(((px - x1) * dx + (py - y1) * dy) / len2, 0.0f, 1.0f);
    return hypotf(px - (x1 + t * dx), py - (y1 + t * dy));
}

/* 扇形环测试：圆心(cx,cy)、半径 r、线宽 w、角度区间[a0,a1]（度） */
static int in_arc_sector(float px, float py, float cx, float cy, float r, float w,
                         float a0, float a1)
{
    float dx = px - cx, dy = py - cy;
    float d = hypotf(dx, dy);
    if (fabsf(d - r) > w * 0.5f + 0.5f) {
        return 0;
    }
    if (d < 1e-4f) {
        return 0;
    }
    /* 屏幕坐标 y 向下；angle 从 +x 轴顺时针为正 */
    float ang = atan2f(dy, dx) * 57.2957795f;
    if (ang < 0) {
        ang += 360.0f;
    }
    float span = a1 - a0;
    if (span <= 0) {
        span += 360.0f;
    }
    if (span >= 359.0f) {
        return 1;
    }
    float rel = ang - a0;
    while (rel < 0) {
        rel += 360.0f;
    }
    while (rel >= 360.0f) {
        rel -= 360.0f;
    }
    return rel <= span;
}

static int in_tri(float px, float py, float x1, float y1, float x2, float y2, float x3, float y3)
{
    float d1 = (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2);
    float d2 = (px - x3) * (y2 - y3) - (x2 - x3) * (py - y3);
    float d3 = (px - x1) * (y3 - y1) - (x3 - x1) * (py - y1);
    int neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    int pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

/* 圆角矩形描边：内部实心距边界在 w 内 */
static int in_rrect_stroke(float px, float py, float x, float y, float w_, float h_, float r, float sw)
{
    /* 外扩半个线宽再测 */
    float ox = x - sw * 0.5f, oy = y - sw * 0.5f;
    float ow = w_ + sw, oh = h_ + sw;
    if (px < ox || py < oy || px > ox + ow || py > oy + oh) {
        return 0;
    }
    /* 角部圆化 */
    float rr = r + sw * 0.5f;
    if (rr > ow * 0.5f) {
        rr = ow * 0.5f;
    }
    if (rr > oh * 0.5f) {
        rr = oh * 0.5f;
    }
    float cx, cy;
    if (px < ox + rr && py < oy + rr) {
        cx = ox + rr; cy = oy + rr;
    } else if (px > ox + ow - rr && py < oy + rr) {
        cx = ox + ow - rr; cy = oy + rr;
    } else if (px < ox + rr && py > oy + oh - rr) {
        cx = ox + rr; cy = oy + oh - rr;
    } else if (px > ox + ow - rr && py > oy + oh - rr) {
        cx = ox + ow - rr; cy = oy + oh - rr;
    } else {
        cx = px; cy = py;
    }
    float d = hypotf(px - cx, py - cy);
    float outer = (cx == px && cy == py) ? 0.0f : rr;
    /* 判断是否在描边带内：外侧距离 < sw 且不在内部 */
    if (outer > 0.0f) {
        return d <= rr && d > rr - sw;
    }
    /* 直边区域 */
    float dxo = fminf(px - ox, ox + ow - px);
    float dyo = fminf(py - oy, oy + oh - py);
    float dmin = fminf(dxo, dyo);
    return dmin <= sw;
}

/* 圆角矩形填充（用于实心块） */
static int in_rrect_fill(float px, float py, float x, float y, float w_, float h_, float r)
{
    if (px < x || py < y || px > x + w_ || py > y + h_) {
        return 0;
    }
    float rr = r;
    if (rr > w_ * 0.5f) {
        rr = w_ * 0.5f;
    }
    if (rr > h_ * 0.5f) {
        rr = h_ * 0.5f;
    }
    float cx, cy;
    int corner = 0;
    if (px < x + rr && py < y + rr) {
        cx = x + rr; cy = y + rr; corner = 1;
    } else if (px > x + w_ - rr && py < y + rr) {
        cx = x + w_ - rr; cy = y + rr; corner = 1;
    } else if (px < x + rr && py > y + h_ - rr) {
        cx = x + rr; cy = y + h_ - rr; corner = 1;
    } else if (px > x + w_ - rr && py > y + h_ - rr) {
        cx = x + w_ - rr; cy = y + h_ - rr; corner = 1;
    } else {
        return 1;
    }
    (void)corner;
    return hypotf(px - cx, py - cy) <= rr;
}

static int in_op(const sym_op_t *o, float px, float py, float scale, float def_w)
{
    float p0 = o->p[0] * 0.5f, p1 = o->p[1] * 0.5f;
    float p2 = o->p[2] * 0.5f, p3 = o->p[3] * 0.5f;
    float p4 = o->p[4] * 0.5f, p5 = o->p[5] * 0.5f;

    switch (o->op) {
        case OP_LINE: {
            float w = o->p[4] ? p4 * scale : def_w;
            return dist_seg(px, py, p0 * scale, p1 * scale, p2 * scale, p3 * scale) <= w * 0.5f;
        }
        case OP_ARC: {
            float w = o->p[5] ? p5 * scale : def_w;
            return in_arc_sector(px, py, p0 * scale, p1 * scale, p2 * scale, w,
                                 o->p[3] * 3.0f, o->p[4] * 3.0f);
        }
        case OP_DOT:
            return hypotf(px - p0 * scale, py - p1 * scale) <= p2 * scale;
        case OP_TRI:
            return in_tri(px, py, p0 * scale, p1 * scale, p2 * scale, p3 * scale,
                          p4 * scale, p5 * scale);
        case OP_RRECT: {
            float w = o->p[5] ? o->p[5] * scale : def_w;
            if (p4 > 0.0f) {
                return in_rrect_stroke(px, py, p0 * scale, p1 * scale, p2 * scale, p3 * scale,
                                       p4 * scale, w);
            }
            return in_rrect_fill(px, py, p0 * scale, p1 * scale, p2 * scale, p3 * scale, 0.0f);
        }
        default:
            return 0;
    }
}

/* 计算单条指令的包围盒 */
static void op_bbox(const sym_op_t *o, float scale, float def_w, bbox_t *b)
{
    float p0 = o->p[0] * 0.5f, p1 = o->p[1] * 0.5f;
    float p2 = o->p[2] * 0.5f, p3 = o->p[3] * 0.5f;
    float p4 = o->p[4] * 0.5f, p5 = o->p[5] * 0.5f;
    float pad = def_w;

    switch (o->op) {
        case OP_LINE:
            pad = (o->p[4] ? p4 * scale : def_w) * 0.5f + 1.0f;
            b->x = fminf(p0, p2) * scale - pad;
            b->y = fminf(p1, p3) * scale - pad;
            b->w = fabsf(p2 - p0) * scale + pad * 2;
            b->h = fabsf(p3 - p1) * scale + pad * 2;
            break;
        case OP_ARC: {
            pad = (o->p[5] ? p5 * scale : def_w) * 0.5f + 1.5f;
            float r = p2 * scale + pad;
            b->x = p0 * scale - r;
            b->y = p1 * scale - r;
            b->w = b->h = r * 2;
            break;
        }
        case OP_DOT:
            b->x = p0 * scale - p2 * scale - 1;
            b->y = p1 * scale - p2 * scale - 1;
            b->w = b->h = p2 * scale * 2 + 2;
            break;
        case OP_TRI:
            b->x = fminf(p0, fminf(p2, p4)) * scale - 1;
            b->y = fminf(p1, fminf(p3, p5)) * scale - 1;
            b->w = (fmaxf(p0, fmaxf(p2, p4)) - fminf(p0, fminf(p2, p4))) * scale + 2;
            b->h = (fmaxf(p1, fmaxf(p3, p5)) - fminf(p1, fminf(p3, p5))) * scale + 2;
            break;
        case OP_RRECT: {
            pad = (o->p[5] ? o->p[5] * scale : def_w) * 0.5f + 1.0f;
            b->x = p0 * scale - pad;
            b->y = p1 * scale - pad;
            b->w = p2 * scale + pad * 2;
            b->h = p3 * scale + pad * 2;
            break;
        }
        default:
            b->x = b->y = 0;
            b->w = b->h = 0;
            break;
    }
}

/* ──────────────────── lv_font_t 后端 ──────────────────── */

typedef struct {
    uint8_t  size;      /* 目标像素字号 */
    int16_t  box;       /* 字形方形边长 */
    float    scale;     /* 网格 → 像素 */
} sym_font_dsc_t;

#define SYM_MAX_FONTS 10

static bool sym_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                              uint32_t letter, uint32_t letter_next)
{
    (void)letter_next;
    int idx = sym_lookup(letter);
    if (idx < 0) {
        return false;
    }
    const sym_font_dsc_t *fd = (const sym_font_dsc_t *)font->dsc;
    int box = fd->box;
    /* box 已保证 ≤ line_height - base_line，这里 ofs_y=0 时字形正好贴行框顶部 */
    dsc_out->adv_w  = (uint16_t)(box + 1);
    dsc_out->box_w  = (uint16_t)box;
    dsc_out->box_h  = (uint16_t)box;
    dsc_out->ofs_x  = 0;
    dsc_out->ofs_y  = 0;
    dsc_out->stride = 0;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc_out->is_placeholder = 0;
    dsc_out->gid.index = (uint32_t)(idx + 1);
    return true;
}

static const void *sym_get_glyph_bitmap(lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf)
{
    const lv_font_t *font = g_dsc->resolved_font;
    const sym_font_dsc_t *fd = (const sym_font_dsc_t *)font->dsc;
    int idx = (int)g_dsc->gid.index - 1;
    if (idx < 0 || idx >= SYM_MAP_N) {
        return NULL;
    }
    if (g_dsc->req_raw_bitmap || draw_buf == NULL) {
        return NULL;
    }

    const sym_glyph_t *gl = &s_map[idx].g;
    int w = g_dsc->box_w;
    int h = g_dsc->box_h;
    if (w <= 0 || h <= 0) {
        return NULL;
    }

    uint8_t *out = draw_buf->data;
    uint32_t stride = draw_buf->header.stride;

    /* ── 自动归一化：把图形按自身包围盒等比缩放并居中到 box ──
     * 不再依赖手写坐标恰好落在网格内，避免任何图标被裁切；
     * 同时让所有图标的视觉重量一致（光学平衡）。 */
    int nb = gl->n < 32 ? gl->n : 32;
    float ux1 = 1e9f, uy1 = 1e9f, ux2 = -1e9f, uy2 = -1e9f;
    for (int i = 0; i < nb; i++) {
        bbox_t b;
        op_bbox(&gl->ops[i], 1.0f, 0.0f, &b);
        if (b.w <= 0 || b.h <= 0) {
            continue;
        }
        ux1 = fminf(ux1, b.x);
        uy1 = fminf(uy1, b.y);
        ux2 = fmaxf(ux2, b.x + b.w);
        uy2 = fmaxf(uy2, b.y + b.h);
    }
    if (ux2 <= ux1 || uy2 <= uy1) {
        return NULL;
    }

    /* 统一描边（像素）：随字号增长但斜率放缓，保证小字号不糊 */
    float def_w = fd->size * 0.075f;
    if (def_w < 1.2f) {
        def_w = 1.2f;
    }
    if (def_w > 3.4f) {
        def_w = 3.4f;
    }

    /* 留出描边余量后求贴合比例 */
    float avail_w = (float)w - def_w - 1.0f;
    float avail_h = (float)h - def_w - 1.0f;
    if (avail_w < 1.0f) {
        avail_w = 1.0f;
    }
    if (avail_h < 1.0f) {
        avail_h = 1.0f;
    }
    float gw = ux2 - ux1;
    float gh = uy2 - uy1;
    float fit = fminf(avail_w / gw, avail_h / gh);
    /* 上限：把 12 单位宽的设计图放到满格，防止小图形被过度放大 */
    float cap = (float)w / 12.0f;
    float scale = fit < cap ? fit : cap;

    /* 居中偏移 */
    float ox = ((float)w - gw * scale) * 0.5f - ux1 * scale;
    float oy = ((float)h - gh * scale) * 0.5f - uy1 * scale;

    bbox_t bb[32];
    for (int i = 0; i < nb; i++) {
        op_bbox(&gl->ops[i], scale, def_w, &bb[i]);
        bb[i].x += ox;
        bb[i].y += oy;
    }

    const float inv = 1.0f / SS;
    for (int y = 0; y < h; y++) {
        uint8_t *row = out + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            int hits = 0;
            for (int sy = 0; sy < SS; sy++) {
                float py = (float)y + (sy + 0.5f) * inv;
                for (int sx = 0; sx < SS; sx++) {
                    float px = (float)x + (sx + 0.5f) * inv;
                    for (int i = 0; i < nb; i++) {
                        if (px < bb[i].x || py < bb[i].y ||
                            px > bb[i].x + bb[i].w || py > bb[i].y + bb[i].h) {
                            continue;
                        }
                        if (in_op(&gl->ops[i], px - ox, py - oy, scale, def_w)) {
                            hits++;
                            break;
                        }
                    }
                }
            }
            row[x] = (uint8_t)(hits * 255 / SS_N);
        }
    }
    lv_draw_buf_flush_cache(draw_buf, NULL);
    return draw_buf;
}

/* 字体实例池（静态，无堆分配） */
static lv_font_t       s_fonts[SYM_MAX_FONTS];
static sym_font_dsc_t  s_dscs[SYM_MAX_FONTS];
static int             s_font_n;

/* 与目标字号最接近的 montserrat（作为符号字体之后的第三级兜底，
 * 保证 ASCII 标点等非符号字形仍能落到位图字体上，不会画成空框） */
static const lv_font_t *montserrat_for(int px)
{
    static const struct {
        int px;
        const lv_font_t *f;
    } t[] = {
#if LV_FONT_MONTSERRAT_8
        { 8,  &lv_font_montserrat_8  },
#endif
#if LV_FONT_MONTSERRAT_10
        { 10, &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
        { 12, &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
        { 14, &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
        { 16, &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
        { 18, &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
        { 20, &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
        { 22, &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
        { 24, &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
        { 26, &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
        { 28, &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
        { 30, &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
        { 32, &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
        { 34, &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
        { 36, &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
        { 38, &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
        { 40, &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
        { 42, &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
        { 44, &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
        { 46, &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
        { 48, &lv_font_montserrat_48 },
#endif
    };
    const lv_font_t *best = &lv_font_montserrat_14;
    int best_d = 1 << 20;
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        int d = t[i].px > px ? t[i].px - px : px - t[i].px;
        if (d < best_d) {
            best_d = d;
            best = t[i].f;
        }
    }
    return best;
}

const lv_font_t *ui_symbol_font(int size_px)
{
    if (size_px < 8 || size_px > 96) {
        return NULL;
    }
    for (int i = 0; i < s_font_n; i++) {
        if (s_dscs[i].size == size_px) {
            return &s_fonts[i];
        }
    }
    if (s_font_n >= SYM_MAX_FONTS) {
        return &s_fonts[0];
    }

    int i = s_font_n++;
    sym_font_dsc_t *d = &s_dscs[i];
    d->size  = (uint8_t)size_px;
    /* 行高/基线：与目标字号一致，保证图标居中在文字行内 */
    int32_t line_h = (int32_t)size_px;
    int32_t base_l = (int32_t)(size_px * 0.22f);
    /* 图标视觉尺寸：约为字号 78%，且严格不超过"行高-基线"，
     * 否则 letter_coords.y1 会算出负数（越过行框顶部）。 */
    int32_t box = (int32_t)(size_px * 0.78f);
    int32_t cap = line_h - base_l - 1;
    if (box > cap) {
        box = cap;
    }
    if (box < 9) {
        box = 9;
    }
    d->box   = (int16_t)box;
    d->scale = (float)box / SYM_GRID;

    lv_font_t *f = &s_fonts[i];
    lv_memzero(f, sizeof(*f));
    f->get_glyph_dsc    = sym_get_glyph_dsc;
    f->get_glyph_bitmap = sym_get_glyph_bitmap;
    f->release_glyph    = NULL;
    f->line_height      = line_h;
    f->base_line        = base_l;
    f->subpx            = LV_FONT_SUBPX_NONE;
    f->static_bitmap    = 0;
    f->underline_position  = -1;
    f->underline_thickness = 1;
    f->dsc              = d;
    f->fallback         = montserrat_for(size_px);   /* 三级兜底 */
    return f;
}

bool ui_symbol_font_is_symbol(uint32_t cp)
{
    return sym_lookup(cp) >= 0;
}
