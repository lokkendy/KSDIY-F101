#include "HomeFleet.h"

#include "i18n.h"
#include "ui_bambu.h"

#include <stdio.h>
#include <string.h>

typedef enum { FILTER_ALL, FILTER_RUNNING, FILTER_IDLE, FILTER_FAULT, FILTER_OFFLINE } fleet_filter_t;

static void set_label(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void set_visible(lv_obj_t *obj, bool visible)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == visible) {
        lv_obj_set_flag(obj, LV_OBJ_FLAG_HIDDEN, !visible);
    }
}

static bool is_offline(const bambu_cloud_fleet_device_t *printer)
{
    return !printer->live && printer->device.online_known && !printer->device.online;
}

static bool is_printing(bambu_print_state_t state)
{
    return state == BAMBU_PRINT_RUNNING || state == BAMBU_PRINT_PREPARE;
}

static bool matches(const bambu_cloud_fleet_device_t *printer, fleet_filter_t filter)
{
    if (filter == FILTER_ALL) return true;
    if (filter == FILTER_OFFLINE) return is_offline(printer);
    if (!printer->live) return false;
    if (filter == FILTER_RUNNING) return is_printing(printer->state);
    if (filter == FILTER_FAULT) {
        return printer->state == BAMBU_PRINT_FAILED || printer->state == BAMBU_PRINT_PAUSED;
    }
    return printer->state == BAMBU_PRINT_IDLE || printer->state == BAMBU_PRINT_FINISHED;
}

static const char *status_text(const bambu_cloud_fleet_device_t *printer, lv_color_t *color)
{
    *color = UI_COLOR_TEXT_SEC;
    if (printer->subscription_denied) return tr("未授权状态");
    if (is_offline(printer)) return tr("离线");
    if (!printer->live) return tr(printer->has_data ? "状态待更新" : "等待状态");

    switch (printer->state) {
    case BAMBU_PRINT_RUNNING: *color = UI_COLOR_SUCCESS; return tr("打印中");
    case BAMBU_PRINT_PREPARE: *color = UI_COLOR_SUCCESS; return tr("准备中");
    case BAMBU_PRINT_PAUSED: *color = UI_COLOR_WARNING; return tr("已暂停");
    case BAMBU_PRINT_FAILED: *color = UI_COLOR_DANGER; return tr("打印失败");
    case BAMBU_PRINT_FINISHED: return tr("已完成");
    case BAMBU_PRINT_IDLE: return tr("空闲");
    default: return tr("在线");
    }
}

static lv_obj_t *compact_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = ui_create_label(parent, "", font, color);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

