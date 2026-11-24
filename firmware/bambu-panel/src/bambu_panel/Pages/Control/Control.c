#include "Control.h"

#include "JobModel.h"
#include "bambu_service.h"
#include "bambu_studio_api.h"
#include "page_manager.h"
#include "i18n.h"
#include "ui_bambu.h"
#include "ui_screen.h"
#include "bambu_profile.h"
#include "ui_bambu_command.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float step_mm;



    lv_obj_t *lbl_x;
    lv_obj_t *lbl_y;
    lv_obj_t *lbl_z;
    lv_obj_t *lbl_nozzle_tgt;
    lv_obj_t *lbl_bed_tgt;
    lv_obj_t *lbl_status;
    lv_obj_t *lbl_fans[3];
    lv_obj_t *step_btns[3];
    int step_idx;
    lv_obj_t *btn_pause, *btn_stop;
    lv_obj_t *motion[12], *extrusion[2], *temperature[12];
    unsigned motion_count, extrusion_count, temperature_count;
    uint32_t temp_seq, temp_started;
    lv_timer_t *timer;
} ctrl_ui_t;

static ctrl_ui_t s_ctrl;

static bool manual_allowed(bool extrude)
{
    bambu_svc_snapshot_t s;bambu_service_snapshot(&s);
    bool idle=s.printer.state==BAMBU_PRINT_IDLE || s.printer.state==BAMBU_PRINT_FINISHED || s.printer.state==BAMBU_PRINT_FAILED;
    return s.connected && s.printer.has_data && idle && (!extrude || s.printer.nozzle_temp>=170.f);
}

static lv_obj_t *tracked_button(lv_obj_t *parent,const char *text,int32_t w,int32_t h,
                                ui_btn_style_t style,lv_event_cb_t cb,void *data,int group)
{
    lv_obj_t *btn=ui_create_btn(parent,text,w,h,style,cb,data);
    if(group==0 && s_ctrl.motion_count<12)s_ctrl.motion[s_ctrl.motion_count++]=btn;
    if(group==1 && s_ctrl.extrusion_count<2)s_ctrl.extrusion[s_ctrl.extrusion_count++]=btn;
    if(group==2 && s_ctrl.temperature_count<12)s_ctrl.temperature[s_ctrl.temperature_count++]=btn;
    return btn;
}
#define motion_btn(...) tracked_button(__VA_ARGS__,0)
#define extrusion_btn(...) tracked_button(__VA_ARGS__,1)
#define temperature_btn(...) tracked_button(__VA_ARGS__,2)

static void set_enabled(lv_obj_t *button,bool enabled)
{
    if(!button)return;
    if(enabled)lv_obj_remove_state(button,LV_STATE_DISABLED);
    else lv_obj_add_state(button,LV_STATE_DISABLED);
}

static void jog(char axis,float dir)
{
    esp_err_t rc=manual_allowed(false)?bambu_service_jog_axis(axis,s_ctrl.step_mm*dir,3000):ESP_ERR_INVALID_STATE;
    ui_bambu_command_result(rc,"手动移动");
}

static void on_jog(lv_event_t *e)
{
    const char *tag = (const char *)lv_event_get_user_data(e);
    if (!tag) {
        return;
    }
    if (strcmp(tag, "X+") == 0) {
        jog('X', 1);
    } else if (strcmp(tag, "X-") == 0) {
        jog('X', -1);
    } else if (strcmp(tag, "Y+") == 0) {
        jog('Y', 1);
    } else if (strcmp(tag, "Y-") == 0) {
        jog('Y', -1);
    } else if (strcmp(tag, "Z+") == 0) {
        jog('Z', 1);
    } else if (strcmp(tag, "Z-") == 0) {
        jog('Z', -1);
    }
}

static void on_step(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_ctrl.step_idx = idx;
    if (idx == 1) {
        s_ctrl.step_mm = 1.f;
    } else if (idx == 2) {
        s_ctrl.step_mm = 10.f;
    } else {
        s_ctrl.step_mm = 0.1f;
    }
    ui_bambu_step_pills_set_active(s_ctrl.step_btns, idx);
}

static void on_home(lv_event_t *e)
{ (void)e;ui_bambu_command_result(manual_allowed(false)?bambu_service_home_axes():ESP_ERR_INVALID_STATE,"回零"); }

