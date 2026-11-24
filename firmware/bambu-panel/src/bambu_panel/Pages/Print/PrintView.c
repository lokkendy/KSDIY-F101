#include "PrintView.h"

#include "bambu_model.h"
#include "i18n.h"
#include "sdkconfig.h"
#include "ui_keyboard.h"
#include "ui_screen.h"

#ifndef CONFIG_APP_BAMBU_DEFAULT_NICKNAME
#define CONFIG_APP_BAMBU_DEFAULT_NICKNAME "My Printer"
#endif

static void on_pwd_eye(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_user_data(e);
    bool hidden = lv_textarea_get_password_mode(ta);
    lv_textarea_set_password_mode(ta, !hidden);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_label_set_text(lv_obj_get_child(btn, 0),
                      hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static lv_obj_t *mk_input(lv_obj_t *parent, const char *ph, bool flex_grow_in_row)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    if (flex_grow_in_row) {
        lv_obj_set_flex_grow(ta, 1);
        lv_obj_set_width(ta, 0);
    } else {
        lv_obj_set_width(ta, LV_PCT(100));
    }
    lv_obj_set_height(ta, ui_land_input_h());
    lv_textarea_set_placeholder_text(ta, ph);
    lv_textarea_set_one_line(ta, true);
    ui_style_textarea_field(ta);
    ui_keyboard_bind(ta);
    return ta;
}

void PrintView_Create(PrintView_t *v, lv_obj_t *parent,
                      const bambu_lan_config_t *saved,
                      lv_event_cb_t on_model_changed,
                      lv_event_cb_t on_discover,
                      lv_event_cb_t on_save,
                      lv_event_cb_t on_pause,
                      lv_event_cb_t on_resume,
                      lv_event_cb_t on_stop,
                      lv_event_cb_t on_speed)
{
    const bool land = ui_screen_is_landscape();
    const int btn_h = land ? ui_land_btn_h_sm() : UI_BTN_H;

    bambu_model_t model = saved ? saved->model : BAMBU_MODEL_P1P;
    if (model == BAMBU_MODEL_UNKNOWN) {
        model = BAMBU_MODEL_P1P;
    }

    lv_obj_t *root = ui_create_page_root(parent);
    v->root = root;
    if (land) {
        lv_obj_set_style_min_height(root, 0, 0);
    }
    lv_obj_t *content = ui_create_content(root);
    ui_create_page_header(content, tr("打印控制"), NULL, pm_back_cb);
    if (land) {
        lv_obj_set_flex_grow(content, 1);
        lv_obj_set_style_min_height(content, 0, 0);
        lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t *form = content;
    lv_obj_t *land_left = NULL;
    lv_obj_t *land_right = NULL;
    if (land) {
        form = lv_obj_create(content);
        lv_obj_remove_style_all(form);
        lv_obj_set_width(form, LV_PCT(100));
        lv_obj_set_flex_grow(form, 1);
        lv_obj_set_style_min_height(form, 0, 0);
        lv_obj_set_height(form, LV_PCT(100));
        lv_obj_set_flex_flow(form, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(form, ui_land_gap(), 0);
        lv_obj_clear_flag(form, LV_OBJ_FLAG_SCROLLABLE);

        land_left = lv_obj_create(form);
        land_right = lv_obj_create(form);
        for (lv_obj_t *c = land_left; c; c = (c == land_left) ? land_right : NULL) {
            lv_obj_remove_style_all(c);
            lv_obj_set_width(c, c == land_left ? LV_PCT(38) : LV_PCT(62));
            lv_obj_set_height(c, LV_PCT(100));
            lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(c, UI_PAD_SM, 0);
            lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_AUTO);
            lv_obj_set_scroll_dir(c, LV_DIR_VER);
        }
        ui_land_cap_scroll_area(form, ui_land_btn_h() + UI_PAD_SM * 4);
    }

    lv_obj_t *st_parent = land ? land_left : form;
    lv_obj_t *st = ui_create_card(st_parent);
    v->lbl_status = ui_create_label(st, tr("未连接"), UI_FONT_BODY, UI_COLOR_TEXT_SEC);

    lv_obj_t *ctrl = ui_create_card(land ? land_left : form);
    lv_obj_t *row1 = ui_create_row(ctrl);
    ui_create_btn(row1, tr("暂停"), LV_PCT(32), land ? 38 : 48, UI_BTN_OUTLINE, on_pause, NULL);
    ui_create_btn(row1, tr("继续"), LV_PCT(32), land ? 38 : 48, UI_BTN_OUTLINE, on_resume, NULL);
    ui_create_btn(row1, tr("取消"), LV_PCT(32), land ? 38 : 48, UI_BTN_OUTLINE, on_stop, NULL);

    if (land) {
        ui_create_label(ctrl, tr("速度档"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        lv_obj_t *spd_row = ui_create_row(ctrl);
        for (int i = 1; i <= 4; i++) {
            char lbl[8];
            snprintf(lbl, sizeof(lbl), "%d", i);
            ui_create_btn(spd_row, lbl, LV_PCT(23), 32, UI_BTN_FILLED, on_speed,
                          (void *)(intptr_t)i);
        }
    } else {
        lv_obj_t *spd = ui_create_card(form);
        ui_create_label(spd, tr("打印速度档"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        lv_obj_t *spd_row = ui_create_row(spd);
        for (int i = 1; i <= 4; i++) {
            char lbl[8];
            snprintf(lbl, sizeof(lbl), "%d", i);
            ui_create_btn(spd_row, lbl, LV_PCT(23), 40, UI_BTN_FILLED, on_speed,
                          (void *)(intptr_t)i);
        }
    }

    lv_obj_t *lan = ui_create_card(land ? land_right : form);
    ui_create_label(lan,
                    land ? tr("LAN MQTT · mqtts://IP:8883 · bblp") :
                           tr("LAN MQTT（mqtts://IP:8883, 用户 bblp）"),
                    UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    ui_create_btn(lan, tr("扫描局域网打印机"), LV_PCT(100), btn_h, UI_BTN_OUTLINE, on_discover,
                  NULL);
    ui_create_label(lan, tr("机型"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    v->dd_model = lv_dropdown_create(lan);
    lv_obj_set_width(v->dd_model, LV_PCT(100));
    lv_dropdown_set_options(v->dd_model, bambu_model_dropdown_text());
    ui_style_dropdown(v->dd_model);
    lv_dropdown_set_selected(v->dd_model, bambu_model_to_ui_index(model));
    if (on_model_changed) {
        lv_obj_add_event_cb(v->dd_model, on_model_changed, LV_EVENT_VALUE_CHANGED, v);
    }

    v->lbl_lan_hint = ui_create_label(lan, bambu_model_lan_hint(model), UI_FONT_CAPTION,
                                      UI_COLOR_TEXT_SEC);
    if (land) {
        lv_label_set_long_mode(v->lbl_lan_hint, LV_LABEL_LONG_DOT);
        lv_obj_set_style_max_height(v->lbl_lan_hint, lv_font_get_line_height(UI_FONT_CAPTION), 0);
    } else {
        lv_label_set_long_mode(v->lbl_lan_hint, LV_LABEL_LONG_WRAP);
    }
    lv_obj_set_width(v->lbl_lan_hint, LV_PCT(100));

    if (land) {
        lv_obj_t *ip_sn = ui_create_row(lan);
        lv_obj_t *ip_col = lv_obj_create(ip_sn);
        lv_obj_remove_style_all(ip_col);
        lv_obj_set_width(ip_col, LV_PCT(48));
        lv_obj_set_flex_flow(ip_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(ip_col, LV_OBJ_FLAG_SCROLLABLE);
        ui_create_label(ip_col, tr("打印机 IP"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        v->ta_ip = mk_input(ip_col, "192.168.0.100", false);

        lv_obj_t *sn_col = lv_obj_create(ip_sn);
        lv_obj_remove_style_all(sn_col);
        lv_obj_set_width(sn_col, LV_PCT(48));
        lv_obj_set_flex_flow(sn_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(sn_col, LV_OBJ_FLAG_SCROLLABLE);
        ui_create_label(sn_col, tr("序列号 SN"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        v->ta_serial = mk_input(sn_col, "01P00…", false);

        lv_obj_t *code_name = ui_create_row(lan);
        lv_obj_t *code_col = lv_obj_create(code_name);
        lv_obj_remove_style_all(code_col);
        lv_obj_set_flex_grow(code_col, 1);
        lv_obj_set_width(code_col, 0);
        lv_obj_set_flex_flow(code_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(code_col, LV_OBJ_FLAG_SCROLLABLE);
        ui_create_label(code_col, tr("LAN 访问码"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        lv_obj_t *code_row = ui_create_row(code_col);
        v->ta_code = mk_input(code_row, "12345678", true);
        lv_textarea_set_password_mode(v->ta_code, true);
        ui_create_btn(code_row, LV_SYMBOL_EYE_OPEN, 36, 36, UI_BTN_OUTLINE, on_pwd_eye, v->ta_code);

        lv_obj_t *name_col = lv_obj_create(code_name);
        lv_obj_remove_style_all(name_col);
        lv_obj_set_width(name_col, LV_PCT(48));
        lv_obj_set_flex_flow(name_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(name_col, LV_OBJ_FLAG_SCROLLABLE);
        ui_create_label(name_col, tr("显示名称"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        v->ta_name = mk_input(name_col, CONFIG_APP_BAMBU_DEFAULT_NICKNAME, false);
    } else {
        ui_create_label(lan, tr("打印机 IP"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        v->ta_ip = mk_input(lan, "192.168.0.100", false);
        ui_create_label(lan, tr("序列号 SN"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        v->ta_serial = mk_input(lan, "01P00… / 01S00… / 039…", false);
        ui_create_label(lan, tr("LAN 访问码"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        lv_obj_t *code_row = ui_create_row(lan);
        v->ta_code = mk_input(code_row, "12345678", true);
        lv_textarea_set_password_mode(v->ta_code, true);
        ui_create_btn(code_row, LV_SYMBOL_EYE_OPEN, 46, 46, UI_BTN_OUTLINE, on_pwd_eye, v->ta_code);
        ui_create_label(lan, tr("显示名称"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        v->ta_name = mk_input(lan, CONFIG_APP_BAMBU_DEFAULT_NICKNAME, false);
    }

    lv_obj_t *footer = ui_create_page_footer(root);
    ui_create_btn(footer, tr("保存并连接"), LV_PCT(100), ui_land_btn_h(), UI_BTN_FILLED, on_save,
                  NULL);

    if (saved) {
        if (saved->printer_ip[0]) {
            lv_textarea_set_text(v->ta_ip, saved->printer_ip);
        }
        if (saved->serial[0]) {
            lv_textarea_set_text(v->ta_serial, saved->serial);
        }
        if (saved->access_code[0]) {
            lv_textarea_set_text(v->ta_code, saved->access_code);
        }
        if (saved->nickname[0]) {
            lv_textarea_set_text(v->ta_name, saved->nickname);
        }
    }
}
