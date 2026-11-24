#include "ui_bambu_command.h"
#include "Home.h"
#include "BambuCloud.h"
#include "HomeModel.h"
#include "HomeFleet.h"
#include "HomeView.h"
#include "HAL_WiFi.h"

#include "../About/About.h"
#include "../Control/Control.h"
#include "../Filament/Filament.h"
#include "../Print/Print.h"
#include "../WifiScan/WifiScan.h"

#include "bambu_cloud.h"
#include "bambu_job_cover.h"
#include "bambu_preview.h"
#include "bambu_profile.h"
#include "ui_bambu.h"
#include "bambu_service.h"
#include "i18n.h"
#include "ui_screen.h"

#include "bambu_photo.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef struct {
    lv_obj_t *root;
    HomeView_t view;
    HomeFleet_t fleet;
    char requested_serial[sizeof(((bambu_cloud_device_t *)0)->serial)];
    uint32_t selection_started;
    int32_t fleet_scroll;
    bool fleet_visible;
    bool select_failed;
    lv_timer_t *timer;
    bambu_job_cover_t *job_cover;
    lv_image_dsc_t photo_dsc;     /* 机型照片（data 指向 flash 常量数组，免释放） */
    HomeModel_t shown;
    bool shown_valid;
    bool cover_active;
    bambu_model_t photo_model;    /* 当前已显示照片的机型（COUNT=尚未显示） */
} home_presenter_t;

static home_presenter_t s_home;
static bambu_cloud_fleet_t fleet_snapshot;
static bool fleet_mode, mode_chosen, single_cloud;

static void on_menu(lv_event_t *e);
static void on_select_printer(lv_event_t *e);
static void on_fleet_filter(lv_event_t *e);
static void refresh_mode(void);

