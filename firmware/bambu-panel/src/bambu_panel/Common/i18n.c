/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 中英文词典与 tr() 实现
 *
 * 查找顺序：应用注册词典（注册序）→ 内置通用词典 → 返回中文键本身。
 * ZH 模式零查表直接返回键。
 */
#include "i18n.h"

#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "i18n";

static volatile app_lang_t s_lang = APP_LANG_ZH;

void i18n_set_lang(app_lang_t lang)
{
    s_lang = lang;
}

app_lang_t i18n_get_lang(void)
{
    return s_lang;
}

/* ──────────────────── 应用词典注册（最多 8 张） ──────────────────── */

#define I18N_MAX_TABLES 8

static const i18n_dict_entry_t *s_tables[I18N_MAX_TABLES];
static size_t s_table_counts[I18N_MAX_TABLES];
static size_t s_table_num = 0;

void i18n_register(const i18n_dict_entry_t *entries, size_t count)
{
    if (!entries || count == 0) {
        return;
    }
    if (s_table_num >= I18N_MAX_TABLES) {
        return;
    }
    s_tables[s_table_num] = entries;
    s_table_counts[s_table_num] = count;
    s_table_num++;
}

/* ──────────────────── 内置通用词典 ────────────────────
 * 仅收录页面框架 + 所有应用通用的词条；业务文案放应用词典 app_i18n.c。 */
static const i18n_dict_entry_t s_fw_dict[] = {
    /* 通用（弹窗/结果提示） */
    { "取消", "Cancel" },
    /* 硬件日志（App.c） */
    { "[IMU] LSM6DS3TR-C 就绪", "[IMU] LSM6DS3TR-C ready" },
    { "按下", "Pressed" },
    { "松开", "Released" },
    { "长按", "Long-press" },
    { "加载中...", "Loading..." },
    { "确定", "OK" },
    { "操作完成", "Done" },
    { "操作失败", "Operation failed" },
    { "设备忙，请稍候", "Device busy, try later" },
    { "未连接", "Not connected" },
    { "已连接", "Connected" },
    { "开启", "On" },
    { "关闭", "Off" },

    /* 页面框架（Tab 栏 / 页头） */
    { "首页", "Home" },
    { "文件", "Files" },
    { "打印记录", "Print log" },
    { "相机", "Camera" },
    { "设置", "Settings" },
    { "关于", "About" },
    { "系统设置", "System Settings" },
    { "关于本机与应用", "About this device and app" },
    { "KSDIY 通用应用模板\n页面框架 / 多语言 / 语音 / WiFi 管理",
      "KSDIY App Template\nUI framework / i18n / voice / WiFi" },

    /* 设置页通用项（配合 settings 子系统） */
    { "显示", "Display" },
    { "亮度", "Brightness" },
    { LV_SYMBOL_EYE_OPEN " 配色", LV_SYMBOL_EYE_OPEN " Theme" },
    { "深色\n浅色", "Dark\nLight" },
    { "竖屏\n横屏 90°\n竖屏 180°\n横屏 270°",
      "Portrait\nLandscape 90°\nPortrait 180°\nLandscape 270°" },
    { "语言", "Language" },
    { "中文\nEnglish", "中文\nEnglish" },
    { LV_SYMBOL_AUDIO " 语音提示", LV_SYMBOL_AUDIO " Voice prompts" },
    { LV_SYMBOL_BELL " 触摸反馈音", LV_SYMBOL_BELL " Touch click sound" },
    { LV_SYMBOL_POWER " 重启设备", LV_SYMBOL_POWER " Reboot" },
    { "重启", "Reboot" },
    { "确定要重启设备吗？", "Reboot the device?" },

    /* 电源控制（AXP2101 软关机 / 电源键长按菜单） */
    { LV_SYMBOL_POWER " 关机", LV_SYMBOL_POWER " Power off" },
    { LV_SYMBOL_POWER " 重启", LV_SYMBOL_POWER " Reboot" },
    { "关机", "Power off" },
    { "确定要关机吗？关机后短按电源键开机。",
      "Power off now? Short-press the power key to turn on." },
    { "电源菜单", "Power menu" },
    { "关机失败", "Power off failed" },
    { "[SYS] 用户关机", "[SYS] User power off" },
    { "[SYS] 电源菜单: 关机", "[SYS] Power menu: off" },
    { "[SYS] 电源菜单: 重启", "[SYS] Power menu: reboot" },

    /* 状态栏 */
    { "主机", "Host" },

    /* WiFi 连接结果（HAL_WiFi 事件反馈，随框架同步全员生效） */
    { "WiFi 已连接", "WiFi connected" },
    { "WiFi 已断开", "WiFi disconnected" },
    { "未找到该网络，可能已关机或超出范围", "Network not found (off or out of range)" },
    { "路由器拒绝认证，请检查加密方式或 MAC 过滤", "Auth rejected: check encryption / MAC filter" },
    { "密码可能不正确，请重新输入", "Password may be wrong, re-enter it" },
    { "认证超时，请靠近路由器后重试", "Auth timeout: move closer to the router" },
    { "路由器连接数已满，请稍后再试", "Router is full, try again later" },
    { "被路由器拒绝，请检查黑名单设置", "Rejected by router: check the blacklist" },
    { "连接失败，请重试", "Connection failed, try again" },
    { "正在连接 %s", "Connecting to %s" },
    { "连接失败", "Connection failed" },
    { "原因码 %d", "Reason code %d" },
    { "重试", "Retry" },
    { "重新输入密码", "Re-enter password" },

    /* 启动日志通用短语 */
    { "[SYS] 设备启动: %s", "[SYS] Device started: %s" },
    { "[SYS] 显示初始化完成", "[SYS] Display ready" },
    { "[SYS] UI 就绪", "[SYS] UI ready" },
    { "[SYS] 系统启动完成", "[SYS] System ready" },
    { "[SD] SD 卡已挂载: /sdcard", "[SD] SD card mounted: /sdcard" },
    { "[SD] SD 卡未检测到", "[SD] SD card not found" },
};

