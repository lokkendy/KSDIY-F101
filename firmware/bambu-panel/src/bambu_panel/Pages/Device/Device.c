#include "ui_bambu_command.h"
#include "Device.h"

#include "bambu_service.h"
#include "bambu_studio_api.h"
#include "bambu_status.h"
#include "i18n.h"
#include "ui_common.h"
#include "ui_screen.h"
#include "ui_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    lv_obj_t *lbl_fw;
    lv_obj_t *lbl_wifi;
    lv_obj_t *lbl_fans;
    lv_obj_t *lbl_hms;
    lv_obj_t *lbl_ams;
    lv_obj_t *ta_nozzle;
    lv_obj_t *ta_bed;
    lv_timer_t *timer;
} device_ui_t;

static device_ui_t s_dev;

static void refresh_ui(void)
{
    bambu_svc_snapshot_t snap;
    bambu_service_snapshot(&snap);
    const bambu_status_t *p = &snap.printer;

    if (p->fw_version[0] || p->fw_machine[0]) {
        char fw[80];
        snprintf(fw, sizeof(fw), "%s %s", p->fw_machine, p->fw_version);
        lv_label_set_text(s_dev.lbl_fw, fw);
    } else {
        lv_label_set_text(s_dev.lbl_fw, tr("—"));
    }

    if (p->wifi_signal != 0) {
        char w[32];
        snprintf(w, sizeof(w), "%d dBm", p->wifi_signal);
        lv_label_set_text(s_dev.lbl_wifi, w);
    } else {
        lv_label_set_text(s_dev.lbl_wifi, tr("—"));
    }

    char fans[64];
    snprintf(fans, sizeof(fans), tr("模型 %d%% · 热端 %d%% · 辅助 %d/%d%%"),
             p->cooling_fan_pct, p->heatbreak_fan_pct, p->big_fan1_pct, p->big_fan2_pct);
    lv_label_set_text(s_dev.lbl_fans, fans);

    if (p->hms_count > 0 && p->hms_summary[0]) {
        lv_label_set_text(s_dev.lbl_hms, p->hms_summary);
        lv_obj_set_style_text_color(s_dev.lbl_hms, UI_COLOR_DANGER, 0);
    } else {
        lv_label_set_text(s_dev.lbl_hms, tr("无 HMS 告警"));
        lv_obj_set_style_text_color(s_dev.lbl_hms, UI_COLOR_TEXT_SEC, 0);
    }

    char ams[256];
    ams[0] = 0;
    if (p->ams_tray_count > 0) {
        size_t pos = 0;
        for (int i = 0; i < p->ams_tray_count && pos < sizeof(ams) - 48; i++) {
            const bambu_ams_tray_t *t = &p->ams_trays[i];
            if (!t->present) {
                continue;
            }
            int n = snprintf(ams + pos, sizeof(ams) - pos, "T%s %s %s\n",
                             t->tray_id, t->type[0] ? t->type : "?",
                             t->color_hex[0] ? t->color_hex : "—");
            if (n > 0) {
                pos += (size_t)n;
            }
        }
    }
    if (ams[0]) {
        lv_label_set_text(s_dev.lbl_ams, ams);
    } else {
        lv_label_set_text(s_dev.lbl_ams, tr("未检测到 AMS 数据"));
    }
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_ui();
}

static void on_pushall(lv_event_t *e)
{
    (void)e;
    if (bambu_service_request_pushall() == ESP_OK) {
        ui_toast(tr("已请求完整状态同步"), UI_TOAST_INFO);
    } else {
        ui_toast(tr("刷新过于频繁或未连接"), UI_TOAST_ERROR);
    }
}

static void on_light(lv_event_t *e)
{
    bool on = (bool)(intptr_t)lv_event_get_user_data(e);
    const char *which = (const char *)lv_obj_get_user_data(lv_event_get_target(e));
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (which && strcmp(which, "chamber") == 0) {
        err = bambu_service_set_chamber_light(on);
    } else if (which && strcmp(which, "work") == 0) {
        err = bambu_service_set_work_light(on);
    }
    ui_bambu_command_result(err,"灯光");
}

static lv_obj_t *mk_light_btn(lv_obj_t *row, const char *label, const char *tag, bool turn_on)
{
    lv_obj_t *b = ui_create_btn(row, label, LV_PCT(48), UI_BTN_H_SM, UI_BTN_OUTLINE, on_light,
                                (void *)(intptr_t)turn_on);
    lv_obj_set_user_data(b, (void *)tag);
    return b;
}

