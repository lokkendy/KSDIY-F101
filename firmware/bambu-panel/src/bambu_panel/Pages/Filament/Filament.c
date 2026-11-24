#include "ui_bambu_command.h"
#include "Filament.h"

#include "bambu_service.h"
#include "bambu_studio_api.h"
#include "page_manager.h"
#include "i18n.h"
#include "ui_bambu.h"
#include "ui_screen.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *lbl_mat;
    lv_obj_t *spool, *empty_ams;
    lv_obj_t *lbl_color;
    lv_obj_t *lbl_remain;
    ui_bambu_ams_col_t slots[4];
    int selected_slot;
    lv_obj_t *unit_select,*load_btn,*unload_btn;
    int unit_ids[BAMBU_AMS_UNITS],unit_count,selected_unit;
    lv_timer_t *timer;
} filament_ui_t;

static filament_ui_t s_fil;

static const bambu_ams_tray_t *selected_tray(const bambu_status_t *p,int slot)
{
    int uid=s_fil.unit_count?s_fil.unit_ids[s_fil.selected_unit]:-1;
    for(unsigned i=0;i<BAMBU_AMS_TRAYS;++i){
        const bambu_ams_tray_t *t=&p->ams_trays[i];
        if(t->unit_id==uid && t->slot_id==slot)return t;
    }
    return NULL;
}

static lv_color_t reported_color(const char *hex)
{
    if(!hex || strlen(hex)<6)return lv_color_hex(0x42484D);
    unsigned color=0;
    for(unsigned i=0;i<6;++i){
        unsigned char c=hex[i];unsigned n;
        if(c>='0' && c<='9')n=c-'0';else if(c>='a'&&c<='f')n=c-'a'+10;
        else if(c>='A'&&c<='F')n=c-'A'+10;else return lv_color_hex(0x42484D);
        color=(color<<4)|n;
    }
    return lv_color_hex(color);
}

static void on_ams_load(lv_event_t *e)
{
    (void)e;
    int slot = s_fil.selected_slot - 1;
    if (slot < 0 || slot > 3) {
        return;
    }
    bambu_svc_snapshot_t snap;
    bambu_service_snapshot(&snap);
    const bambu_ams_tray_t *tray=selected_tray(&snap.printer,slot);
    if(!snap.connected || !tray || !tray->present){ui_toast(tr("该槽位无可用耗材"),UI_TOAST_ERROR);return;}
    int cur = (int)snap.printer.nozzle_temp;
    int tar = (int)snap.printer.nozzle_target;
    if (tar < 180) {
        tar = 220;
    }
    esp_err_t err = bambu_studio_ams_change_filament(true,tray->unit_id,tray->slot_id,cur,tar);
    ui_bambu_command_result(err,"AMS 进料");
}

static void on_ams_unload(lv_event_t *e)
{
    (void)e;
    bambu_svc_snapshot_t snap;
    bambu_service_snapshot(&snap);
    if(!snap.connected || !snap.printer.ams_installed){ui_toast(tr("未安装 AMS"),UI_TOAST_ERROR);return;}
    int unit=s_fil.unit_count?s_fil.unit_ids[s_fil.selected_unit]:0;
    int cur = (int)snap.printer.nozzle_temp;
    int tar = (int)snap.printer.nozzle_target;
    esp_err_t err = bambu_studio_ams_change_filament(false,unit,0,cur,tar);
    ui_bambu_command_result(err,"AMS 退料");
}

