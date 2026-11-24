/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - Modal 统一层（dialog / 自定义卡片 / loading）
 *
 * 全部挂 lv_layer_top() 上的 modal_root（低于键盘/Toast，盖住页面栈）：
 *   - 栈式多 modal：每层 = 全屏遮罩 + 居中卡片，层叠加深变暗
 *   - 卡片 SCALE_FADE 进出（transform_scale 243→256 + overshoot，280ms）
 *   - ui_modal_close_top() 供 pm_back() 返回链关闭最顶弹窗
 *   - ui_loading_show() 转圈遮罩（三圆点动画，零小部件依赖）
 *
 * 设计依据: PAGE_MANAGER_DESIGN.md §9（z 序 / 生命周期 / 不进导航栈）
 * 页面只"开/关"不拥有：Modal 归本模块管理，页面销毁不必（也无法）逐个回收。
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Modal 构建描述（ui_modal_open 一次性传入；结构体可栈上分配） */
typedef struct {
    /** 在 card 容器内自绘内容（创建时同步调用一次） */
    void (*build)(lv_obj_t *card, void *user_data);
    /** 关闭前回调（无论按钮关/ close_top 关都会走；在此清理） */
    void (*on_close)(lv_obj_t *card, void *user_data);
    void *user_data;
    lv_coord_t width;          /* 0 = UI_SCREEN_W - 56（与确认框同宽） */
    lv_opa_t dim;              /* 遮罩浓度；0 = 默认 LV_OPA_60 */
    bool close_on_outside;     /* 点击卡片外区域关闭（默认 false） */
} ui_modal_builder_t;

/** 打开一个 Modal，返回卡片容器（往里 add 子对象即可）；转场动画自动播 */
lv_obj_t *ui_modal_open(const ui_modal_builder_t *b);

/** 关闭指定卡片所属 Modal（出动画后整层删除，on_close 先行） */
void ui_modal_close(lv_obj_t *card);

/** 关闭最顶层 Modal（pm_back() 返回链用）；无 Modal 时空操作 */
void ui_modal_close_top(void);

/** 是否有 Modal 打开 */
bool ui_modal_is_open(void);

/** 全屏 Loading（单例；重复 show 只更新文字/遮罩） */
void ui_loading_show(bool blocking);   /* blocking=true 吃掉所有输入 */

/** Loading 消失（未显示时空操作） */
void ui_loading_hide(void);

/** Loading 是否显示中 */
bool ui_loading_is_open(void);

#ifdef __cplusplus
}
#endif