static bool read_temperature(lv_obj_t *ta,int *value)
{
    const char *text=lv_textarea_get_text(ta);char *end=NULL;
    if(!text || !*text)return false;
    long v=strtol(text,&end,10);while(end && *end==' ')++end;
    if(end==text || !end || *end || v<0 || v>1000)return false;
    *value=(int)v;return true;
}

static void on_set_nozzle(lv_event_t *e)
{ (void)e;int value;if(!read_temperature(s_dev.ta_nozzle,&value)){ui_toast(tr("请输入有效的目标温度"),UI_TOAST_ERROR);return;}ui_bambu_command_result(bambu_service_set_nozzle_target_c(value),"喷头目标温度"); }

static void on_set_bed(lv_event_t *e)
{ (void)e;int value;if(!read_temperature(s_dev.ta_bed,&value)){ui_toast(tr("请输入有效的目标温度"),UI_TOAST_ERROR);return;}ui_bambu_command_result(bambu_service_set_bed_target_c(value),"热床目标温度"); }

static void on_buzzer_off(lv_event_t *e)
{ (void)e;ui_bambu_command_result(bambu_studio_stop_buzzer(),"关闭蜂鸣器"); }

static void on_calib(lv_event_t *e)
{ (void)e;ui_bambu_command_result(bambu_studio_start_calibration(true,true,true,false,false,false,false),"全校准"); }

static void on_timelapse(lv_event_t *e)
{ bool on=(bool)(intptr_t)lv_event_get_user_data(e);ui_bambu_command_result(bambu_studio_camera_timelapse(on),"延时摄影"); }

static void on_ams_reset(lv_event_t *e)
{ (void)e;ui_bambu_command_result(bambu_studio_ams_reset(),"AMS 复位"); }

