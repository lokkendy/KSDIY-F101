/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - WiFi 扫描页 View（纯控件）
 */
#pragma once

#include "Page.h"
#include "ui_keyboard.h"
#include "HAL_WiFi.h"   /* 仅用 wifi_ap_t 数据类型 */

typedef struct {
    lv_obj_t *root;
    lv_obj_t *lbl_status;
    lv_obj_t *ap_list;
    lv_obj_t *pwd_card;   /* 密码弹窗卡片（ui_modal 统一层） */
    lv_obj_t *pwd_ta;
    lv_obj_t *fail_card;  /* 连接失败弹窗卡片（重试/重新输入密码） */
} WifiScanView_t;

void WifiScanView_Create(WifiScanView_t *v, lv_obj_t *parent,
                         lv_event_cb_t on_refresh, lv_event_cb_t on_back);

void WifiScanView_SetStatus(WifiScanView_t *v, const char *text);
void WifiScanView_ShowScanning(WifiScanView_t *v);
/** 重建热点列表（user_data=序号；active_ssid=当前/上次选择的热点高亮；点击回调由 Presenter 提供） */
void WifiScanView_ShowApList(WifiScanView_t *v, const wifi_ap_t *aps, uint16_t count,
                             const char *active_ssid, lv_event_cb_t on_ap_click);

/** 弹出密码输入框（开放网络提示可留空） */
void WifiScanView_ShowPwdDialog(WifiScanView_t *v, const char *ssid, uint8_t authmode,
                                lv_event_cb_t on_cancel, lv_event_cb_t on_connect);
/** 关闭密码弹窗并收起键盘 */
void WifiScanView_ClosePwdDialog(WifiScanView_t *v);

/** 弹出连接失败框（原因文案 + 原因码 + 重试/重新输入密码/取消） */
void WifiScanView_ShowFailDialog(WifiScanView_t *v, const char *ssid,
                                 const char *reason_text, int reason,
                                 lv_event_cb_t on_retry, lv_event_cb_t on_reinput);
/** 关闭连接失败框（幂等） */
void WifiScanView_CloseFailDialog(WifiScanView_t *v);
