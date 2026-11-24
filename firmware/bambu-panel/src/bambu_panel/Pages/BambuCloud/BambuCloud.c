#include "BambuCloud.h"
#include "../Home/Home.h"
#include "bambu_cloud.h"
#include "ui_bambu.h"
#include "ui_keyboard.h"
#include "ui_screen.h"
#include <stdio.h>
#include <string.h>

static struct {
    lv_obj_t *message, *hint, *form, *monitor, *account, *code, *send, *login;
    lv_obj_t *printer, *printer_info, *job, *metrics, *refresh, *sign_out, *media;
    lv_timer_t *timer;
    unsigned catalog_revision;
    char feedback[112];
} ui;
static bambu_cloud_view_t snapshot;
static bambu_cloud_device_t printers[BAMBU_CLOUD_DEVICE_MAX];

static void label_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void visible(lv_obj_t *obj, bool show)
{
    if (show) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void enabled(lv_obj_t *obj, bool enable)
{
    if (enable) lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

static void on_send(lv_event_t *event)
{
    (void)event;
    ui_keyboard_hide();
    if (bambu_cloud_send_code(lv_textarea_get_text(ui.account))) ui.feedback[0] = 0;
    else snprintf(ui.feedback, sizeof(ui.feedback), "请输入有效邮箱或中国大陆手机号");
}

static void on_login(lv_event_t *event)
{
    (void)event;
    ui_keyboard_hide();
    if (bambu_cloud_verify(lv_textarea_get_text(ui.account), lv_textarea_get_text(ui.code))) {
        ui.feedback[0] = 0;
        lv_textarea_set_text(ui.code, "");
    } else snprintf(ui.feedback, sizeof(ui.feedback), "请填写账号和收到的验证码");
}

static void on_fleet(lv_event_t *event)
{
    (void)event;
    home_open_fleet();
}

static void on_refresh(lv_event_t *event)
{
    (void)event;
    ui.feedback[0] = 0;
    bambu_cloud_refresh();
}

static void on_logout(lv_event_t *event)
{
    (void)event;
    ui.feedback[0] = 0;
    bambu_cloud_sign_out();
}

static void on_select(lv_event_t *event)
{
    (void)event;
    ui.feedback[0] = 0;
    bambu_cloud_select(lv_dropdown_get_selected(ui.printer), ui.catalog_revision);
}

static void on_media(lv_event_t *event)
{
    bambu_cloud_device_t selected;
    bambu_cloud_selected_snapshot(&snapshot, &selected);
    if (!snapshot.signed_in || !selected.serial[0]) return;
    bool camera = lv_event_get_user_data(event) != NULL;
    ui_bambu_open_lan_media(camera, selected.serial);
}

static const char *job_state(bambu_print_state_t state)
{
    switch (state) {
    case BAMBU_PRINT_IDLE: return "空闲";
    case BAMBU_PRINT_RUNNING: return "打印中";
    case BAMBU_PRINT_PAUSED: return "已暂停";
    case BAMBU_PRINT_PREPARE: return "准备中";
    case BAMBU_PRINT_FINISHED: return "已完成";
    case BAMBU_PRINT_FAILED: return "打印失败";
    default: return "等待状态";
    }
}

static void metric(char *out, size_t cap, bool known, int value, const char *unit)
{
    if (known) snprintf(out, cap, "%d%s", value, unit);
    else snprintf(out, cap, "—");
}

static int temperature(float value)
{
    return value > -100 && value < 2000 ? (int)(value + (value >= 0 ? 0.5f : -0.5f)) : 0;
}

static void update(lv_timer_t *timer)
{
    (void)timer;
    bambu_cloud_snapshot(&snapshot, printers);
    label_text(ui.message, snapshot.message);
    label_text(ui.hint, ui.feedback);
    visible(ui.hint, ui.feedback[0] != 0);
    visible(ui.form, !snapshot.signed_in);
    visible(ui.monitor, snapshot.signed_in);
    visible(ui.media, snapshot.count != 0);
    enabled(ui.send, !snapshot.busy && !snapshot.cooldown_seconds);
    enabled(ui.login, !snapshot.busy);
    enabled(ui.refresh, !snapshot.busy);
    enabled(ui.sign_out, !snapshot.busy);
    enabled(ui.printer, !snapshot.busy && snapshot.count > 1);
    char text[384];
    if (snapshot.cooldown_seconds)
        snprintf(text, sizeof(text), "%u 秒后可重发", snapshot.cooldown_seconds);
    else snprintf(text, sizeof(text), "获取验证码");
    label_text(lv_obj_get_child(ui.send, 0), text);
    if (!snapshot.signed_in) return;

    if (snapshot.catalog_revision != ui.catalog_revision) {
        char options[BAMBU_CLOUD_DEVICE_MAX * 82 + 1] = "";
        size_t pos = 0;
        for (unsigned i = 0; i < snapshot.count; ++i) {
            const char *name = printers[i].name[0] ? printers[i].name : printers[i].serial;
            int n = snprintf(options + pos, sizeof(options) - pos, "%s%s", i ? "\n" : "", name);
            if (n < 0 || (size_t)n >= sizeof(options) - pos) break;
            pos += (size_t)n;
        }
        lv_dropdown_set_options(ui.printer, pos ? options : "暂无打印机");
        ui.catalog_revision = snapshot.catalog_revision;
    }
    if (snapshot.count && !snapshot.busy && !lv_dropdown_is_open(ui.printer) &&
        lv_dropdown_get_selected(ui.printer) != snapshot.selected)
        lv_dropdown_set_selected(ui.printer, snapshot.selected);

    const bambu_cloud_device_t *d = snapshot.count ? &printers[snapshot.selected] : NULL;
    if (d) {
        const char *online = snapshot.live ? "在线" :
                             !d->online_known ? "在线状态未知" :
                             d->online ? "列表显示在线" : "列表显示离线";
        snprintf(text, sizeof(text), "%s  %s", d->model, online);
        label_text(ui.printer_info, text);
        const char *state = snapshot.metrics & BAMBU_CLOUD_STATE ? job_state(snapshot.status.state) :
                            d->state[0] ? d->state : "等待状态";
        snprintf(text, sizeof(text), "%s%s%s", state, snapshot.status.task_name[0] ? " · " : "",
                 snapshot.status.task_name);
        label_text(ui.job, text);
    } else {
        label_text(ui.printer_info, " ");
        label_text(ui.job, " ");
    }
    char progress[20], remaining[20], nozzle[20], bed[20], chamber[20];
    uint16_t seen = snapshot.metrics;
    metric(progress, sizeof(progress), seen & BAMBU_CLOUD_PROGRESS, snapshot.status.progress_percent, "%");
    metric(remaining, sizeof(remaining), seen & BAMBU_CLOUD_REMAINING, snapshot.status.remaining_minutes, " 分钟");
    metric(nozzle, sizeof(nozzle), seen & BAMBU_CLOUD_NOZZLE, temperature(snapshot.status.nozzle_temp), " °C");
    metric(bed, sizeof(bed), seen & BAMBU_CLOUD_BED, temperature(snapshot.status.bed_temp), " °C");
    metric(chamber, sizeof(chamber), seen & BAMBU_CLOUD_CHAMBER, temperature(snapshot.status.chamber_temp), " °C");
    snprintf(text, sizeof(text), "进度 %s    剩余 %s\n喷嘴 %s    热床 %s    腔体 %s",
             progress, remaining, nozzle, bed, chamber);
    label_text(ui.metrics, text);
}

static lv_obj_t *field(lv_obj_t *parent, const char *placeholder, unsigned max_len)
{
    lv_obj_t *input = lv_textarea_create(parent);
    lv_obj_set_width(input, LV_PCT(100));
    lv_obj_set_height(input, ui_land_input_h());
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, max_len);
    lv_textarea_set_placeholder_text(input, placeholder);
    ui_style_textarea_field(input);
    ui_keyboard_bind(input);
    return input;
}

static lv_obj_t *create(lv_obj_t *parent, void *arg)
{
    (void)arg;
    memset(&ui, 0, sizeof(ui));
    ui.catalog_revision = UINT32_MAX;
    lv_obj_t *root = ui_create_page_root(parent);
    lv_obj_t *content = ui_create_content(root);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_page_header(content, "拓竹云监控", "中国区 · 公网只读", pm_back_cb);
    lv_obj_t *state = ui_create_card(content);
    ui.message = ui_create_label(state, "", UI_FONT_BODY, UI_COLOR_TEXT_SEC);
    lv_obj_set_width(ui.message, LV_PCT(100));
    ui.hint = ui_create_label(state, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_set_width(ui.hint, LV_PCT(100));
    ui.form = ui_create_card(content);
    ui.account = field(ui.form, "邮箱或中国大陆手机号", BAMBU_CLOUD_ACCOUNT_MAX - 1);
    ui.code = field(ui.form, "验证码", BAMBU_CLOUD_CODE_MAX - 1);
    lv_textarea_set_accepted_chars(ui.code, "0123456789");
    lv_textarea_set_password_mode(ui.code, true);
    lv_textarea_set_password_show_time(ui.code, 0);
    lv_obj_t *actions = ui_create_row(ui.form);
    ui.send = ui_create_btn(actions, "获取验证码", LV_PCT(48), ui_land_btn_h(), UI_BTN_OUTLINE, on_send, NULL);
    ui.login = ui_create_btn(actions, "登录并保存", LV_PCT(48), ui_land_btn_h(), UI_BTN_FILLED, on_login, NULL);
    ui.monitor = ui_create_card(content);
    ui.printer = lv_dropdown_create(ui.monitor);
    lv_obj_set_width(ui.printer, LV_PCT(100));
    ui_style_dropdown(ui.printer);
    lv_obj_add_event_cb(ui.printer, on_select, LV_EVENT_VALUE_CHANGED, NULL);
    ui.printer_info = ui_create_label(ui.monitor, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    ui.job = ui_create_label(ui.monitor, "", UI_FONT_BODY, UI_COLOR_TEXT);
    ui.metrics = ui_create_label(ui.monitor, "", UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_width(ui.job, LV_PCT(100));
    lv_obj_set_width(ui.metrics, LV_PCT(100));
    ui_create_btn_bind(ui.monitor, "多台总览", LV_PCT(100), ui_land_btn_h(),
                       UI_BTN_FILLED, on_fleet, NULL);
    actions = ui_create_row(ui.monitor);
    ui.refresh = ui_create_btn(actions, "刷新设备", LV_PCT(48), ui_land_btn_h(), UI_BTN_OUTLINE, on_refresh, NULL);
    ui.sign_out = ui_create_btn(actions, "退出账号", LV_PCT(48), ui_land_btn_h(), UI_BTN_OUTLINE, on_logout, NULL);
    ui.media = ui_create_row(ui.monitor);
    ui_create_btn(ui.media, "局域网相机", LV_PCT(48), ui_land_btn_h(), UI_BTN_OUTLINE, on_media, (void *)1);
    ui_create_btn(ui.media, "设备存储", LV_PCT(48), ui_land_btn_h(), UI_BTN_OUTLINE, on_media, NULL);
    ui.timer = lv_timer_create(update, 500, NULL);
    update(NULL);
    return root;
}

static void destroy(void *arg)
{
    (void)arg;
    ui_keyboard_hide();
    if (ui.timer) lv_timer_delete(ui.timer);
    memset(&ui, 0, sizeof(ui));
}

const page_t page_bambu_cloud = {
    .name = "拓竹云监控",
    .create = create,
    .destroy = destroy,
};