static lv_obj_t *device_create(lv_obj_t *parent, void *user_data)
{
    (void)user_data;
    memset(&s_dev, 0, sizeof(s_dev));

    const bool land = ui_screen_is_landscape();

    lv_obj_t *root = ui_create_page_root(parent);
    lv_obj_t *content = ui_create_content(root);
    ui_create_page_header(content, tr("设备与 AMS"), tr("LAN 开发者模式控制"), pm_back_cb);

    lv_obj_t *body = content;
    if (land) {
        lv_obj_set_flex_grow(content, 1);
        lv_obj_set_style_min_height(content, 0, 0);
        lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        body = lv_obj_create(content);
        lv_obj_remove_style_all(body);
        lv_obj_set_width(body, LV_PCT(100));
        lv_obj_set_flex_grow(body, 1);
        lv_obj_set_style_min_height(body, 0, 0);
        ui_land_cap_scroll_area(body, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(body, UI_PAD_SM, 0);
    }

    lv_obj_t *col_l = body;
    lv_obj_t *col_r = body;
    if (land) {
        lv_obj_t *cols = lv_obj_create(body);
        lv_obj_remove_style_all(cols);
        lv_obj_set_width(cols, LV_PCT(100));
        lv_obj_set_height(cols, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(cols, ui_land_gap(), 0);
        lv_obj_clear_flag(cols, LV_OBJ_FLAG_SCROLLABLE);
        col_l = lv_obj_create(cols);
        col_r = lv_obj_create(cols);
        lv_obj_remove_style_all(col_l);
        lv_obj_remove_style_all(col_r);
        lv_obj_set_width(col_l, LV_PCT(50));
        lv_obj_set_width(col_r, LV_PCT(50));
        lv_obj_set_height(col_l, LV_SIZE_CONTENT);
        lv_obj_set_height(col_r, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col_l, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_flow(col_r, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(col_l, UI_PAD_SM, 0);
        lv_obj_set_style_pad_row(col_r, UI_PAD_SM, 0);
    }

    lv_obj_t *sys = ui_create_card(col_l);
    ui_create_section_title(sys, tr("系统"));
    s_dev.lbl_fw = ui_create_label(sys, tr("—"), UI_FONT_BODY, UI_COLOR_TEXT);
    s_dev.lbl_wifi = ui_create_label(sys, tr("—"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    ui_create_btn(sys, tr("手动 pushall 同步"), LV_PCT(100), UI_BTN_H_SM, UI_BTN_FILLED, on_pushall,
                  NULL);
    lv_obj_t *srow = ui_create_row(sys);
    ui_create_btn(srow, tr("关蜂鸣器"), LV_PCT(48), UI_BTN_H_SM, UI_BTN_OUTLINE, on_buzzer_off,
                  NULL);
    ui_create_btn(srow, tr("全校准"), LV_PCT(48), UI_BTN_H_SM, UI_BTN_OUTLINE, on_calib, NULL);

    lv_obj_t *light = ui_create_card(col_l);
    ui_create_section_title(light, tr("灯光"));
    lv_obj_t *lrow = ui_create_row(light);
    mk_light_btn(lrow, tr("机箱灯 开"), "chamber", true);
    mk_light_btn(lrow, tr("机箱灯 关"), "chamber", false);
    lv_obj_t *lrow2 = ui_create_row(light);
    mk_light_btn(lrow2, tr("工作灯 开"), "work", true);
    mk_light_btn(lrow2, tr("工作灯 关"), "work", false);

    lv_obj_t *temp = ui_create_card(land ? col_r : body);
    ui_create_section_title(temp, tr("目标温度 (Studio MQTT)"));
    ui_create_label(temp, tr("喷嘴 °C"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    s_dev.ta_nozzle = lv_textarea_create(temp);
    lv_obj_set_width(s_dev.ta_nozzle, LV_PCT(100));
    lv_obj_set_height(s_dev.ta_nozzle, ui_land_input_h());
    lv_textarea_set_one_line(s_dev.ta_nozzle, true);
    ui_style_textarea_field(s_dev.ta_nozzle);
    lv_textarea_set_text(s_dev.ta_nozzle, "220");
    ui_create_btn(temp, tr("设置喷嘴"), LV_PCT(100), UI_BTN_H_SM, UI_BTN_OUTLINE, on_set_nozzle,
                  NULL);
    ui_create_label(temp, tr("热床 °C"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    s_dev.ta_bed = lv_textarea_create(temp);
    lv_obj_set_width(s_dev.ta_bed, LV_PCT(100));
    lv_obj_set_height(s_dev.ta_bed, ui_land_input_h());
    lv_textarea_set_one_line(s_dev.ta_bed, true);
    ui_style_textarea_field(s_dev.ta_bed);
    lv_textarea_set_text(s_dev.ta_bed, "60");
    ui_create_btn(temp, tr("设置热床"), LV_PCT(100), UI_BTN_H_SM, UI_BTN_OUTLINE, on_set_bed, NULL);

    lv_obj_t *fan = ui_create_card(land ? col_r : body);
    ui_create_section_title(fan, tr("风扇"));
    s_dev.lbl_fans = ui_create_label(fan, tr("—"), UI_FONT_BODY, UI_COLOR_TEXT);

    lv_obj_t *hms = ui_create_card(land ? col_r : body);
    ui_create_section_title(hms, tr("HMS"));
    s_dev.lbl_hms = ui_create_label(hms, tr("—"), UI_FONT_BODY, UI_COLOR_TEXT);

    lv_obj_t *ams = ui_create_card(body);
    ui_create_section_title(ams, tr("AMS 槽位"));
    lv_obj_t *arow = ui_create_row(ams);
    ui_create_btn(arow, tr("AMS 复位"), LV_PCT(48), UI_BTN_H_SM, UI_BTN_OUTLINE, on_ams_reset,
                  NULL);
    ui_create_btn(arow, tr("延时摄影"), LV_PCT(48), UI_BTN_H_SM, UI_BTN_OUTLINE, on_timelapse,
                  (void *)(intptr_t)1);
    s_dev.lbl_ams = ui_create_label(ams, tr("—"), UI_FONT_BODY, UI_COLOR_TEXT);
    lv_label_set_long_mode(s_dev.lbl_ams, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_dev.lbl_ams, LV_PCT(100));
    if (land) {
        lv_obj_set_style_max_height(s_dev.lbl_ams, 72, 0);
    }

    s_dev.timer = lv_timer_create(timer_cb, 1000, NULL);
    refresh_ui();
    return root;
}

static void device_destroy(void *user_data)
{
    (void)user_data;
    if (s_dev.timer) {
        lv_timer_delete(s_dev.timer);
        s_dev.timer = NULL;
    }
}

static void device_on_pause(void *user_data)
{
    (void)user_data;
    if (s_dev.timer) {
        lv_timer_pause(s_dev.timer);
    }
}

static void device_on_resume(void *user_data)
{
    (void)user_data;
    if (s_dev.timer) {
        lv_timer_resume(s_dev.timer);
    }
    refresh_ui();
}

const page_t page_device = {
    .name = "设备",
    .create = device_create,
    .destroy = device_destroy,
    .on_pause = device_on_pause,
    .on_resume = device_on_resume,
    .cache = PM_CACHE_KEEP_ALIVE,
    .user_data = NULL,
};
