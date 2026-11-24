/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * 通用应用模板 - 电源菜单实现
 *
 * 关机由 AXP2101 切断系统电源轨，重启调用 esp_restart()。
 * 弹窗不可点遮罩关闭（关机须明确选择）；重复唤起不叠加。
 */
#include "ui_power_menu.h"

#include <stdint.h>

#include "esp_system.h"

#include "HAL.h"
#include "data_bridge.h"
#include "i18n.h"
#include "ui_common.h"
#include "ui_modal.h"
#include "ui_theme.h"

static lv_obj_t *s_card;
static lv_timer_t *shutdown_timer;

static void shutdown_check(lv_timer_t *timer)
{
    (void)timer;
    shutdown_timer = NULL;
    ui_toast(tr("关机未完成，请检查电源管理芯片"), UI_TOAST_ERROR);
}

void ui_power_shutdown(void)
{
    if (shutdown_timer) return;

    bridge_add_log(tr("[SYS] 电源菜单: 关机"));
    if (HAL_Power_Shutdown() != ESP_OK) {
        ui_toast(tr("关机失败"), UI_TOAST_ERROR);
        return;
    }

    shutdown_timer = lv_timer_create(shutdown_check, 3000, NULL);
    if (shutdown_timer) lv_timer_set_repeat_count(shutdown_timer, 1);
}

static void pwr_act(int act)
{
    lv_obj_t *card = s_card;
    s_card = NULL;
    if (card && lv_obj_is_valid(card)) {
        ui_modal_close(card);
    }

    if (act == 1) {
        bridge_add_log(tr("[SYS] 电源菜单: 重启"));
        esp_restart();
    } else if (act == 2) {
        ui_power_shutdown();
    }
}

static void pwr_btn_cb(lv_event_t *e)
{
    pwr_act((int)(intptr_t)lv_event_get_user_data(e));
}

static void pwr_build(lv_obj_t *card, void *ud)
{
    (void)ud;
    s_card = card;

    ui_create_label(card, tr("电源菜单"), UI_FONT_TITLE, UI_COLOR_TEXT);

    lv_obj_t *row = ui_create_row(card);
    lv_obj_set_style_pad_column(row, 10, 0);
    ui_create_btn(row, tr("取消"), 0, UI_BTN_H_SM, UI_BTN_SUBTLE,
                  pwr_btn_cb, (void *)(intptr_t)0);
    ui_create_btn(row, tr("重启"), 0, UI_BTN_H_SM, UI_BTN_OUTLINE,
                  pwr_btn_cb, (void *)(intptr_t)1);
    ui_create_btn(row, tr("关机"), 0, UI_BTN_H_SM, UI_BTN_DANGER,
                  pwr_btn_cb, (void *)(intptr_t)2);
}

void ui_power_menu_open(void)
{
    if (ui_modal_is_open()) {
        return;   /* 已有弹窗（含本菜单）不叠加 */
    }
    ui_modal_builder_t b = {
        .build = pwr_build,
        .dim = LV_OPA_60,
    };
    ui_modal_open(&b);
}
