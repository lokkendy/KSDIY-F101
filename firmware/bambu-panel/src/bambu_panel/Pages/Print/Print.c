#include "ui_bambu_command.h"
#include "ui_dispatch.h"
#include "Print.h"
#include "PrintView.h"

#include "bambu_discovery.h"
#include "bambu_model.h"
#include "bambu_profile.h"
#include "bambu_service.h"
#include "i18n.h"
#include "ui_keyboard.h"
#include "ui_modal.h"
#include "ui_screen.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    PrintView_t view;
    lv_timer_t *timer;
    bool scan_busy;
} print_presenter_t;

typedef struct {
    bambu_discovered_t *list;
    size_t count;
} disc_result_t;

typedef struct {
    disc_result_t *result;
    uint32_t gen;
} disc_ui_done_t;

typedef struct {
    disc_result_t *result;
    print_presenter_t *pres;
    lv_obj_t *modal_card;
} disc_modal_ctx_t;

static print_presenter_t s_print;
static uint32_t s_disc_gen;

static void apply_discovered(const bambu_discovered_t *d)
{
    if (!d) {
        return;
    }
    lv_textarea_set_text(s_print.view.ta_ip, d->ip);
    lv_textarea_set_text(s_print.view.ta_serial, d->serial);
    if (d->name[0]) {
        lv_textarea_set_text(s_print.view.ta_name, d->name);
    }
    if (d->model != BAMBU_MODEL_UNKNOWN && d->model != BAMBU_MODEL_OTHER) {
        lv_dropdown_set_selected(s_print.view.dd_model, bambu_model_to_ui_index(d->model));
        lv_label_set_text(s_print.view.lbl_lan_hint, bambu_model_lan_hint(d->model));
    }
}

static void read_form(bambu_lan_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    const char *ip = lv_textarea_get_text(s_print.view.ta_ip);
    const char *sn = lv_textarea_get_text(s_print.view.ta_serial);
    const char *code = lv_textarea_get_text(s_print.view.ta_code);
    const char *name = lv_textarea_get_text(s_print.view.ta_name);
    if (ip) {
        strncpy(cfg->printer_ip, ip, sizeof(cfg->printer_ip) - 1);
    }
    if (sn) {
        strncpy(cfg->serial, sn, sizeof(cfg->serial) - 1);
    }
    if (code) {
        strncpy(cfg->access_code, code, sizeof(cfg->access_code) - 1);
    }
    if (name) {
        strncpy(cfg->nickname, name, sizeof(cfg->nickname) - 1);
    }
    uint16_t sel = lv_dropdown_get_selected(s_print.view.dd_model);
    cfg->model = bambu_model_from_ui_index(sel);
}

static void on_model_changed(lv_event_t *e)
{
    PrintView_t *v = lv_event_get_user_data(e);
    uint16_t sel = lv_dropdown_get_selected(v->dd_model);
    bambu_model_t m = bambu_model_from_ui_index(sel);
    lv_label_set_text(v->lbl_lan_hint, bambu_model_lan_hint(m));
}

static void refresh_status(void)
{
    bambu_svc_snapshot_t snap;
    bambu_service_snapshot(&snap);
    lv_label_set_text(s_print.view.lbl_status, snap.message);
}

static void on_save(lv_event_t *e)
{
    (void)e;
    bambu_lan_config_t cfg;
    read_form(&cfg);
    if (!cfg.printer_ip[0] || !cfg.serial[0] || !cfg.access_code[0]) {
        ui_toast(tr("请填写 IP、SN 与访问码"), UI_TOAST_ERROR);
        return;
    }
    esp_err_t err=bambu_service_apply_lan(&cfg);
    if(err!=ESP_OK){ui_toast(tr("请检查 IP、SN 与 8 位访问码"),UI_TOAST_ERROR);return;}
    if(bambu_profile_save(&cfg)!=ESP_OK){ui_toast(tr("正在连接，但配置保存失败，重启后需重新配置"),UI_TOAST_ERROR);return;}
    ui_toast(tr("正在连接打印机…"), UI_TOAST_INFO);
    refresh_status();
}

static void on_pause(lv_event_t *e)
{ (void)e;ui_bambu_command_result(bambu_service_pause(),"暂停打印"); }

static void on_resume(lv_event_t *e)
{ (void)e;ui_bambu_command_result(bambu_service_resume(),"继续打印"); }

static void on_stop(lv_event_t *e)
{ (void)e;ui_bambu_command_result(bambu_service_stop_print(),"停止打印"); }

static void on_speed(lv_event_t *e)
{ ui_bambu_command_result(bambu_service_set_speed_level((int)(intptr_t)lv_event_get_user_data(e)),"打印速度"); }

static void disc_modal_close(disc_modal_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->modal_card) {
        ui_modal_close(ctx->modal_card);
        ctx->modal_card = NULL;
    }
}

static void on_disc_pick(lv_event_t *e)
{
    disc_modal_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    const bambu_discovered_t *d = (const bambu_discovered_t *)lv_obj_get_user_data(btn);
    apply_discovered(d);
    ui_toast(tr("已填入打印机信息，请填写 LAN 访问码后连接"), UI_TOAST_INFO);
    disc_modal_close(ctx);
}

static void disc_modal_on_close(lv_obj_t *card, void *user_data)
{
    (void)card;
    disc_modal_ctx_t *ctx = user_data;
    if (!ctx) {
        return;
    }
    if (ctx->result) {
        free(ctx->result->list);
        free(ctx->result);
    }
    free(ctx);
    s_print.scan_busy = false;
}

