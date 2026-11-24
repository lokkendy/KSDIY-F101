#include "ui_bambu.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "bambu_cloud.h"
#include "bambu_discovery.h"
#include "bambu_profile.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_dispatch.h"
#include "ui_modal.h"
#include "ui_theme.h"
#include "page_manager.h"
#include "i18n.h"
#include "ui_widgets.h"   /* UI_FONT_SYMBOL_SM（新模板移至 ui_widgets.h）*/

typedef struct {
    bambu_lan_config_t cfg;
    char page[40];
    char expected_serial[41];
    bool camera;
    bool wait_catalog;
} media_request_t;

static bool media_searching;
static bool cloud_media;
static char media_serial[41];

const char *ui_bambu_media_cloud_serial(void)
{
    return cloud_media ? media_serial : NULL;
}

static bool selected_matches(const char *serial)
{
    bambu_cloud_view_t cloud;
    bambu_cloud_device_t selected;
    bambu_cloud_selected_snapshot(&cloud, &selected);
    return cloud.signed_in && serial[0] && !strcasecmp(serial, selected.serial);
}

bool ui_bambu_copy_media_config(const char *serial, bambu_lan_config_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!serial) {
        if (bambu_service_copy_lan_config(out) != ESP_OK) bambu_profile_load(out);
        return out->serial[0] && out->printer_ip[0] && out->access_code[0];
    }

    if (!selected_matches(serial)) return false;
    if (bambu_cloud_copy_media(out) && !strcasecmp(serial, out->serial) &&
        out->printer_ip[0] && out->access_code[0]) return true;

    /* A saved access code may be used only for the same printer, with peer identity checked. */
    bambu_profile_load(out);
    if (!strcasecmp(serial, out->serial) && out->printer_ip[0] && out->access_code[0]) {
        out->verify_printer = true;
        return true;
    }

    return bambu_cloud_media_credentials(serial, out) && out->printer_ip[0];
}

static void enter_media(bool camera, const char *serial)
{
    cloud_media = serial != NULL;
    strlcpy(media_serial, serial ? serial : "", sizeof(media_serial));
    pm_switch_tab(camera ? 3 : 1);
}

static void media_search_done(void *arg)
{
    media_request_t *request = arg;
    media_searching = false;
    ui_loading_hide();
    const char *current = pm_current();
    if (current && !strcmp(request->page, current)) {
        const char *serial = request->expected_serial[0] ?
                             request->expected_serial : request->cfg.serial;
        if (selected_matches(serial)) {
            if (request->cfg.printer_ip[0]) bambu_cloud_set_media(&request->cfg);
            enter_media(request->camera, serial);
        } else if (!serial[0]) {
            ui_toast(tr("打印机局域网授权尚未就绪，请稍后再试"), UI_TOAST_INFO);
        }
    }
    memset(request, 0, sizeof(*request));
    free(request);
}

