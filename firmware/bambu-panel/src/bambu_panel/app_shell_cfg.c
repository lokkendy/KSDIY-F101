#include "app_shell_cfg.h"

#include "i18n.h"
#include "ui_common.h"
#include "data_bridge.h"
#include "ksdiy_status_icons.h"

#include "Pages/Files/Files.h"
#include "Pages/Filament/Filament.h"
#include "Pages/History/History.h"
#include "Pages/Home/Home.h"
#include "Pages/Home/HomeModel.h"
#include "Pages/Job/Job.h"
#include "Pages/Settings/Settings.h"

#include <string.h>

static lv_obj_t *sb_printer_create(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_pad_column(row, 5, 0);
    const ksdiy_status_colors_t colors = {
        UI_COLOR_TEXT, UI_COLOR_TEXT_SEC, UI_COLOR_BG_BAR,
        UI_COLOR_SUCCESS, UI_COLOR_ACCENT, UI_COLOR_DANGER,
    };
    ksdiy_status_printer_create(row, colors);
    lv_obj_t *name = lv_label_create(row);
    const int screen_w = lv_display_get_horizontal_resolution(NULL);
    lv_obj_set_style_max_width(name, screen_w - 386, 0);
    lv_obj_set_height(name, lv_font_get_line_height(UI_FONT_BODY));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(name, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(name, UI_COLOR_TEXT, 0);

    lv_obj_t *status = lv_label_create(row);
    lv_obj_set_style_text_font(status, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(status, UI_COLOR_TEXT_SEC, 0);
    return row;
}

static void sb_printer_update(lv_obj_t *item, const app_bridge_t *b, void *user_data)
{
    (void)b;
    (void)user_data;
    HomeModel_t device;
    HomeModel_GetSnapshot(&device);
    lv_obj_t *name = lv_obj_get_child(item, 1);
    lv_obj_t *status = lv_obj_get_child(item, 2);
    if (strcmp(lv_label_get_text(name), device.printer_name))
        lv_label_set_text(name, device.printer_name);
    if (strcmp(lv_label_get_text(status), device.online_line))
        lv_label_set_text(status, device.online_line);
    ksdiy_status_link_set(lv_obj_get_child(item, 0),
                          device.connected ? KSDIY_LINK_CONNECTED : KSDIY_LINK_OFF);
    lv_color_t color = device.connected ? UI_COLOR_SUCCESS : UI_COLOR_TEXT_SEC;
    if (!lv_color_eq(lv_obj_get_style_text_color(status, 0), color))
        lv_obj_set_style_text_color(status, color, 0);
}

static const sb_item_t s_sb_left[] = {
    {
        .type = SB_CUSTOM,
        .create_cb = sb_printer_create,
        .update_cb = sb_printer_update,
    },
};

static const sb_item_t s_sb_right[] = {
    { .type = SB_WIFI },
    { .type = SB_BATT },
    { .type = SB_CLOCK },
};

static const statusbar_cfg_t s_app_statusbar = {
    .left = s_sb_left,
    .n_left = sizeof(s_sb_left) / sizeof(s_sb_left[0]),
    .right = s_sb_right,
    .n_right = sizeof(s_sb_right) / sizeof(s_sb_right[0]),
    .period_ms = 1000,
};

/* 与设计稿一致：5 Tab；耗材/控制从首页进入（二级页） */
static const pm_tab_def_t s_app_tabs[] = {
    { "设备",     LV_SYMBOL_HOME,     &page_home },
    { "设备存储", LV_SYMBOL_FILE,     &page_files },
    { "打印记录", LV_SYMBOL_LIST,     &page_history },
    { "相机",     LV_SYMBOL_VIDEO,    &page_job },
    { "设置",     LV_SYMBOL_SETTINGS, &page_settings },
    { "耗材",     LV_SYMBOL_DRIVE,    &page_filament },
};

const pm_shell_cfg_t g_app_shell_cfg = {
    .tabs = s_app_tabs,
    .n_tabs = sizeof(s_app_tabs) / sizeof(s_app_tabs[0]),
    .root_page = NULL,
    .statusbar = &s_app_statusbar,
};