static void on_extrude(lv_event_t *e)
{
    bool out=(bool)(intptr_t)lv_event_get_user_data(e);
    ui_bambu_command_result(manual_allowed(true)?bambu_service_extrude_mm(out?10.f:-10.f):ESP_ERR_INVALID_STATE,out?"进料":"退料");
}

static void send_temperature(bool bed,int value)
{
    bambu_svc_snapshot_t snap;bambu_service_snapshot(&snap);
    if(!snap.connected || !snap.printer.has_data || s_ctrl.temp_seq){
        ui_toast(tr(s_ctrl.temp_seq?"请等待上一条指令确认":"请先连接并同步打印机状态"),UI_TOAST_INFO);return;
    }
    esp_err_t rc=bed?bambu_service_set_bed_target_c(value):bambu_service_set_nozzle_target_c(value);
    ui_bambu_command_result(rc,bed?"热床目标温度":"喷头目标温度");
    if(rc==ESP_OK){s_ctrl.temp_seq=bambu_service_last_seq();s_ctrl.temp_started=lv_tick_get();}
}

static void on_temp_preset(lv_event_t *e)
{
    intptr_t pack=(intptr_t)lv_event_get_user_data(e);
    send_temperature((pack>>16)!=0,(int)(pack&0xffff));
}

static void on_temp_adj(lv_event_t *e)
{
    intptr_t pack=(intptr_t)lv_event_get_user_data(e);
    bool bed=(pack&2)!=0;
    bambu_svc_snapshot_t snap;bambu_service_snapshot(&snap);
    if(!snap.connected || !snap.printer.has_data){ui_toast(tr("请先连接并同步打印机状态"),UI_TOAST_ERROR);return;}
    int value=(int)(bed?snap.printer.bed_target:snap.printer.nozzle_target);
    send_temperature(bed,value+((pack&1)?5:-5));
}

static void on_pause(lv_event_t *e)
{
    (void)e;JobModel_t m;JobModel_GetSnapshot(&m, NULL);
    ui_bambu_command_result(m.can_resume?bambu_service_resume():m.can_pause?bambu_service_pause():ESP_ERR_INVALID_STATE,m.can_resume?"继续打印":"暂停打印");
}

static void on_stop(lv_event_t *e)
{
    (void)e;JobModel_t m;JobModel_GetSnapshot(&m, NULL);
    ui_bambu_command_result(m.can_cancel?bambu_service_stop_print():ESP_ERR_INVALID_STATE,"停止打印");
}

static void on_level_stub(lv_event_t *e)
{
    (void)e;ui_bambu_command_result(manual_allowed(false)?bambu_studio_start_calibration(false,true,false,false,false,false,false):ESP_ERR_INVALID_STATE,"自动调平");
}