static void media_search_worker(void *arg)
{
    media_request_t *request = arg;
    if (request->wait_catalog) {
        /* On boot the account may already be loaded before Wi-Fi, time sync,
           and the bound-printer catalog have finished. */
        for (unsigned attempt = 0; attempt < 60; ++attempt) {
            bambu_cloud_view_t cloud;
            bambu_cloud_device_t selected;
            bambu_cloud_selected_snapshot(&cloud, &selected);
            if (!cloud.signed_in) break;
            if (selected.serial[0] && bambu_cloud_media_credentials(selected.serial, &request->cfg)) {
                if (request->cfg.printer_ip[0] || cloud.live) break;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    bambu_discovered_t found[BAMBU_DISCOVERY_MAX_DEVICES];
    size_t count = BAMBU_DISCOVERY_MAX_DEVICES;
    if (request->cfg.serial[0] && !request->cfg.printer_ip[0] &&
        bambu_discovery_scan(11000, found, &count) == ESP_OK) {
        for (size_t i = 0; i < count; ++i) {
            if (strcasecmp(request->cfg.serial, found[i].serial)) continue;
            strlcpy(request->cfg.printer_ip, found[i].ip, sizeof(request->cfg.printer_ip));
            if (request->cfg.model == BAMBU_MODEL_OTHER) request->cfg.model = found[i].model;
            break;
        }
    }
    if (!ui_post(media_search_done, request)) {
        memset(request, 0, sizeof(*request));
        free(request);
        media_searching = false;
    }
    vTaskDelete(NULL);
}

void ui_bambu_open_lan_media(bool camera, const char *serial)
{
    if (media_searching) return;
    if (!serial) {
        enter_media(camera, NULL);
        return;
    }

    bambu_lan_config_t cfg;
    if (ui_bambu_copy_media_config(serial, &cfg)) {
        memset(&cfg, 0, sizeof(cfg));
        enter_media(camera, serial);
        return;
    }

    media_request_t *request = calloc(1, sizeof(*request));
    if (!request) return;
    strlcpy(request->expected_serial, serial, sizeof(request->expected_serial));
    if (!bambu_cloud_media_credentials(serial, &request->cfg)) {
        bambu_cloud_view_t cloud;
        bambu_cloud_device_t selected;
        bambu_cloud_selected_snapshot(&cloud, &selected);
        if (cloud.signed_in && !selected.serial[0] && !serial[0]) {
            request->wait_catalog = true;
        } else {
            free(request);
            if (selected_matches(serial)) enter_media(camera, serial);
            else ui_toast(tr("打印机局域网授权尚未就绪，请稍后再试"), UI_TOAST_INFO);
            return;
        }
    }
    const char *current = pm_current();
    if (!current) {
        memset(request, 0, sizeof(*request));
        free(request);
        return;
    }
    strlcpy(request->page, current, sizeof(request->page));
    request->camera = camera;
    media_searching = true;
    ui_loading_show(false);
    if (xTaskCreate(media_search_worker, "bambu_media", 6144, request, 4, NULL) != pdPASS) {
        memset(request, 0, sizeof(*request));
        free(request);
        media_searching = false;
        ui_loading_hide();
    }
}

static void style_bambu_card(lv_obj_t *card)
{
    lv_obj_set_style_bg_color(card, UI_BAMBU_CARD, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, 0);
}

/** LVGL9 默认 obj 可点击，会抢 lv_btn 行内子控件的事件，导致“点不动” */
static void ui_clear_clickable_subtree(lv_obj_t *root)
{
    if (!root) {
        return;
    }
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
    uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; i++) {
        ui_clear_clickable_subtree(lv_obj_get_child(root, i));
    }
}

lv_obj_t *ui_bambu_chip(lv_obj_t *parent, const char *text)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_set_height(chip, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, UI_COLOR_CARD_HI, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(chip, 20, 0);
    lv_obj_set_style_pad_hor(chip, 10, 0);
    lv_obj_set_style_pad_ver(chip, 4, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lb = lv_label_create(chip);
    lv_label_set_text(lb, text);
    lv_obj_set_style_text_font(lb, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(lb, UI_COLOR_TEXT_SEC, 0);
    lv_obj_center(lb);
    return chip;
}

lv_obj_t *ui_bambu_temp_tile(lv_obj_t *parent, const char *title, lv_obj_t **val_out,
                             lv_color_t accent)
{
    lv_obj_t *card = ui_create_card(parent);
    style_bambu_card(card);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_width(card, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_style_min_height(card, 72, 0);
    ui_create_label(card, title, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    *val_out = ui_create_label(card, "--", UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_width(*val_out, LV_PCT(100));
    lv_label_set_long_mode(*val_out, LV_LABEL_LONG_WRAP);
    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_set_size(bar, LV_PCT(100), 3);
    lv_obj_set_style_bg_color(bar, accent, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *ui_bambu_cam_panel(lv_obj_t *parent, lv_obj_t **img_out, lv_obj_t **status_out,
                             lv_obj_t **title_out)
{
    lv_obj_t *panel = ui_create_card(parent);
    style_bambu_card(panel);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(panel, UI_PAD_SM, 0);
    lv_obj_set_style_clip_corner(panel, true, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hdr = ui_create_row(panel);
    lv_obj_t *t = ui_create_label(hdr, tr("实时相机"), UI_FONT_CAPTION, UI_COLOR_TEXT);
    *title_out = t;
    lv_obj_set_width(t, LV_PCT(56));
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    *status_out = ui_create_label(hdr, LV_SYMBOL_EYE_OPEN, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(*status_out, 1);
    lv_label_set_long_mode(*status_out, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(*status_out, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *viewport = lv_obj_create(panel);
    lv_obj_remove_style_all(viewport);
    lv_obj_remove_flag(viewport, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(viewport, LV_PCT(100));
    lv_obj_set_flex_grow(viewport, 1);
    lv_obj_set_style_bg_color(viewport, lv_color_hex(0x0D1117), 0);
    lv_obj_set_style_bg_opa(viewport, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(viewport, 10, 0);
    lv_obj_set_style_clip_corner(viewport, true, 0);

    *img_out = lv_image_create(viewport);
    lv_obj_set_size(*img_out, LV_PCT(100), LV_PCT(100));
    return panel;
}

lv_obj_t *ui_bambu_action_btn(lv_obj_t *parent, const char *icon, const char *label,
                              bool primary, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, LV_PCT(100));
    lv_obj_set_style_min_height(btn, ui_screen_is_landscape() ? 48 : 64, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, primary ? 0 : 1, 0);
    lv_obj_set_style_border_color(btn, UI_COLOR_BORDER, 0);
    lv_obj_set_style_bg_color(btn, primary ? UI_COLOR_PRIMARY : UI_BAMBU_CARD, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_shadow_color(btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(btn, 4, 0);
    lv_obj_set_style_pad_ver(btn, 4, 0);
    lv_obj_set_style_pad_row(btn, 3, 0);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    const bool land = ui_screen_is_landscape();
    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, land ? UI_FONT_SYMBOL_SM : UI_FONT_SYMBOL, 0);
    lv_obj_set_style_text_color(ic, primary ? lv_color_black() : UI_COLOR_PRIMARY, 0);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, label);
    lv_obj_set_style_text_font(lb, land ? UI_FONT_CAPTION : UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(lb, primary ? lv_color_black() : UI_COLOR_TEXT, 0);
    lv_obj_set_width(lb, LV_PCT(100));
    lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_DOT);
    return btn;
}

void ui_bambu_action_btn_set_active(lv_obj_t *btn, bool active)
{
    if (!btn) {
        return;
    }
    lv_obj_set_style_bg_color(btn, active ? UI_COLOR_PRIMARY : UI_BAMBU_CARD, 0);
    lv_obj_set_style_border_width(btn, active ? 0 : 1, 0);
    lv_obj_set_style_shadow_width(btn, active ? 10 : 0, 0);
    lv_obj_set_style_shadow_opa(btn, active ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    lv_obj_t *ic = lv_obj_get_child(btn, 0);
    lv_obj_t *lb = lv_obj_get_child(btn, 1);
    if (ic) {
        lv_obj_set_style_text_color(ic, active ? lv_color_black() : UI_COLOR_PRIMARY, 0);
    }
    if (lb) {
        lv_obj_set_style_text_color(lb, active ? lv_color_black() : UI_COLOR_TEXT, 0);
    }
}

lv_obj_t *ui_bambu_ams_column(lv_obj_t *parent, int slot_1based, ui_bambu_ams_col_t *out)
{
    memset(out, 0, sizeof(*out));
    lv_obj_t *card = ui_create_card(parent);
    out->card = card;
    style_bambu_card(card);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_width(card, LV_PCT(24));
    lv_obj_set_style_min_height(card, 0, 0);
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_set_style_pad_all(card, UI_PAD_SM, 0);
    lv_obj_set_style_pad_row(card, 4, 0);

    char num[8];
    snprintf(num, sizeof(num), "%d", slot_1based);
    ui_create_label(card, num, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

    out->spool = lv_obj_create(card);
    lv_obj_set_size(out->spool, 44, 44);
    lv_obj_set_style_radius(out->spool, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(out->spool, lv_color_hex(0x2A3544), 0);
    lv_obj_set_style_border_width(out->spool, 2, 0);
    lv_obj_set_style_border_color(out->spool, UI_COLOR_BORDER, 0);
    lv_obj_clear_flag(out->spool, LV_OBJ_FLAG_SCROLLABLE);

    out->lbl_status = ui_create_label(card, tr("空闲"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    out->lbl_mat = ui_create_label(card, "—", UI_FONT_BODY, UI_COLOR_TEXT);
    out->lbl_sub = ui_create_label(card, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_t *labels[] = {out->lbl_status, out->lbl_mat, out->lbl_sub};
    for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        lv_obj_set_width(labels[i], LV_PCT(100));
        lv_label_set_long_mode(labels[i], LV_LABEL_LONG_DOT);
    }

    out->bar = lv_bar_create(card);
    lv_obj_set_width(out->bar, LV_PCT(100));
    lv_obj_set_height(out->bar, 5);
    lv_bar_set_range(out->bar, 0, 100);
    lv_obj_set_style_bg_color(out->bar, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(out->bar, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    return card;
}

void ui_bambu_ams_column_select(ui_bambu_ams_col_t *col, bool selected)
{
    if (!col || !col->card) {
        return;
    }
    lv_obj_set_style_border_width(col->card, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(col->card, selected ? UI_COLOR_PRIMARY : UI_COLOR_BORDER, 0);
    lv_obj_set_style_shadow_width(col->card, selected ? 14 : 0, 0);
    lv_obj_set_style_shadow_color(col->card, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_opa(col->card, LV_OPA_TRANSP, 0);
}

void ui_bambu_ams_column_tint(ui_bambu_ams_col_t *col, lv_color_t spool, int remain_pct)
{
    if (!col) {
        return;
    }
    if (col->spool) {
        lv_obj_set_style_bg_color(col->spool, spool, 0);
    }
    if (col->bar) {
        if (remain_pct < 0) {
            remain_pct = 0;
        }
        if (remain_pct > 100) {
            remain_pct = 100;
        }
        lv_bar_set_value(col->bar, remain_pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(col->bar, spool, LV_PART_INDICATOR);
    }
}

lv_obj_t *ui_bambu_file_row_rich(lv_obj_t *parent, const char *name, const char *mat,
                                 const char *time_meta, bool selected)
{
    lv_obj_t *row = lv_btn_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, UI_BAMBU_CARD, 0);
    lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(row, selected ? UI_COLOR_PRIMARY : UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, UI_PAD_SM, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_PAD_SM, 0);
    lv_obj_set_style_shadow_width(row, selected ? 8 : 0, 0);
    lv_obj_set_style_shadow_color(row, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_opa(row, selected ? LV_OPA_30 : LV_OPA_TRANSP, 0);

    lv_obj_t *thumb = lv_obj_create(row);
    lv_obj_set_size(thumb, 52, 52);
    lv_obj_set_style_radius(thumb, 8, 0);
    lv_obj_set_style_bg_color(thumb, UI_COLOR_CARD_HI, 0);
    lv_obj_set_style_border_width(thumb, 0, 0);
    lv_obj_clear_flag(thumb, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);
    ui_create_label(col, name, UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_t *tags = ui_create_row(col);
    ui_bambu_chip(tags, mat);
    ui_create_label(col, time_meta, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

    if (selected) {
        lv_obj_t *ok = ui_create_label(row, LV_SYMBOL_OK, UI_FONT_SYMBOL, UI_COLOR_PRIMARY);
        (void)ok;
    }

    uint32_t nc = lv_obj_get_child_count(row);
    for (uint32_t i = 0; i < nc; i++) {
        ui_clear_clickable_subtree(lv_obj_get_child(row, i));
    }
    return row;
}

lv_obj_t *ui_bambu_storage_nav_btn(lv_obj_t *parent, const char *icon, const char *label,
                                   bool selected, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, ui_land_btn_h_sm() + (ui_screen_is_landscape() ? 6 : 4));
    lv_obj_set_style_bg_color(btn, UI_BAMBU_CARD, 0);
    lv_obj_set_style_border_width(btn, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(btn, selected ? UI_COLOR_PRIMARY : UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_shadow_color(btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, 8, 0);
    lv_obj_set_style_pad_hor(btn, UI_PAD_SM, 0);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, UI_FONT_SYMBOL, 0);
    lv_obj_set_style_text_color(ic, selected ? UI_COLOR_PRIMARY : UI_COLOR_TEXT_SEC, 0);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, label);
    lv_obj_set_style_text_font(lb, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(lb, UI_COLOR_TEXT, 0);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(lb, 1);
    return btn;
}

lv_obj_t *ui_bambu_task_bar_create(lv_obj_t *page_root, ui_bambu_task_bar_t *out)
{
    memset(out, 0, sizeof(*out));
    lv_obj_t *strip = lv_obj_create(page_root);
    out->root = strip;
    lv_obj_set_width(strip, LV_PCT(100));
    lv_obj_set_height(strip, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(strip, ui_screen_is_landscape() ? 56 : 64, 0);
    lv_obj_set_style_bg_color(strip, UI_BAMBU_CARD, 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(strip, 1, 0);
    lv_obj_set_style_border_color(strip, UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(strip, 12, 0);
    lv_obj_set_style_pad_all(strip, UI_PAD_SM, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(strip, 4, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = ui_create_row(strip);
    lv_obj_t *thumb = lv_obj_create(top);
    lv_obj_set_size(thumb, 36, 36);
    lv_obj_set_style_radius(thumb, 6, 0);
    lv_obj_set_style_bg_color(thumb, UI_COLOR_CARD_HI, 0);
    lv_obj_set_style_border_width(thumb, 0, 0);
    lv_obj_clear_flag(thumb, LV_OBJ_FLAG_SCROLLABLE);

    out->lbl_file = ui_create_label(top, "—", UI_FONT_CAPTION, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(out->lbl_file, 1);
    lv_label_set_long_mode(out->lbl_file, LV_LABEL_LONG_DOT);
    out->lbl_pct = ui_create_label(top, "0%", UI_FONT_BODY, UI_COLOR_PRIMARY);

    out->bar = lv_bar_create(strip);
    lv_obj_set_width(out->bar, LV_PCT(100));
    lv_obj_set_height(out->bar, 6);
    lv_bar_set_range(out->bar, 0, 100);
    lv_obj_set_style_bg_color(out->bar, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(out->bar, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(out->bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(out->bar, 4, LV_PART_INDICATOR);

    lv_obj_t *bot = ui_create_row(strip);
    out->lbl_layer = ui_create_label(bot, tr("层 — / —"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(out->lbl_layer, 1);
    out->lbl_time = ui_create_label(bot, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    return strip;
}

void ui_bambu_task_bar_update(ui_bambu_task_bar_t *s, const char *file, int pct,
                              const char *layer_line, const char *time_line)
{
    if (!s || !s->root) {
        return;
    }
    if (s->lbl_file && file) {
        lv_label_set_text(s->lbl_file, file);
    }
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    if (s->lbl_pct) {
        char b[8];
        snprintf(b, sizeof(b), "%d%%", pct);
        lv_label_set_text(s->lbl_pct, b);
    }
    if (s->bar) {
        lv_bar_set_value(s->bar, pct, LV_ANIM_OFF);
    }
    if (s->lbl_layer && layer_line && layer_line[0]) {
        lv_label_set_text(s->lbl_layer, layer_line);
    }
    if (s->lbl_time && time_line && time_line[0]) {
        lv_label_set_text(s->lbl_time, time_line);
    }
}

void ui_bambu_step_pills_create(lv_obj_t *parent, lv_obj_t *btns_out[3], lv_event_cb_t cb)
{
    ui_create_label(parent, tr("步进距离 (mm)"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_t *row = ui_create_row(parent);
    const char *labels[] = { "0.1", "1", "10" };
    for (int i = 0; i < 3; i++) {
        btns_out[i] = ui_create_btn(row, labels[i], LV_PCT(32), 32, UI_BTN_OUTLINE, cb,
                                    (void *)(intptr_t)i);
    }
    ui_bambu_step_pills_set_active(btns_out, 0);
}

void ui_bambu_step_pills_set_active(lv_obj_t *btns[3], int idx)
{
    for (int i = 0; i < 3; i++) {
        if (!btns[i]) {
            continue;
        }
        bool on = (i == idx);
        lv_obj_set_style_bg_color(btns[i], on ? UI_COLOR_PRIMARY : UI_COLOR_BG, 0);
        lv_obj_set_style_bg_opa(btns[i], on ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(btns[i], on ? UI_COLOR_PRIMARY : UI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(btns[i], on ? 2 : 1, 0);
    }
}