static void disc_modal_build(lv_obj_t *card, void *user_data)
{
    disc_modal_ctx_t *ctx = user_data;
    ctx->modal_card = card;
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);

    ui_create_label(card, tr("选择打印机"), UI_FONT_TITLE, UI_COLOR_TEXT);
    ui_create_label(card, tr("发现仅用于填写 IP/SN，仍需 LAN 访问码才能连接"),
                    UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

    lv_obj_t *list = lv_list_create(card);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(list, ui_screen_is_landscape() ? 220 : 320, 0);

    disc_result_t *res = ctx->result;
    for (size_t i = 0; i < res->count; i++) {
        const bambu_discovered_t *d = &res->list[i];
        char line[192];
        const char *title = d->name[0] ? d->name : d->serial;
        snprintf(line, sizeof(line), "%s\n%s · %s", title, d->ip, d->serial);
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_WIFI, line);
        lv_obj_set_style_text_font(btn,UI_FONT_BODY,0);
        lv_obj_set_user_data(btn, (void *)&res->list[i]);
        lv_obj_add_event_cb(btn, on_disc_pick, LV_EVENT_CLICKED, ctx);
    }
}

static void show_disc_modal(disc_result_t *res)
{
    disc_modal_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        free(res->list);
        free(res);
        s_print.scan_busy = false;
        return;
    }
    ctx->result = res;
    ctx->pres = &s_print;

    ui_modal_builder_t b = {
        .build = disc_modal_build,
        .on_close = disc_modal_on_close,
        .user_data = ctx,
        .width = 0,
        .dim = 0,
        .close_on_outside = true,
    };
    ui_modal_open(&b);
}

static void discovery_ui_done(void *arg)
{
    disc_ui_done_t *pack = arg;
    if (!pack || pack->gen != s_disc_gen) {
        if (pack && pack->result) {
            free(pack->result->list);
            free(pack->result);
        }
        free(pack);
        return;
    }

    ui_loading_hide();
    s_print.scan_busy = false;
    disc_result_t *res = pack->result;
    free(pack);

    if (!res || res->count == 0) {
        ui_toast(tr("未发现打印机（需 WiFi 已连接、打印机 LAN 模式、同网段）"), UI_TOAST_INFO);
        if (res) {
            free(res->list);
            free(res);
        }
        return;
    }
    show_disc_modal(res);
}

static void discovery_worker(void *arg)
{
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    disc_ui_done_t *pack = calloc(1, sizeof(*pack));
    disc_result_t *res = calloc(1, sizeof(*res));
    if (!pack || !res) {
        free(pack);
        free(res);
        disc_ui_done_t *fail = calloc(1, sizeof(*fail));
        if (fail) {
            fail->gen = gen;
            if (!ui_post(discovery_ui_done, fail)) free(fail);
        }
        vTaskDelete(NULL);
        return;
    }
    res->list = calloc(BAMBU_DISCOVERY_MAX_DEVICES, sizeof(bambu_discovered_t));
    if (!res->list) {
        free(res);
        pack->gen = gen;
        if (!ui_post(discovery_ui_done, pack)) {
            if (pack->result) { free(pack->result->list); free(pack->result); }
            free(pack);
        }
        vTaskDelete(NULL);
        return;
    }
    size_t n = BAMBU_DISCOVERY_MAX_DEVICES;
    esp_err_t err = bambu_discovery_scan(8000, res->list, &n);
    if (err != ESP_OK) {
        n = 0;
    }
    res->count = n;
    pack->result = res;
    pack->gen = gen;
    if (!ui_post(discovery_ui_done, pack)) {
            if (pack->result) { free(pack->result->list); free(pack->result); }
            free(pack);
        }
    vTaskDelete(NULL);
}

static void on_discover(lv_event_t *e)
{
    (void)e;
    if (s_print.scan_busy) {
        return;
    }
    s_disc_gen++;
    s_print.scan_busy = true;
    ui_loading_show(true);
    if (xTaskCreate(discovery_worker, "bambu_disc", 6144, (void *)(uintptr_t)s_disc_gen, 5,
                    NULL) != pdPASS) {
        ui_loading_hide();
        s_print.scan_busy = false;
        ui_toast(tr("扫描任务启动失败"), UI_TOAST_ERROR);
    }
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_status();
}

static lv_obj_t *print_create(lv_obj_t *parent, void *user_data)
{
    (void)user_data;
    memset(&s_print, 0, sizeof(s_print));
    bambu_lan_config_t saved;
    bambu_profile_load(&saved);
    PrintView_Create(&s_print.view, parent, &saved, on_model_changed, on_discover, on_save,
                     on_pause, on_resume, on_stop, on_speed);
    s_print.timer = lv_timer_create(timer_cb, 1000, NULL);
    refresh_status();
    return s_print.view.root;
}

static void print_destroy(void *user_data)
{
    (void)user_data;
    ++s_disc_gen;
    s_print.scan_busy = false;
    if (s_print.timer) {
        lv_timer_delete(s_print.timer);
        s_print.timer = NULL;
    }
    ui_keyboard_hide();
}

static void print_on_pause(void *user_data)
{
    (void)user_data;
    if (s_print.timer) {
        lv_timer_pause(s_print.timer);
    }
    if (s_print.scan_busy) {
        s_disc_gen++;
        s_print.scan_busy = false;
        ui_loading_hide();
    }
    ui_keyboard_hide();
}

static void print_on_resume(void *user_data)
{
    (void)user_data;
    if (s_print.timer) {
        lv_timer_resume(s_print.timer);
    }
    refresh_status();
}

const page_t page_print = {
    .name = "打印",   /* 打印仪表二级页（与「控制」机芯操作页区分） */
    .create = print_create,
    .destroy = print_destroy,
    .on_pause = print_on_pause,
    .on_resume = print_on_resume,
    .cache = PM_CACHE_KEEP_ALIVE,
    .user_data = NULL,
};