const char *tr(const char *zh)
{
    if (s_lang == APP_LANG_ZH || zh == NULL) {
        return zh;
    }
    /* 应用词典优先（注册序），可覆盖内置词条 */
    for (size_t t = 0; t < s_table_num; t++) {
        for (size_t i = 0; i < s_table_counts[t]; i++) {
            if (strcmp(s_tables[t][i].zh, zh) == 0) {
                return s_tables[t][i].en;
            }
        }
    }
    for (size_t i = 0; i < sizeof(s_fw_dict) / sizeof(s_fw_dict[0]); i++) {
        if (strcmp(s_fw_dict[i].zh, zh) == 0) {
            return s_fw_dict[i].en;
        }
    }
    return zh;   /* 未命中回退中文原文 */
}

/* ──────────────────── 文案绑定注册表（原地重译） ──────────────────── */

typedef struct {
    lv_obj_t *label;
    const char *key;     /* 中文键（编译期字符串字面量，无须保活副本） */
} tr_bind_ent_t;

static tr_bind_ent_t s_binds[TR_BIND_MAX];

/** 回收失效槽（label 已删 / 地址被非 label 对象复用）。返回回收条数。
 *  lv_obj_is_valid 遍历屏幕树，只在表满自愈与重译时调用（低频路径）。 */
static int i18n_bind_gc(void)
{
    int freed = 0;
    for (int i = 0; i < TR_BIND_MAX; i++) {
        if (!s_binds[i].label) {
            continue;
        }
        /* 须验类型：已删对象的地址被新对象复用时 is_valid 会误判"活着"，
         * 对非 label 对象 lv_label_set_text = 野写（延迟崩溃，sim 实测段错误） */
        if (!lv_obj_is_valid(s_binds[i].label) ||
            !lv_obj_check_type(s_binds[i].label, &lv_label_class)) {
            s_binds[i].label = NULL;
            s_binds[i].key = NULL;
            freed++;
        }
    }
    return freed;
}

void tr_bind(lv_obj_t *label, const char *zh_key)
{
    /* 非 label 拒绑：文案已在创建时设好，只是失去重译（降级不崩溃） */
    if (!label || !zh_key || !lv_obj_check_type(label, &lv_label_class)) {
        return;
    }
    lv_label_set_text(label, tr(zh_key));

    /* 同 label 换键：复用槽位 */
    for (int i = 0; i < TR_BIND_MAX; i++) {
        if (s_binds[i].label == label) {
            s_binds[i].key = zh_key;
            return;
        }
    }
    for (int i = 0; i < TR_BIND_MAX; i++) {
        if (s_binds[i].label == NULL) {
            s_binds[i].label = label;
            s_binds[i].key = zh_key;
            return;
        }
    }
    /* 表满自愈：页面销毁后 dangling 槽堆积是常态（重译才会紧凑），
     * 先回收再入表——真机菜单页重建实测因此满表（2026-09-26） */
    if (i18n_bind_gc() > 0) {
        for (int i = 0; i < TR_BIND_MAX; i++) {
            if (s_binds[i].label == NULL) {
                s_binds[i].label = label;
                s_binds[i].key = zh_key;
                return;
            }
        }
    }
    ESP_LOGW(TAG, "tr_bind table full (%d), degrade: %.24s",
             TR_BIND_MAX, zh_key);
}

size_t i18n_retranslate_all(void)
{
    size_t n = 0;
    for (int i = 0; i < TR_BIND_MAX; i++) {
        if (!s_binds[i].label) {
            continue;
        }
        /* label 可能已被删除：有效性校验（遍历屏幕树），失效项顺带紧凑。
         * 还须验类型：已删对象的地址被新对象复用时 is_valid 会误判"活着"，
         * 对非 label 对象 lv_label_set_text = 野写（延迟崩溃，sim 实测段错误） */
        if (!lv_obj_is_valid(s_binds[i].label) ||
            !lv_obj_check_type(s_binds[i].label, &lv_label_class)) {
            s_binds[i].label = NULL;
            s_binds[i].key = NULL;
            continue;
        }
        lv_label_set_text(s_binds[i].label, tr(s_binds[i].key));
        n++;
    }
    return n;
}

size_t i18n_bind_count(void)
{
    size_t n = 0;
    for (int i = 0; i < TR_BIND_MAX; i++) {
        if (s_binds[i].label) {
            n++;
        }
    }
    return n;
}