/* 机型照片：数据是内置 PNG 常量，仅机型变化时换 src（1s 定时器防重解码） */
static void home_apply_photo(const HomeView_t *v, bambu_model_t model)
{
    const uint8_t *png = NULL;
    size_t len = 0;
    if (!bambu_photo_for_model(model, &png, &len) || len < 32) {
        lv_obj_add_flag(v->img_photo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(v->lbl_photo_ph, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* PNG IHDR 宽高（big-endian @16..23）；压缩源由 LodePNG 解码，不能标为原始像素 */
    uint32_t w = ((uint32_t)png[16] << 24) | ((uint32_t)png[17] << 16) |
                 ((uint32_t)png[18] << 8) | (uint32_t)png[19];
    uint32_t h = ((uint32_t)png[20] << 24) | ((uint32_t)png[21] << 16) |
                 ((uint32_t)png[22] << 8) | (uint32_t)png[23];
    if (w == 0 || h == 0 || w > 4096 || h > 4096) {
        lv_obj_add_flag(v->img_photo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(v->lbl_photo_ph, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_image_dsc_t *d = &s_home.photo_dsc;
    lv_image_cache_drop(d);
    lv_image_header_cache_drop(d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    d->header.w = (uint16_t)w;
    d->header.h = (uint16_t)h;
    d->header.stride = 0;
    d->data_size = (uint32_t)len;
    d->data = png;
    lv_image_set_src(v->img_photo, d);

    /* 等比缩进照片盒内边距内（盒为横条/竖条不定，取宽高较小侧） */
    lv_obj_update_layout(v->img_printer);
    int32_t bw = lv_obj_get_width(v->img_printer);
    int32_t bh = lv_obj_get_height(v->img_printer);
    enum { k_photo_pad = 6 };
    int32_t inner_w = bw - 2 * k_photo_pad;
    int32_t inner_h = bh - 2 * k_photo_pad;
    if (inner_w < 8) {
        inner_w = 8;
    }
    if (inner_h < 8) {
        inner_h = 8;
    }
    int32_t inner = inner_w < inner_h ? inner_w : inner_h;
    uint32_t max_wh = w > h ? w : h;
    lv_image_set_scale(v->img_photo, (uint16_t)(inner * 256 / max_wh));
    printf("[HOME] photo model=%d png=%ux%u len=%u box=%ldx%ld\n", (int)model, (unsigned)w,
           (unsigned)h, (unsigned)len, (long)bw, (long)bh);
    lv_image_set_antialias(v->img_photo, true);
    lv_obj_center(v->img_photo);
    lv_obj_clear_flag(v->img_photo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(v->lbl_photo_ph, LV_OBJ_FLAG_HIDDEN);
}

static void home_set_label(lv_obj_t *label, const char *text, const char *shown)
{
    if (label && (!shown || strcmp(text, shown) != 0)) {
        lv_label_set_text(label, text);
    }
}

static void home_set_visible(lv_obj_t *obj, bool visible)
{
    if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != visible) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool home_camera_config(const HomeModel_t *model, bambu_lan_config_t *config)
{
    if (!model->serial[0] || !HAL_WiFi_IsConnected()) return false;
    if (!ui_bambu_copy_media_config(model->cloud ? model->serial : NULL, config)) return false;
    return !strcasecmp(model->serial, config->serial);
}

void home_open_media(bool camera)
{
    if (fleet_mode) {
        bambu_cloud_view_t cloud;
        bambu_cloud_device_t selected;
        bambu_cloud_selected_snapshot(&cloud, &selected);
        ui_bambu_open_lan_media(camera, cloud.signed_in ? selected.serial : NULL);
    } else {
        HomeModel_t model;
        HomeModel_GetSnapshot(&model);
        ui_bambu_open_lan_media(camera, model.cloud ? model.serial : NULL);
    }
}

static void home_update_camera(void)
{
    HomeView_t *v = &s_home.view;
    if (!v->img_camera) return;

    bambu_lan_config_t config = {0};
    char message[96];
    bool visible = false;
    if (home_camera_config(&s_home.shown, &config)) {
        bambu_cam_state_t state;
        bambu_preview_open(v->img_camera, &config);
        visible = bambu_preview_update(v->img_camera, &state, message, sizeof(message));
        if (visible) {
            strlcpy(message, "实时画面", sizeof(message));
        }
    } else {
        bambu_preview_close(v->img_camera);
        strlcpy(message, s_home.shown.serial[0] ? "等待局域网相机" : "连接打印机",
                sizeof(message));
    }
    const char *label = tr(message);
    home_set_label(v->lbl_camera, label, lv_label_get_text(v->lbl_camera));
    home_set_visible(v->img_printer, !visible);
}

static void home_refresh(void)
{
    HomeModel_t model;
    HomeModel_GetSnapshot(&model);
    const HomeModel_t *shown = s_home.shown_valid ? &s_home.shown : NULL;
    HomeView_t *v = &s_home.view;
    if (v->card_device) {
        if (!shown || shown->card_level != model.card_level) {
            lv_color_t border = UI_COLOR_ACCENT;
            if (model.card_level == HOME_CARD_DISCONNECTED || model.card_level == HOME_CARD_WARN) {
                border = UI_COLOR_WARNING;
            } else if (model.card_level == HOME_CARD_ERROR) {
                border = UI_COLOR_DANGER;
            }
            lv_obj_set_style_border_color(v->card_device, border, 0);
        }
        if (v->status_dot && (!shown || shown->connected != model.connected)) {
            lv_color_t color = model.connected ? UI_COLOR_SUCCESS : UI_COLOR_TEXT_SEC;
            lv_obj_set_style_bg_color(v->status_dot, color, 0);
            lv_obj_set_style_text_color(v->lbl_online, color, 0);
        }

        home_set_label(v->lbl_printer_name, model.printer_name, shown ? shown->printer_name : NULL);
        home_set_label(v->lbl_online, model.online_line, shown ? shown->online_line : NULL);
        home_set_label(v->lbl_idle, model.idle_sub, shown ? shown->idle_sub : NULL);
        home_set_label(v->lbl_cloud, model.cloud_line, shown ? shown->cloud_line : NULL);

        home_set_label(v->lbl_job_file, model.file_text, shown ? shown->file_text : NULL);
        home_set_label(v->lbl_job_pct, model.progress_text, shown ? shown->progress_text : NULL);
        if (v->bar_job && (!shown || shown->progress_pct != model.progress_pct)) {
            lv_bar_set_value(v->bar_job, model.progress_pct, LV_ANIM_OFF);
        }
        home_set_label(v->lbl_job_remain, model.remain_text, shown ? shown->remain_text : NULL);
        home_set_label(v->lbl_job_layer, model.layer_text, shown ? shown->layer_text : NULL);
        home_set_visible(v->lbl_job_layer, model.layer_text[0] != '\0');
        home_set_label(v->lbl_nozzle_val, model.nozzle_val, shown ? shown->nozzle_val : NULL);
        home_set_label(v->lbl_nozzle_tgt, model.nozzle_tgt, shown ? shown->nozzle_tgt : NULL);
        home_set_label(v->lbl_bed_val, model.bed_val, shown ? shown->bed_val : NULL);
        home_set_label(v->lbl_bed_tgt, model.bed_tgt, shown ? shown->bed_tgt : NULL);

        home_set_visible(v->tile_chamber, model.show_chamber);
        if (model.show_chamber) {
            const bool had_chamber = shown && shown->show_chamber;
            home_set_label(v->lbl_chamber_val, model.chamber_val,
                           had_chamber ? shown->chamber_val : NULL);
            home_set_label(v->lbl_chamber_tgt, model.chamber_tgt,
                           had_chamber ? shown->chamber_tgt : NULL);
        }
    }
    home_set_label(v->lbl_identity, model.identity_text, shown ? shown->identity_text : NULL);

    if (v->img_photo && v->lbl_photo_ph && model.model != s_home.photo_model) {
        s_home.photo_model = model.model;
        home_apply_photo(v, model.model);
    }

    if (v->btn_cancel_job) {
        const bool show_controls = model.has_job_detail && !model.cloud;
        home_set_visible(v->footer, show_controls);
        home_set_visible(v->btn_cancel_job, show_controls);
    }

    const bool cover_active = model.job_active && !model.cloud &&
                              model.file_text[0] && strcmp(model.file_text, "—") != 0;
    if (s_home.job_cover && v->img_job_cover && (cover_active || s_home.cover_active || !shown)) {
        bambu_job_cover_maybe_fetch(s_home.job_cover, cover_active, model.file_text,
                                    model.gcode_file, model.plate_idx);
    }
    s_home.cover_active = cover_active;
    s_home.shown = model;
    s_home.shown_valid = true;
}

static void show_mode(bool show_fleet)
{
    fleet_mode = show_fleet;
    if (!s_home.root || s_home.fleet_visible == show_fleet) return;
    s_home.fleet_visible = show_fleet;
    if (show_fleet) {
        bambu_preview_close(s_home.view.img_camera);
        if (s_home.cover_active) {
            bambu_job_cover_maybe_fetch(s_home.job_cover, false, "", "", 0);
            s_home.cover_active = false;
        }
        if (!s_home.fleet.root) {
            HomeFleet_Create(&s_home.fleet, s_home.root, on_menu,
                             on_select_printer, on_fleet_filter);
        }
        home_set_visible(s_home.view.root, false);
        home_set_visible(s_home.fleet.root, true);
        HomeFleet_Update(&s_home.fleet, &fleet_snapshot, NULL);
        lv_obj_update_layout(s_home.fleet.content);
        lv_obj_scroll_to_y(s_home.fleet.content, s_home.fleet_scroll, LV_ANIM_OFF);
    } else {
        s_home.fleet_scroll = lv_obj_get_scroll_y(s_home.fleet.content);
        home_set_visible(s_home.fleet.root, false);
        home_set_visible(s_home.view.root, true);
        s_home.shown_valid = false;
    }
}

static void open_single(bool cloud)
{
    mode_chosen = true;
    s_home.select_failed = false;
    s_home.requested_serial[0] = 0;
    single_cloud = cloud;
    HomeModel_SetCloudPreferred(cloud);
    show_mode(false);
    home_refresh();
    home_update_camera();
}

void home_open_fleet(void)
{
    mode_chosen = true;
    fleet_mode = true;
    if (pm_page_is_top(&page_home) && s_home.root) refresh_mode();
    else pm_switch_tab(0);
}

static void on_select_printer(lv_event_t *event)
{
    if (s_home.requested_serial[0]) return;
    const HomeFleetCard_t *card = lv_event_get_user_data(event);
    if (card->index >= fleet_snapshot.count) return;
    char serial[sizeof(s_home.requested_serial)];
    strlcpy(serial, fleet_snapshot.devices[card->index].device.serial, sizeof(serial));
    bambu_cloud_fleet_snapshot(&fleet_snapshot);
    unsigned index = 0;
    while (index < fleet_snapshot.count &&
           strcmp(serial, fleet_snapshot.devices[index].device.serial)) ++index;
    if (index == fleet_snapshot.count || !fleet_snapshot.signed_in) {
        refresh_mode();
        return;
    }
    if (index == fleet_snapshot.selected) {
        open_single(true);
        return;
    }

    s_home.select_failed = !bambu_cloud_select(index, fleet_snapshot.catalog_revision);
    if (!s_home.select_failed) {
        strlcpy(s_home.requested_serial, fleet_snapshot.devices[index].device.serial,
                sizeof(s_home.requested_serial));
        s_home.selection_started = lv_tick_get();
    }
    refresh_mode();
}

static void on_fleet_filter(lv_event_t *event)
{
    HomeFleet_SelectFilter(&s_home.fleet, (unsigned)(uintptr_t)lv_event_get_user_data(event));
    refresh_mode();
    lv_obj_scroll_to_y(s_home.fleet.content, 0, LV_ANIM_OFF);
}

static void refresh_mode(void)
{
    if (!mode_chosen || fleet_mode) bambu_cloud_fleet_snapshot(&fleet_snapshot);
    if (!mode_chosen && fleet_snapshot.signed_in && fleet_snapshot.count) {
        mode_chosen = true;
        fleet_mode = fleet_snapshot.count > 1;
        single_cloud = fleet_mode;
    }
    show_mode(fleet_mode);
    if (!s_home.fleet_visible) {
        home_refresh();
        home_update_camera();
        return;
    }

    if (s_home.requested_serial[0]) {
        if (fleet_snapshot.selected < fleet_snapshot.count &&
            !strcmp(s_home.requested_serial,
                    fleet_snapshot.devices[fleet_snapshot.selected].device.serial)) {
            open_single(true);
            return;
        }
        if (lv_tick_elaps(s_home.selection_started) >= 5000 || !fleet_snapshot.signed_in) {
            s_home.requested_serial[0] = 0;
            s_home.select_failed = true;
        }
    }
    const char *notice = s_home.requested_serial[0] ? "正在切换打印机" :
                         s_home.select_failed ? "切换未完成，请重试" : NULL;
    HomeFleet_Update(&s_home.fleet, &fleet_snapshot, notice);
}

void home_ui_refresh(void)
{
    if (s_home.fleet_visible || !s_home.view.root || !lv_obj_is_valid(s_home.view.root)) {
        return;
    }
    home_refresh();
}

static void on_status_click(lv_event_t *e)
{
    (void)e;
    if (s_home.shown.cloud) pm_push(&page_bambu_cloud);
    else home_open_media(true);
}

static void on_menu(lv_event_t *e)
{
    const char *target = (const char *)lv_event_get_user_data(e);
    if (!target) {
        return;
    }
    if (strcmp(target, "fleet_view") == 0) {
        home_open_fleet();
        return;
    }
    if (strcmp(target, "single_view") == 0) {
        open_single(single_cloud);
        return;
    }
    if (s_home.shown.cloud && (!strcmp(target, "tab_control") || !strcmp(target, "tab_filament") ||
                               !strcmp(target, "device") || !strcmp(target, "filament"))) {
        ui_toast(tr("公网只读"), UI_TOAST_INFO);
        return;
    }
    if (strcmp(target, "printer_connection") == 0) {
        pm_push(s_home.shown.cloud ? &page_bambu_cloud : &page_print);
    } else if (strcmp(target, "tab_camera") == 0 || strcmp(target, "tab_files") == 0) {
        ui_bambu_open_lan_media(strcmp(target, "tab_camera") == 0,
                                s_home.shown.cloud ? s_home.shown.serial : NULL);
    } else if (strcmp(target, "tab_filament") == 0) {
        pm_push(&page_filament);
    } else if (strcmp(target, "tab_control") == 0) {
        pm_push(&page_control);
    } else if (strcmp(target, "tab_settings") == 0) {
        pm_switch_tab(4);
    } else if (strcmp(target, "job") == 0) {
        home_open_media(true);
    } else if (strcmp(target, "bambu_cloud") == 0) {
        pm_push(&page_bambu_cloud);
    } else if (strcmp(target, "print") == 0) {
        pm_push(&page_print);
    } else if (strcmp(target, "device") == 0) {
        pm_push(&page_control);
    } else if (strcmp(target, "filament") == 0) {
        pm_push(&page_filament);
    } else if (strcmp(target, "wifi") == 0) {
        pm_push(&page_wifi_scan);
    } else if (strcmp(target, "settings") == 0) {
        pm_switch_tab(4);
    } else if (strcmp(target, "about") == 0) {
        pm_push(&page_about);
    }
}

static void on_cancel_job(lv_event_t *e)
{
    (void)e;
    if (!s_home.shown.cloud) ui_bambu_command_result(bambu_service_stop_print(),"停止打印");
    home_refresh();
}

static void home_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_mode();
}

static lv_obj_t *home_create(lv_obj_t *parent, void *user_data)
{
    (void)user_data;
    memset(&s_home, 0, sizeof(s_home));
    s_home.photo_model = BAMBU_MODEL_COUNT;   /* 强制首帧应用照片 */
    s_home.root = ui_create_page_root(parent);
    HomeView_Create(&s_home.view, s_home.root, on_status_click, on_menu, on_cancel_job);
    s_home.job_cover = bambu_job_cover_create(s_home.view.img_job_cover,
                                              s_home.view.lbl_job_cover_ph);
    s_home.timer = lv_timer_create(home_update_timer_cb, 500, NULL);
    if (s_home.timer) lv_timer_pause(s_home.timer);
    if (!mode_chosen || fleet_mode) bambu_cloud_fleet_snapshot(&fleet_snapshot);
    if (fleet_mode) show_mode(true);
    else home_refresh();
    return s_home.root;
}

static void home_destroy(void *user_data)
{
    (void)user_data;
    if (s_home.timer) {
        lv_timer_delete(s_home.timer);
        s_home.timer = NULL;
    }
    bambu_preview_close(s_home.view.img_camera);
    lv_image_cache_drop(&s_home.photo_dsc);
    lv_image_header_cache_drop(&s_home.photo_dsc);
    bambu_job_cover_destroy(s_home.job_cover);
    memset(&s_home, 0, sizeof(s_home));
}

static void home_on_pause(void *user_data)
{
    (void)user_data;
    if (s_home.timer) {
        lv_timer_pause(s_home.timer);
    }
    bambu_preview_close(s_home.view.img_camera);
}

static void home_on_resume(void *user_data)
{
    (void)user_data;
    if (s_home.timer) {
        lv_timer_resume(s_home.timer);
    }
    refresh_mode();
}

static void home_on_language(void *user_data)
{
    (void)user_data;
    s_home.shown_valid = false;
    if (s_home.fleet_visible) {
        HomeFleet_Update(&s_home.fleet, &fleet_snapshot, NULL);
    } else {
        home_refresh();
    }
}

const page_t page_home = {
    .name = "首页",   /* 与 Tab 标签一致（selftest/日志按此名断言） */
    .create = home_create,
    .destroy = home_destroy,
    .on_pause = home_on_pause,
    .on_resume = home_on_resume,
    .on_language = home_on_language,
    .cache = PM_CACHE_KEEP_ALIVE,
    .user_data = NULL,
};