static void on_speed(lv_event_t *e)
{ ui_bambu_command_result(bambu_service_set_speed_level((int)(intptr_t)lv_event_get_user_data(e)),"打印速度"); }
static void on_fan(lv_event_t *e)
{
    unsigned packed=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    unsigned fan=packed>>8,percent=packed&255;
    if(fan<1||fan>3||percent>100)return;
    /* Studio DevFan legacy protocol: part=1, auxiliary=2, chamber=3. */
    char gcode[40];snprintf(gcode,sizeof(gcode),"M106 P%u S%u",fan,(percent*255+50)/100);
    esp_err_t rc=bambu_service_send_gcode_line(gcode);
    ui_bambu_command_result(rc,"风扇");
}
static void build_fan_speed(lv_obj_t *parent)
{
    lv_obj_t *card=ui_create_card(parent);ui_create_section_title(card,tr("打印速度"));
    lv_obj_t *speeds=ui_create_row(card);const char *names[]={"静音","标准","运动","狂暴"};
    for(int i=0;i<4;++i)ui_create_btn(speeds,tr(names[i]),0,36,UI_BTN_OUTLINE,on_speed,(void*)(intptr_t)(i+1));
    card=ui_create_card(parent);ui_create_section_title(card,tr("风扇"));
    bambu_lan_config_t profile;bambu_profile_load(&profile);
    const char *fan_names[]={"模型风扇","辅助风扇","机箱风扇"};
    int count=bambu_model_supports_chamber(profile.model)?3:1;
    for(int i=0;i<count;++i){
        lv_obj_t *head=ui_create_row(card);ui_create_label(head,tr(fan_names[i]),UI_FONT_BODY,UI_COLOR_TEXT);
        s_ctrl.lbl_fans[i]=ui_create_label(head,"—",UI_FONT_CAPTION,UI_COLOR_TEXT_SEC);
        lv_obj_t *row=ui_create_row(card);
        for(int p=0;p<=100;p+=25){char label[12];snprintf(label,sizeof(label),"%d%%",p);ui_create_btn(row,label,0,34,UI_BTN_OUTLINE,on_fan,(void*)(uintptr_t)(((i+1)<<8)|p));}
    }
}
static void refresh_ui(void)
{
    JobModel_t m;
    JobModel_GetSnapshot(&m, NULL);
    lv_label_set_text(s_ctrl.lbl_status, tr(m.state_text));
    bambu_svc_snapshot_t snap;
    bambu_service_snapshot(&snap);
    int fan_values[]={snap.printer.cooling_fan_pct,snap.printer.big_fan1_pct,snap.printer.big_fan2_pct};
    for(int i=0;i<3;++i)if(s_ctrl.lbl_fans[i]){char text[16];if(snap.connected&&snap.printer.has_data)snprintf(text,sizeof(text),"%d%%",fan_values[i]);else snprintf(text,sizeof(text),"—");lv_label_set_text(s_ctrl.lbl_fans[i],text);}
    if(s_ctrl.temp_seq && (!snap.connected || lv_tick_get()-s_ctrl.temp_started>=10000 ||
       bambu_service_ack_query(s_ctrl.temp_seq,NULL,0)!=BAMBU_ACK_PENDING))s_ctrl.temp_seq=0;
    for(unsigned i=0;i<s_ctrl.motion_count;++i)set_enabled(s_ctrl.motion[i],manual_allowed(false));
    for(unsigned i=0;i<s_ctrl.extrusion_count;++i)set_enabled(s_ctrl.extrusion[i],manual_allowed(true));
    for(unsigned i=0;i<s_ctrl.temperature_count;++i)set_enabled(s_ctrl.temperature[i],snap.connected&&snap.printer.has_data&&!s_ctrl.temp_seq);
    set_enabled(s_ctrl.btn_pause,m.can_pause||m.can_resume);set_enabled(s_ctrl.btn_stop,m.can_cancel);
    if(s_ctrl.btn_pause){lv_obj_t *label=lv_obj_get_child(s_ctrl.btn_pause,0);if(label)lv_label_set_text(label,tr(m.can_resume?"继续":"暂停"));}
    if (snap.connected && snap.printer.has_data) {
        char n[16];
        snprintf(n, sizeof(n), "%.0f", snap.printer.nozzle_target);
        lv_label_set_text(s_ctrl.lbl_nozzle_tgt, n);
        snprintf(n, sizeof(n), "%.0f", snap.printer.bed_target);
        lv_label_set_text(s_ctrl.lbl_bed_tgt, n);
    } else {
        lv_label_set_text(s_ctrl.lbl_nozzle_tgt,"—");lv_label_set_text(s_ctrl.lbl_bed_tgt,"—");
    }
}

static void refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_ui();
}

static void control_build_action_row(lv_obj_t *parent)
{
    lv_obj_t *act = ui_create_row(parent);
    const lv_coord_t act_h = ui_land_btn_h_sm();
    motion_btn(act, tr("回零"), 0, act_h, UI_BTN_OUTLINE, on_home, NULL);
    motion_btn(act, tr("自动调平"), 0, act_h, UI_BTN_OUTLINE, on_level_stub, NULL);
    s_ctrl.btn_pause=ui_create_btn(act, tr("暂停"), 0, act_h, UI_BTN_FILLED, on_pause, NULL);
    s_ctrl.btn_stop=ui_create_btn(act, tr("停止"), 0, act_h, UI_BTN_DANGER, on_stop, NULL);
}