static void update_detail(int slot)
{
    if (slot < 1 || slot > 4 || !s_fil.lbl_mat) {
        return;
    }
    s_fil.selected_slot = slot;
    for (int i = 0; i < 4; i++) {
        ui_bambu_ams_column_select(&s_fil.slots[i], (i + 1) == slot);
    }
    bambu_svc_snapshot_t snap;
    bambu_service_snapshot(&snap);
    const bambu_ams_tray_t *t=selected_tray(&snap.printer,slot-1);
    if(snap.connected && snap.printer.has_data && t && t->present){
        lv_label_set_text(s_fil.lbl_mat,t->type[0]?t->type:tr("未知耗材"));
        lv_label_set_text(s_fil.lbl_color,t->color_hex[0]?t->color_hex:"—");
        char remain[40];
        if(t->remain_percent>=0)snprintf(remain,sizeof(remain),"%s %d%%",tr("剩余"),t->remain_percent);
        else snprintf(remain,sizeof(remain),"%s",tr("打印机未上报剩余量"));
        lv_label_set_text(s_fil.lbl_remain,remain);
    }else{
        lv_label_set_text(s_fil.lbl_mat,tr(!snap.connected?"未连接":snap.printer.ams_installed?"空槽位":"未安装 AMS"));
        lv_label_set_text(s_fil.lbl_color,"—");lv_label_set_text(s_fil.lbl_remain,"—");
    }
    if(s_fil.spool)lv_obj_set_style_bg_color(s_fil.spool,t&&t->present?reported_color(t->color_hex):UI_COLOR_CARD_HI,0);
    if(s_fil.load_btn){if(snap.connected && t && t->present)lv_obj_remove_state(s_fil.load_btn,LV_STATE_DISABLED);else lv_obj_add_state(s_fil.load_btn,LV_STATE_DISABLED);}
    if(s_fil.unload_btn){if(snap.connected && snap.printer.ams_installed)lv_obj_remove_state(s_fil.unload_btn,LV_STATE_DISABLED);else lv_obj_add_state(s_fil.unload_btn,LV_STATE_DISABLED);}

}

static void on_slot_click(lv_event_t *e)
{
    update_detail((int)(intptr_t)lv_event_get_user_data(e));
}

static void refresh_slots(void)
{
    bambu_svc_snapshot_t snap;bambu_service_snapshot(&snap);const bambu_status_t *p=&snap.printer;
    int ids[BAMBU_AMS_UNITS],count=0;
    for(unsigned i=0;p->ams_installed && i<BAMBU_AMS_TRAYS;++i){
        int uid=p->ams_trays[i].unit_id;if(uid<0)continue;
        bool found=false;for(int k=0;k<count;++k)if(ids[k]==uid)found=true;
        if(!found && count<BAMBU_AMS_UNITS)ids[count++]=uid;
    }
    if(s_fil.empty_ams){lv_label_set_text(s_fil.empty_ams,tr(!snap.connected?"连接打印机后查看耗材":p->ams_installed?"选择槽位查看耗材信息":"未安装 AMS，可在控制页操作外置耗材"));}
    if(s_fil.unit_select){if(count)lv_obj_remove_state(s_fil.unit_select,LV_STATE_DISABLED);else lv_obj_add_state(s_fil.unit_select,LV_STATE_DISABLED);}
    bool changed=count!=s_fil.unit_count;
    for(int i=0;i<count;++i)if(ids[i]!=s_fil.unit_ids[i])changed=true;
    if(changed){
        s_fil.unit_count=count;memcpy(s_fil.unit_ids,ids,(size_t)count*sizeof(ids[0]));
        if(s_fil.selected_unit>=count)s_fil.selected_unit=0;
        char choices[100]="";size_t used=0;
        for(int i=0;i<count;++i)used+=(size_t)snprintf(choices+used,sizeof(choices)-used,"%sAMS %d",i?"\n":"",ids[i]+1);
        if(!count)snprintf(choices,sizeof(choices),"%s",tr("未安装 AMS"));
        if(s_fil.unit_select){lv_dropdown_set_options(s_fil.unit_select,choices);lv_dropdown_set_selected(s_fil.unit_select,s_fil.selected_unit);}
    }
    for(int i=0;i<4;++i){
        ui_bambu_ams_col_t *c=&s_fil.slots[i];const bambu_ams_tray_t *t=selected_tray(p,i);
        bool present=snap.connected && p->has_data && t && t->present;
        lv_label_set_text(c->lbl_mat,present?(t->type[0]?t->type:tr("未知耗材")):tr(p->ams_installed?"空槽位":"未安装"));
        lv_label_set_text(c->lbl_sub,present&&t->color_hex[0]?t->color_hex:"—");
        int global=t?t->unit_id*4+t->slot_id:-1;
        lv_label_set_text(c->lbl_status,present?tr(p->ams_active_tray==global?"使用中":"待机"):"—");
        ui_bambu_ams_column_tint(c,present?reported_color(t->color_hex):lv_color_hex(0x42484D),present&&t->remain_percent>=0?t->remain_percent:0);
    }
    update_detail(s_fil.selected_slot);
}