static void create_card(HomeFleet_t *view, HomeFleetCard_t *card)
{
    const bool wide = ui_screen_is_landscape();
    const int columns = wide ? 3 : 2;
    const int content_width = ui_screen_w() - (wide ? 72 + 2 * UI_PAD_SM : 2 * UI_PAD);
    const int gap = 8;

    card->card = ui_create_card(view->grid);
    lv_obj_set_width(card->card, (content_width - gap * (columns - 1)) / columns);
    lv_obj_set_style_pad_all(card->card, 10, 0);
    lv_obj_set_style_pad_row(card->card, 4, 0);
    lv_obj_set_style_bg_color(card->card, UI_COLOR_CARD_HI, LV_STATE_PRESSED);
    lv_obj_add_event_cb(card->card, view->on_printer, LV_EVENT_CLICKED, card);
    card->name = compact_label(card->card, UI_FONT_BODY, UI_COLOR_TEXT);
    card->model = compact_label(card->card, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

    lv_obj_t *row = ui_create_row(card->card);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    card->state = compact_label(row, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_set_width(card->state, 0);
    lv_obj_set_flex_grow(card->state, 1);
    card->progress = ui_create_label(row, "", UI_FONT_NUM_SM, UI_COLOR_TEXT);

    card->bar = lv_bar_create(card->card);
    lv_obj_set_size(card->bar, LV_PCT(100), 4);
    lv_bar_set_range(card->bar, 0, 100);
    lv_obj_set_style_bg_color(card->bar, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card->bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_clear_flag(card->bar, LV_OBJ_FLAG_CLICKABLE);
    card->remaining = compact_label(card->card, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_set_height(card->remaining, lv_font_get_line_height(UI_FONT_CAPTION));
}

static void update_card(HomeFleetCard_t *card, const bambu_cloud_fleet_device_t *printer, bool selected)
{
    const bambu_cloud_device_t *device = &printer->device;
    set_label(card->name, device->name[0] ? device->name : device->serial);
    set_label(card->model, device->model[0] ? device->model : device->serial);
    lv_color_t color;
    const char *state = status_text(printer, &color);
    set_label(card->state, state);
    if (!lv_color_eq(lv_obj_get_style_text_color(card->state, 0), color)) {
        lv_obj_set_style_text_color(card->state, color, 0);
    }

    char text[48];
    int progress = printer->progress_percent;
    if (progress < 0) snprintf(text, sizeof(text), "--");
    else snprintf(text, sizeof(text), "%d%%", progress > 100 ? 100 : progress);
    set_label(card->progress, text);
    int fill = progress < 0 ? 0 : progress > 100 ? 100 : progress;
    if (lv_bar_get_value(card->bar) != fill) lv_bar_set_value(card->bar, fill, LV_ANIM_OFF);

    if (printer->remaining_minutes >= 0 &&
        (is_printing(printer->state) || printer->state == BAMBU_PRINT_PAUSED))
        snprintf(text, sizeof(text), tr("剩余 %d 分钟"), printer->remaining_minutes);
    else
        text[0] = 0;
    set_label(card->remaining, text);

    color = selected ? UI_COLOR_ACCENT : UI_COLOR_BORDER;
    if (!lv_color_eq(lv_obj_get_style_border_color(card->card, 0), color)) {
        lv_obj_set_style_border_color(card->card, color, 0);
    }
}

void HomeFleet_SelectFilter(HomeFleet_t *view, unsigned filter)
{
    if (filter <= FILTER_OFFLINE) view->active_filter = filter;
}

void HomeFleet_Create(HomeFleet_t *view, lv_obj_t *parent, lv_event_cb_t on_menu,
                      lv_event_cb_t on_printer, lv_event_cb_t on_filter)
{
    view->on_printer = on_printer;
    view->root = ui_create_page_root(parent);
    view->content = ui_create_content(view->root);
    lv_obj_t *heading = ui_create_row(view->content);
    lv_obj_set_height(heading, 54);
    lv_obj_t *title = ui_create_label_bind(heading, "多台总览", UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(title, 1);
    ui_create_btn_bind(heading, "单台详情", 110, 42, UI_BTN_SUBTLE, on_menu, (void *)"single_view");

    view->summary = compact_label(view->content, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    view->filters = ui_create_row(view->content);
    lv_obj_set_style_pad_column(view->filters, 6, 0);
    static const char *const keys[] = {"全部", "打印中", "空闲", "异常", "离线"};
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        lv_obj_t *button = ui_create_btn_bind(view->filters, keys[i], 1, 40, UI_BTN_SUBTLE,
                                             on_filter, (void *)(uintptr_t)i);
        view->filter_buttons[i] = button;
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_pad_all(button, 3, 0);
        lv_obj_t *label = lv_obj_get_child(button, 0);
        lv_obj_set_style_text_font(label, UI_FONT_CAPTION, 0);
    }

    view->hint = compact_label(view->content, UI_FONT_CAPTION, UI_COLOR_WARNING);
    view->empty = compact_label(view->content, UI_FONT_BODY, UI_COLOR_TEXT_SEC);
    view->login = ui_create_btn_bind(view->content, "拓竹云监控", LV_PCT(100), 44, UI_BTN_FILLED,
                                     on_menu, (void *)"bambu_cloud");
    view->grid = lv_obj_create(view->content);
    lv_obj_remove_style_all(view->grid);
    lv_obj_set_size(view->grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(view->grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(view->grid, 8, 0);
    lv_obj_clear_flag(view->grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

void HomeFleet_Update(HomeFleet_t *view, const bambu_cloud_fleet_t *snapshot, const char *notice)
{
    unsigned filtered = 0, printing = 0;
    fleet_filter_t filter = view->active_filter;
    for (unsigned i = 0; i <= FILTER_OFFLINE; ++i) {
        lv_obj_t *button = view->filter_buttons[i];
        const bool active = i == filter;
        lv_color_t surface = active ? UI_COLOR_ACCENT : UI_COLOR_CARD_HI;
        if (!lv_color_eq(lv_obj_get_style_bg_color(button, 0), surface)) {
            lv_obj_set_style_bg_color(button, surface, 0);
        }
        lv_obj_t *label = lv_obj_get_child(button, 0);
        lv_color_t ink = active ? lv_color_hex(0x102018) : UI_COLOR_TEXT;
        if (!lv_color_eq(lv_obj_get_style_text_color(label, 0), ink)) {
            lv_obj_set_style_text_color(label, ink, 0);
        }
    }

    for (unsigned i = 0; i < snapshot->count; ++i) {
        const bambu_cloud_fleet_device_t *printer = &snapshot->devices[i];
        if (printer->live && is_printing(printer->state)) ++printing;
        if (!matches(printer, filter)) continue;
        HomeFleetCard_t *card = &view->cards[filtered];
        if (filtered == view->card_count) {
            create_card(view, card);
            ++view->card_count;
        }
        card->index = i;
        update_card(card, printer, i == snapshot->selected);
        set_visible(card->card, true);
        ++filtered;
    }
    for (unsigned i = filtered; i < view->card_count; ++i) set_visible(view->cards[i].card, false);

    char text[64];
    snprintf(text, sizeof(text), tr("%u 台设备 / %u 台打印中"), snapshot->count, printing);
    set_label(view->summary, text);
    set_label(view->hint, notice ? tr(notice) : "");
    set_visible(view->hint, notice != NULL);
    set_visible(view->filters, snapshot->count > 1);
    set_visible(view->grid, filtered > 0);
    set_visible(view->empty, !filtered);
    set_visible(view->login, !snapshot->count);
    if (!filtered) {
        const char *message = !snapshot->signed_in ? "登录中国区账号查看打印机" :
                              !snapshot->count ? "等待设备列表，可在云监控刷新" : "当前分类暂无设备";
        set_label(view->empty, tr(message));
    }
}