static void control_build_xyz_card(lv_obj_t *left, bool with_actions)
{
    const bool land = ui_screen_is_landscape();

    lv_obj_set_style_bg_color(left, UI_BAMBU_CARD, 0);
    ui_create_section_title(left, tr("XYZ 移动控制"));
    if (land) {
        lv_obj_t *prow = ui_create_row(left);
        const char *labels[] = { "0.1", "1", "10" };
        for (int i = 0; i < 3; i++) {
            s_ctrl.step_btns[i] = ui_create_btn(prow, labels[i], LV_PCT(32), 28, UI_BTN_OUTLINE,
                                                on_step, (void *)(intptr_t)i);
        }
        ui_bambu_step_pills_set_active(s_ctrl.step_btns, 0);
    } else {
        ui_bambu_step_pills_create(left, s_ctrl.step_btns, on_step);
    }

    lv_obj_t *pad_row = ui_create_row(left);
    if (!land) {
        lv_obj_set_flex_grow(pad_row, 1);
        lv_obj_set_style_min_height(pad_row, 116, 0);
    } else {
        lv_obj_set_flex_grow(pad_row, 0);
    }
    lv_obj_set_style_pad_column(pad_row, UI_PAD_SM, 0);
    lv_obj_set_flex_align(pad_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const int jog_sz = land ? 38 : 44;
    const int jog_h = land ? 32 : 40;
    const int jog_gap = land ? 2 : 4;
    const lv_coord_t dpad_w = (lv_coord_t)(jog_sz * 3 + jog_gap * 2);

    lv_obj_t *col = lv_obj_create(pad_row);
    lv_obj_remove_style_all(col);
    lv_obj_set_width(col, dpad_w);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, jog_gap, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    motion_btn(col, LV_SYMBOL_UP, jog_sz, jog_h, UI_BTN_OUTLINE, on_jog, (void *)"Y+");
    lv_obj_t *mid = ui_create_row(col);
    lv_obj_set_width(mid, dpad_w);
    lv_obj_set_style_pad_column(mid, jog_gap, 0);
    motion_btn(mid, LV_SYMBOL_LEFT, jog_sz, jog_h, UI_BTN_OUTLINE, on_jog, (void *)"X-");
    motion_btn(mid, LV_SYMBOL_HOME, jog_sz, jog_h, UI_BTN_FILLED, on_home, NULL);
    motion_btn(mid, LV_SYMBOL_RIGHT, jog_sz, jog_h, UI_BTN_OUTLINE, on_jog, (void *)"X+");
    motion_btn(col, LV_SYMBOL_DOWN, jog_sz, jog_h, UI_BTN_OUTLINE, on_jog, (void *)"Y-");

    lv_obj_t *zcol = lv_obj_create(pad_row);
    lv_obj_remove_style_all(zcol);
    lv_obj_set_width(zcol, jog_sz);
    lv_obj_set_flex_flow(zcol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(zcol, jog_gap, 0);
    motion_btn(zcol, "Z+", jog_sz, jog_h, UI_BTN_OUTLINE, on_jog, (void *)"Z+");
    motion_btn(zcol, "Z-", jog_sz, jog_h, UI_BTN_OUTLINE, on_jog, (void *)"Z-");

    if (land) {
        lv_obj_t *coord = ui_create_row(left);
        lv_obj_set_style_pad_column(coord, UI_PAD, 0);
        s_ctrl.lbl_x = ui_create_label(coord, "X: —", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        s_ctrl.lbl_y = ui_create_label(coord, "Y: —", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        s_ctrl.lbl_z = ui_create_label(coord, "Z: —", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    } else {
        s_ctrl.lbl_x = ui_create_label(left, "X: —", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        s_ctrl.lbl_y = ui_create_label(left, "Y: —", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        s_ctrl.lbl_z = ui_create_label(left, "Z: —", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    }

    if (with_actions) {
        control_build_action_row(left);
    }
}

static void control_build_temp_stack(lv_obj_t *parent)
{
    lv_obj_t *ex_card = ui_create_card(parent);
    lv_obj_set_style_bg_color(ex_card, UI_BAMBU_CARD, 0);
    ui_create_section_title(ex_card, tr("挤出控制"));
    lv_obj_t *ex = ui_create_row(ex_card);
    extrusion_btn(ex, tr("退料 10 mm"), LV_PCT(48), ui_land_btn_h_sm(), UI_BTN_OUTLINE, on_extrude,
                  (void *)(intptr_t)0);
    extrusion_btn(ex, tr("进料 10 mm"), LV_PCT(48), ui_land_btn_h_sm(), UI_BTN_FILLED, on_extrude,
                  (void *)(intptr_t)1);

    lv_obj_t *noz = ui_create_card(parent);
    lv_obj_set_style_bg_color(noz, UI_BAMBU_CARD, 0);
    ui_create_label(noz, tr("喷头 °C"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_t *nr = ui_create_row(noz);
    temperature_btn(nr, LV_SYMBOL_MINUS, 40, 36, UI_BTN_OUTLINE, on_temp_adj, (void *)(intptr_t)0);
    s_ctrl.lbl_nozzle_tgt = ui_create_label(nr, "—", UI_FONT_NUM_SM, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(s_ctrl.lbl_nozzle_tgt, 1);
    temperature_btn(nr, LV_SYMBOL_PLUS, 40, 36, UI_BTN_OUTLINE, on_temp_adj, (void *)(intptr_t)1);
    lv_obj_t *np = ui_create_row(noz);
    for (int t = 180; t <= 240; t += 20) {
        char tb[8];
        snprintf(tb, sizeof(tb), "%d", t);
        temperature_btn(np, tb, LV_PCT(23), 32, UI_BTN_OUTLINE, on_temp_preset, (void *)(intptr_t)t);
    }

    lv_obj_t *bed = ui_create_card(parent);
    lv_obj_set_style_bg_color(bed, UI_BAMBU_CARD, 0);
    ui_create_label(bed, tr("热床 °C"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_t *br = ui_create_row(bed);
    temperature_btn(br, LV_SYMBOL_MINUS, 40, 36, UI_BTN_OUTLINE, on_temp_adj, (void *)(intptr_t)2);
    s_ctrl.lbl_bed_tgt = ui_create_label(br, "—", UI_FONT_NUM_SM, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(s_ctrl.lbl_bed_tgt, 1);
    temperature_btn(br, LV_SYMBOL_PLUS, 40, 36, UI_BTN_OUTLINE, on_temp_adj, (void *)(intptr_t)3);
    int beds[] = { 40, 60, 80, 100 };
    lv_obj_t *bpr = ui_create_row(bed);
    for (size_t i = 0; i < 4; i++) {
        char tb[8];
        snprintf(tb, sizeof(tb), "%d", beds[i]);
        temperature_btn(bpr, tb, LV_PCT(23), 32, UI_BTN_OUTLINE, on_temp_preset,
                      (void *)(intptr_t)(beds[i] | (1 << 16)));
    }

    lv_obj_t *st = ui_create_card(parent);
    lv_obj_set_style_bg_color(st, UI_BAMBU_CARD, 0);
    ui_create_section_title(st, tr("当前状态"));
    s_ctrl.lbl_status = ui_create_label(st, tr("—"), UI_FONT_BODY, UI_COLOR_PRIMARY);
    build_fan_speed(parent);
}

static lv_obj_t *control_create(lv_obj_t *parent, void *user_data)
{
    (void)user_data;
    memset(&s_ctrl, 0, sizeof(s_ctrl));
    s_ctrl.step_mm = 0.1f;

    const bool land = ui_screen_is_landscape();

    lv_obj_t *root = ui_create_page_root(parent);
    lv_obj_set_style_bg_color(root, UI_BAMBU_BG, 0);
    lv_obj_t *content = ui_create_content(root);
    ui_create_page_header(content, tr("控制"), tr("XYZ · 温度"), pm_back_cb);

    if (!land) {
        lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(content, UI_PAD_SM, 0);
        lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_t *xyz = ui_create_card(content);
        control_build_xyz_card(xyz, true);
        control_build_temp_stack(content);
        s_ctrl.timer = lv_timer_create(refresh_timer_cb, 1000, NULL);
        refresh_ui();
        return root;
    }

    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_style_min_height(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_PAD_SM, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_min_height(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *left_col = lv_obj_create(row);
    lv_obj_remove_style_all(left_col);
    lv_obj_set_width(left_col, LV_PCT(58));
    lv_obj_set_height(left_col, LV_PCT(100));
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left_col, 2, 0);
    lv_obj_clear_flag(left_col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = ui_create_card(left_col);
    lv_obj_set_width(left, LV_PCT(100));
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_style_min_height(left, 0, 0);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_clip_corner(left, true, 0);
    control_build_xyz_card(left, false);
    control_build_action_row(left_col);

    lv_obj_t *right = lv_obj_create(row);
    lv_obj_remove_style_all(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, UI_PAD_SM, 0);
    lv_obj_add_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(right, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(right, LV_DIR_VER);

    control_build_temp_stack(right);

    s_ctrl.timer = lv_timer_create(refresh_timer_cb, 1000, NULL);
    refresh_ui();
    return root;
}

static void control_destroy(void *user_data)
{
    (void)user_data;
    if (s_ctrl.timer) {
        lv_timer_delete(s_ctrl.timer);
        s_ctrl.timer = NULL;
    }
}

const page_t page_control = {
    .name = "控制",
    .create = control_create,
    .destroy = control_destroy,
    .cache = PM_CACHE_KEEP_ALIVE,
    .user_data = NULL,
};