static void on_unit_changed(lv_event_t *e)
{
    s_fil.selected_unit=lv_dropdown_get_selected(lv_event_get_target(e));refresh_slots();
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_slots();
}

static lv_obj_t *filament_create(lv_obj_t *parent, void *user_data)
{
    (void)user_data;
    memset(&s_fil, 0, sizeof(s_fil));
    s_fil.selected_slot = 1;

    lv_obj_t *root = ui_create_page_root(parent);
    lv_obj_set_style_bg_color(root, UI_BAMBU_BG, 0);
    lv_obj_t *content = ui_create_content(root);
    ui_create_page_header(content,tr("耗材管理"),tr("AMS"),NULL);
    s_fil.empty_ams=ui_create_label(content,"",UI_FONT_CAPTION,UI_COLOR_TEXT_SEC);
    lv_obj_set_width(s_fil.empty_ams,LV_PCT(100));
    s_fil.unit_select=lv_dropdown_create(content);lv_dropdown_set_options(s_fil.unit_select,tr("未安装 AMS"));
    lv_obj_set_width(s_fil.unit_select,180);
    ui_style_dropdown(s_fil.unit_select);
    lv_obj_add_event_cb(s_fil.unit_select,on_unit_changed,LV_EVENT_VALUE_CHANGED,NULL);

    const bool land = ui_screen_is_landscape();

    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_min_height(content, 0, 0);

    if (!land) {
        lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(content, UI_PAD_SM, 0);

        lv_obj_t *slots = ui_create_card(content);
        lv_obj_set_style_bg_color(slots, UI_BAMBU_CARD, 0);
        ui_create_section_title(slots, tr("AMS 槽位"));
        for (int r = 0; r < 2; r++) {
            lv_obj_t *cols = ui_create_row(slots);
            lv_obj_set_style_pad_column(cols, UI_PAD_SM, 0);
            for (int c = 0; c < 2; c++) {
                int slot = r * 2 + c + 1;
                ui_bambu_ams_column(cols, slot, &s_fil.slots[slot - 1]);
                lv_obj_set_width(s_fil.slots[slot - 1].card, LV_PCT(48));
                lv_obj_set_height(s_fil.slots[slot - 1].card, LV_SIZE_CONTENT);
                lv_obj_add_event_cb(s_fil.slots[slot - 1].card, on_slot_click, LV_EVENT_CLICKED,
                                    (void *)(intptr_t)slot);
            }
        }

        lv_obj_t *detail = ui_create_card(content);
        lv_obj_set_style_bg_color(detail, UI_BAMBU_CARD, 0);
        ui_create_section_title(detail, tr("耗材详情"));
        lv_obj_t *det_row = ui_create_row(detail);
        lv_obj_t *big_spool = lv_obj_create(det_row);
    s_fil.spool=big_spool;
        lv_obj_set_size(big_spool, 72, 72);
        lv_obj_set_style_radius(big_spool, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(big_spool, UI_COLOR_TEXT_SEC, 0);
        lv_obj_clear_flag(big_spool, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *spec = lv_obj_create(det_row);
        lv_obj_remove_style_all(spec);
        lv_obj_set_flex_grow(spec, 1);
        lv_obj_set_flex_flow(spec, LV_FLEX_FLOW_COLUMN);
        s_fil.lbl_mat = ui_create_label(spec, "—", UI_FONT_BODY, UI_COLOR_TEXT);
        s_fil.lbl_color = ui_create_label(spec, "—", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        s_fil.lbl_remain = ui_create_label(spec, "—", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        lv_obj_t *act = ui_create_row(detail);
        s_fil.unload_btn=ui_create_btn(act, tr("退料"), LV_PCT(48), ui_land_btn_h_sm(), UI_BTN_OUTLINE, on_ams_unload,
                      NULL);
        s_fil.load_btn=ui_create_btn(act, tr("进料"), LV_PCT(48), ui_land_btn_h_sm(), UI_BTN_FILLED, on_ams_load,
                      NULL);
        s_fil.timer = lv_timer_create(timer_cb, 1000, NULL);
        refresh_slots();
        return root;
    }

    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_style_min_height(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_PAD_SM, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *slots = ui_create_card(row);
    lv_obj_set_style_bg_color(slots, UI_BAMBU_CARD, 0);
    lv_obj_set_flex_grow(slots, 3);
    lv_obj_set_height(slots, LV_PCT(100));
    ui_create_section_title(slots, tr("AMS 槽位"));
    lv_obj_t *cols = ui_create_row(slots);
    lv_obj_set_flex_grow(cols, 1);
    lv_obj_set_height(cols, LV_PCT(100));
    for (int i = 0; i < 4; i++) {
        ui_bambu_ams_column(cols, i + 1, &s_fil.slots[i]);
        lv_obj_add_event_cb(s_fil.slots[i].card, on_slot_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)(i + 1));
    }

    lv_obj_t *detail = ui_create_card(row);
    lv_obj_set_style_bg_color(detail, UI_BAMBU_CARD, 0);
    lv_obj_set_flex_grow(detail, 2);
    ui_create_section_title(detail, tr("耗材详情"));
    lv_obj_t *det_row = ui_create_row(detail);
    lv_obj_t *big_spool = lv_obj_create(det_row);
    s_fil.spool=big_spool;
    lv_obj_set_size(big_spool, 72, 72);
    lv_obj_set_style_radius(big_spool, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(big_spool, UI_COLOR_TEXT_SEC, 0);
    lv_obj_clear_flag(big_spool, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *spec = lv_obj_create(det_row);
    lv_obj_remove_style_all(spec);
    lv_obj_set_flex_grow(spec, 1);
    lv_obj_set_flex_flow(spec, LV_FLEX_FLOW_COLUMN);
    s_fil.lbl_mat = ui_create_label(spec, "—", UI_FONT_BODY, UI_COLOR_TEXT);
    s_fil.lbl_color = ui_create_label(spec, "—", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    s_fil.lbl_remain = ui_create_label(spec, "—", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

    lv_obj_t *act = ui_create_row(detail);
    s_fil.unload_btn=ui_create_btn(act, tr("退料"), LV_PCT(48), ui_land_btn_h_sm(), UI_BTN_OUTLINE, on_ams_unload,
                  NULL);
    s_fil.load_btn=ui_create_btn(act, tr("进料"), LV_PCT(48), ui_land_btn_h_sm(), UI_BTN_FILLED, on_ams_load,
                  NULL);
    s_fil.timer = lv_timer_create(timer_cb, 1000, NULL);
    refresh_slots();
    return root;
}

static void filament_destroy(void *user_data)
{
    (void)user_data;
    if (s_fil.timer) {
        lv_timer_delete(s_fil.timer);
        s_fil.timer = NULL;
    }
}

const page_t page_filament = {
    .name = "耗材",
    .create = filament_create,
    .destroy = filament_destroy,
    .cache = PM_CACHE_KEEP_ALIVE,
    .user_data = NULL,
};
