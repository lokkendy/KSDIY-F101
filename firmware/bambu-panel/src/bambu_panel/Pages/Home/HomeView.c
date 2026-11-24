#include "HomeView.h"

#include "i18n.h"
#include "ui_bambu.h"
#include "ui_screen.h"

static lv_obj_t *shortcut_btn(lv_obj_t *parent, const char *icon, const char *zh_key,
                              lv_event_cb_t cb, const void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_width(btn, 0);
    lv_obj_set_height(btn, LV_PCT(100));
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_border_color(btn, UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 4, 0);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)user_data);
    }
    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, UI_FONT_SYMBOL, 0);
    lv_obj_set_style_text_color(ic, UI_COLOR_ACCENT, 0);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, tr(zh_key));
    tr_bind(lb, zh_key);
    lv_obj_set_style_text_font(lb, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(lb, UI_COLOR_TEXT, 0);
    return btn;
}

static void home_job_preview_init(HomeView_t *v)
{
    v->lbl_job_cover_ph = ui_create_label(v->img_job_preview, LV_SYMBOL_IMAGE, UI_FONT_SYMBOL,
                                          UI_COLOR_TEXT_SEC);
    lv_obj_center(v->lbl_job_cover_ph);
    v->img_job_cover = lv_image_create(v->img_job_preview);
    lv_obj_add_flag(v->img_job_cover, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(v->img_job_cover);
}

static lv_obj_t *portrait_column(lv_obj_t *parent, int32_t width, int32_t gap)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, width, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, gap, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return col;
}

static lv_obj_t *portrait_temperature(lv_obj_t *row, const char *name, lv_obj_t **value,
                                       lv_obj_t **target, lv_event_cb_t on_menu)
{
    lv_obj_t *tile = ui_create_card(row);
    lv_obj_set_width(tile, 0);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_style_pad_all(tile, 8, 0);
    lv_obj_set_style_pad_row(tile, 2, 0);
    ui_create_label_bind(tile, name, UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    *value = ui_create_label(tile, "—", UI_FONT_NUM_SM, UI_COLOR_TEXT);
    *target = ui_create_label(tile, "—", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_add_event_cb(tile, on_menu, LV_EVENT_CLICKED, (void *)"tab_control");
    return tile;
}

static void portrait_camera(HomeView_t *v, lv_obj_t *content, lv_event_cb_t on_menu)
{
    lv_obj_t *stage = lv_obj_create(content);
    lv_obj_remove_style_all(stage);
    lv_obj_set_size(stage, LV_PCT(100), (ui_screen_w() - 2 * UI_PAD) * 9 / 16);
    lv_obj_set_style_bg_color(stage, lv_color_hex(0x101c27), 0);
    lv_obj_set_style_bg_opa(stage, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(stage, 14, 0);
    lv_obj_set_style_clip_corner(stage, true, 0);
    lv_obj_clear_flag(stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(stage, on_menu, LV_EVENT_CLICKED, (void *)"tab_camera");

    v->img_printer = lv_obj_create(stage);
    lv_obj_remove_style_all(v->img_printer);
    lv_obj_set_size(v->img_printer, 152, 164);
    lv_obj_align(v->img_printer, LV_ALIGN_CENTER, 0, -5);
    lv_obj_clear_flag(v->img_printer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    v->lbl_photo_ph = ui_create_label(v->img_printer, LV_SYMBOL_VIDEO, UI_FONT_SYMBOL,
                                      lv_color_hex(0x91a9bd));
    lv_obj_center(v->lbl_photo_ph);
    v->img_photo = lv_image_create(v->img_printer);
    lv_obj_clear_flag(v->img_photo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(v->img_photo);
    lv_obj_add_flag(v->img_photo, LV_OBJ_FLAG_HIDDEN);

    v->img_camera = lv_image_create(stage);
    lv_obj_set_size(v->img_camera, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(v->img_camera, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_clear_flag(v->img_camera, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(v->img_camera, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(v->img_camera);

    v->lbl_camera = ui_create_label(stage, tr("局域网相机"), UI_FONT_CAPTION,
                                    lv_color_hex(0xe6f0f7));
    lv_obj_set_style_bg_color(v->lbl_camera, lv_color_hex(0x10202c), 0);
    lv_obj_set_style_bg_opa(v->lbl_camera, LV_OPA_90, 0);
    lv_obj_set_style_pad_hor(v->lbl_camera, 9, 0);
    lv_obj_set_style_pad_ver(v->lbl_camera, 5, 0);
    lv_obj_set_style_radius(v->lbl_camera, 7, 0);
    lv_obj_set_style_max_width(v->lbl_camera, ui_screen_w() - 2 * UI_PAD - 140, 0);
    lv_label_set_long_mode(v->lbl_camera, LV_LABEL_LONG_DOT);
    lv_obj_align(v->lbl_camera, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    lv_obj_t *open = ui_create_btn_bind(stage, "查看相机", 112, 44, UI_BTN_FILLED,
                                        on_menu, (void *)"tab_camera");
    lv_obj_set_style_text_font(lv_obj_get_child(open, 0), UI_FONT_CAPTION, 0);
    lv_obj_align(open, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

static void home_build_portrait(HomeView_t *v, lv_obj_t *content, lv_event_cb_t on_status_click,
                                lv_event_cb_t on_menu, lv_event_cb_t on_cancel_job)
{
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);

    v->card_device = ui_create_row(content);
    lv_obj_set_height(v->card_device, 56);
    lv_obj_t *identity = portrait_column(v->card_device, 0, 1);
    lv_obj_set_flex_grow(identity, 1);
    v->lbl_printer_name = ui_create_label(identity, "—", UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_obj_set_width(v->lbl_printer_name, LV_PCT(100));
    lv_label_set_long_mode(v->lbl_printer_name, LV_LABEL_LONG_DOT);
    lv_obj_t *states = ui_create_row(identity);
    lv_obj_set_style_pad_column(states, 6, 0);
    v->status_dot = ui_create_dot(states, UI_COLOR_TEXT_SEC);
    v->lbl_online = ui_create_label(states, tr("离线"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    v->lbl_identity = ui_create_label(states, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(v->lbl_identity, 1);
    lv_label_set_long_mode(v->lbl_identity, LV_LABEL_LONG_DOT);
    v->btn_cloud = ui_create_btn(v->card_device, "", 116, 44, UI_BTN_SUBTLE, on_menu,
                                 (void *)"printer_connection");
    v->lbl_cloud = lv_obj_get_child(v->btn_cloud, 0);
    lv_obj_set_style_text_font(v->lbl_cloud, UI_FONT_CAPTION, 0);
    lv_obj_set_width(v->lbl_cloud, LV_PCT(100));
    lv_label_set_long_mode(v->lbl_cloud, LV_LABEL_LONG_DOT);
    lv_obj_t *overview = ui_create_btn_bind(v->card_device, "多台总览", 91, 44,
                                            UI_BTN_OUTLINE, on_menu, (void *)"fleet_view");
    lv_obj_set_style_text_font(lv_obj_get_child(overview, 0), UI_FONT_CAPTION, 0);

    portrait_camera(v, content, on_menu);

    v->card_job = ui_create_card(content);
    lv_obj_set_style_pad_row(v->card_job, 6, 0);
    lv_obj_t *header = ui_create_row(v->card_job);
    lv_obj_t *title = ui_create_label_bind(header, "当前打印", UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(title, 1);
    v->lbl_idle = ui_create_label(header, "—", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_set_style_max_width(v->lbl_idle, 180, 0);
    lv_label_set_long_mode(v->lbl_idle, LV_LABEL_LONG_DOT);
    v->btn_cancel_job = ui_create_btn_bind(header, "停止任务", 94, 38, UI_BTN_OUTLINE,
                                           on_cancel_job, NULL);
    lv_obj_set_style_border_color(v->btn_cancel_job, UI_COLOR_DANGER, 0);
    lv_obj_t *stop_label = lv_obj_get_child(v->btn_cancel_job, 0);
    lv_obj_set_style_text_color(stop_label, UI_COLOR_DANGER, 0);
    lv_obj_set_style_text_font(stop_label, UI_FONT_CAPTION, 0);
    lv_obj_add_flag(v->btn_cancel_job, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *job = ui_create_row(v->card_job);
    v->img_job_preview = lv_obj_create(job);
    lv_obj_set_size(v->img_job_preview, 72, 72);
    lv_obj_set_style_bg_color(v->img_job_preview, UI_COLOR_CARD_HI, 0);
    lv_obj_set_style_radius(v->img_job_preview, 9, 0);
    lv_obj_set_style_border_width(v->img_job_preview, 0, 0);
    lv_obj_clear_flag(v->img_job_preview, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    home_job_preview_init(v);

    lv_obj_t *details = portrait_column(job, 0, 4);
    lv_obj_set_flex_grow(details, 1);
    v->lbl_job_file = ui_create_label(details, "—", UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_set_width(v->lbl_job_file, LV_PCT(100));
    lv_label_set_long_mode(v->lbl_job_file, LV_LABEL_LONG_DOT);
    lv_obj_t *progress = ui_create_row(details);
    v->lbl_job_pct = ui_create_label(progress, "—", UI_FONT_NUM_SM, UI_COLOR_PRIMARY);
    v->lbl_job_remain = ui_create_label(progress, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(v->lbl_job_remain, 1);
    lv_label_set_long_mode(v->lbl_job_remain, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(v->lbl_job_remain, LV_TEXT_ALIGN_RIGHT, 0);

    v->bar_job = lv_bar_create(details);
    lv_obj_set_size(v->bar_job, LV_PCT(100), 6);
    lv_bar_set_range(v->bar_job, 0, 100);
    lv_obj_set_style_bg_color(v->bar_job, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(v->bar_job, UI_COLOR_PRIMARY, LV_PART_INDICATOR);

    v->lbl_job_layer = ui_create_label(details, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    lv_obj_add_flag(v->lbl_job_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(v->card_job, on_status_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(job, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *temperatures = ui_create_row(content);
    portrait_temperature(temperatures, "喷头", &v->lbl_nozzle_val, &v->lbl_nozzle_tgt, on_menu);
    portrait_temperature(temperatures, "热床", &v->lbl_bed_val, &v->lbl_bed_tgt, on_menu);
    v->tile_chamber = portrait_temperature(temperatures, "腔体", &v->lbl_chamber_val,
                                            &v->lbl_chamber_tgt, on_menu);
    lv_obj_add_flag(v->tile_chamber, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *shortcuts = ui_create_row(content);
    lv_obj_set_height(shortcuts, 60);
    static const struct {
        const char *icon, *label, *tag;
    } actions[] = {
        {LV_SYMBOL_BARS, "控制", "tab_control"},
        {LV_SYMBOL_DRIVE, "耗材", "filament"},
        {LV_SYMBOL_FILE, "设备存储", "tab_files"},
        {LV_SYMBOL_WIFI, "云监控", "bambu_cloud"},
    };
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        lv_obj_t *btn = shortcut_btn(shortcuts, actions[i].icon, actions[i].label, on_menu,
                                     actions[i].tag);
        lv_obj_set_style_pad_all(btn, 3, 0);
        lv_obj_t *label = lv_obj_get_child(btn, 1);
        lv_obj_set_style_text_font(label, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    }
}

/* The same live models and callbacks feed a compact Handy-style workspace. */
static lv_obj_t *home_temperature(lv_obj_t *row,const char *name,lv_obj_t **value,
                                  lv_obj_t **target,lv_event_cb_t cb)
{
    lv_obj_t *card=ui_create_card(row);
    lv_obj_set_width(card,0);lv_obj_set_flex_grow(card,1);
    lv_obj_set_height(card,82);lv_obj_set_style_border_width(card,0,0);
    ui_create_label(card,name,UI_FONT_CAPTION,UI_COLOR_TEXT_SEC);
    lv_obj_t *values=ui_create_row(card);
    *value=ui_create_label(values,"—",UI_FONT_NUM_SM,UI_COLOR_TEXT);
    *target=ui_create_label(values,"",UI_FONT_CAPTION,UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(*target, 1);
    lv_label_set_long_mode(*target, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card,cb,LV_EVENT_CLICKED,(void*)"tab_control");
    return card;
}
static void home_build_landscape(HomeView_t *v, lv_obj_t *content, lv_event_cb_t on_status_click,
                                lv_event_cb_t on_menu)
{
    lv_obj_t *heading=ui_create_row(content);
    lv_obj_t *title=ui_create_label(heading,tr("设备"),UI_FONT_TITLE,UI_COLOR_TEXT);
    lv_obj_set_flex_grow(title,1);
    ui_create_btn_bind(heading, "多台总览", LV_SIZE_CONTENT, 34, UI_BTN_OUTLINE,
                       on_menu, (void *)"fleet_view");
    ui_create_btn(heading,"拓竹云监控",LV_SIZE_CONTENT,34,UI_BTN_OUTLINE,on_menu,(void*)"bambu_cloud");
    ui_create_btn(heading,LV_SYMBOL_PLUS " 添加打印机",LV_SIZE_CONTENT,34,UI_BTN_SUBTLE,on_menu,(void*)"print");
    lv_obj_t *row=ui_create_row(content);
    lv_obj_set_flex_grow(row,1);lv_obj_set_style_min_height(row,0,0);
    lv_obj_set_style_pad_column(row,12,0);lv_obj_set_flex_align(row,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START);

    v->card_device=ui_create_card(row);
    lv_obj_set_width(v->card_device,LV_PCT(29));lv_obj_set_height(v->card_device,LV_PCT(100));
    lv_obj_set_style_pad_all(v->card_device,12,0);
    lv_obj_t *names=ui_create_row(v->card_device);
    v->lbl_printer_name=ui_create_label(names,"—",UI_FONT_BODY,UI_COLOR_TEXT);
    lv_obj_set_flex_grow(v->lbl_printer_name,1);lv_label_set_long_mode(v->lbl_printer_name,LV_LABEL_LONG_DOT);
    lv_obj_t *states=ui_create_row(v->card_device);
    v->status_dot=ui_create_dot(states,UI_COLOR_TEXT_SEC);
    v->lbl_online=ui_create_label(states,tr("离线"),UI_FONT_CAPTION,UI_COLOR_PRIMARY);
    v->lbl_idle=ui_create_label(states,"",UI_FONT_CAPTION,UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(v->lbl_idle,1);lv_label_set_long_mode(v->lbl_idle,LV_LABEL_LONG_DOT);
    v->img_printer=lv_obj_create(v->card_device);lv_obj_remove_style_all(v->img_printer);
    lv_obj_set_size(v->img_printer,LV_PCT(100),0);lv_obj_set_flex_grow(v->img_printer,1);
    lv_obj_set_style_min_height(v->img_printer,100,0);lv_obj_clear_flag(v->img_printer,LV_OBJ_FLAG_SCROLLABLE);
    v->lbl_photo_ph=ui_create_label(v->img_printer,LV_SYMBOL_HOME,UI_FONT_SYMBOL,UI_COLOR_TEXT_SEC);lv_obj_center(v->lbl_photo_ph);
    v->img_photo=lv_image_create(v->img_printer);lv_obj_center(v->img_photo);lv_obj_add_flag(v->img_photo,LV_OBJ_FLAG_HIDDEN);
    v->lbl_identity=ui_create_label(v->card_device,"",UI_FONT_CAPTION,UI_COLOR_TEXT_SEC);
    lv_obj_set_width(v->lbl_identity,LV_PCT(100));lv_label_set_long_mode(v->lbl_identity,LV_LABEL_LONG_DOT);
    v->btn_cloud=ui_create_btn(v->card_device,"",LV_PCT(100),38,UI_BTN_SUBTLE,on_menu,(void*)"printer_connection");
    v->lbl_cloud=lv_obj_get_child(v->btn_cloud,0);lv_obj_set_style_text_font(v->lbl_cloud,UI_FONT_CAPTION,0);
    lv_obj_set_width(v->lbl_cloud,LV_PCT(100));lv_label_set_long_mode(v->lbl_cloud,LV_LABEL_LONG_DOT);

    lv_obj_t *workspace=lv_obj_create(row);lv_obj_remove_style_all(workspace);
    lv_obj_set_size(workspace,0,LV_PCT(100));lv_obj_set_flex_grow(workspace,1);
    lv_obj_set_flex_flow(workspace,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(workspace,10,0);
    lv_obj_clear_flag(workspace,LV_OBJ_FLAG_SCROLLABLE);
    v->card_job=ui_create_card(workspace);lv_obj_set_flex_grow(v->card_job,1);
    lv_obj_set_style_min_height(v->card_job,140,0);lv_obj_set_style_border_width(v->card_job,0,0);
    lv_obj_t *job_header=ui_create_row(v->card_job);
    lv_obj_t *job_title=ui_create_label(job_header,tr("当前打印"),UI_FONT_BODY,UI_COLOR_TEXT);lv_obj_set_flex_grow(job_title,1);
    ui_create_btn(job_header,LV_SYMBOL_VIDEO " 实时监控",LV_SIZE_CONTENT,32,UI_BTN_SUBTLE,on_status_click,NULL);
    lv_obj_t *job_body=ui_create_row(v->card_job);lv_obj_set_flex_grow(job_body,1);lv_obj_set_style_min_height(job_body,0,0);
    v->img_job_preview=lv_obj_create(job_body);lv_obj_set_size(v->img_job_preview,100,100);
    lv_obj_set_style_bg_color(v->img_job_preview,UI_COLOR_CARD_HI,0);lv_obj_set_style_border_width(v->img_job_preview,0,0);
    lv_obj_set_style_radius(v->img_job_preview,10,0);lv_obj_clear_flag(v->img_job_preview,LV_OBJ_FLAG_SCROLLABLE);home_job_preview_init(v);
    lv_obj_t *details=lv_obj_create(job_body);lv_obj_remove_style_all(details);
    lv_obj_set_size(details,0,LV_SIZE_CONTENT);lv_obj_set_flex_grow(details,1);lv_obj_set_flex_flow(details,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(details,8,0);
    v->lbl_job_file=ui_create_label(details,"—",UI_FONT_BODY,UI_COLOR_TEXT);lv_obj_set_width(v->lbl_job_file,LV_PCT(100));lv_label_set_long_mode(v->lbl_job_file,LV_LABEL_LONG_DOT);
    v->lbl_job_pct=ui_create_label(details,"0%",UI_FONT_NUM_SM,UI_COLOR_PRIMARY);
    v->lbl_job_remain=ui_create_label(details,"",UI_FONT_CAPTION,UI_COLOR_TEXT_SEC);lv_obj_set_width(v->lbl_job_remain,LV_PCT(100));lv_label_set_long_mode(v->lbl_job_remain,LV_LABEL_LONG_DOT);
    v->bar_job=lv_bar_create(v->card_job);lv_obj_set_size(v->bar_job,LV_PCT(100),6);lv_bar_set_range(v->bar_job,0,100);
    lv_obj_set_style_bg_color(v->bar_job,UI_COLOR_TRACK,LV_PART_MAIN);lv_obj_set_style_bg_color(v->bar_job,UI_COLOR_PRIMARY,LV_PART_INDICATOR);
    lv_obj_t *temps=ui_create_row(workspace);
    home_temperature(temps,tr("喷头"),&v->lbl_nozzle_val,&v->lbl_nozzle_tgt,on_menu);
    home_temperature(temps,tr("热床"),&v->lbl_bed_val,&v->lbl_bed_tgt,on_menu);
    v->tile_chamber=home_temperature(temps,tr("腔体"),&v->lbl_chamber_val,&v->lbl_chamber_tgt,on_menu);
    lv_obj_t *actions=ui_create_row(workspace);lv_obj_set_height(actions,58);
    static const char *icons[]={LV_SYMBOL_VIDEO,LV_SYMBOL_FILE,LV_SYMBOL_BARS,LV_SYMBOL_DRIVE,LV_SYMBOL_SETTINGS};
    static const char *labels[]={"相机","设备存储","控制","耗材","设置"};
    static const char *tags[]={"tab_camera","tab_files","tab_control","filament","tab_settings"};
    for(int i=0;i<5;++i)shortcut_btn(actions,icons[i],labels[i],on_menu,tags[i]);
    v->lbl_file=v->lbl_job_file;v->lbl_progress=v->lbl_job_pct;v->lbl_state=v->lbl_idle;v->card_status=v->card_device;
}

void HomeView_Create(HomeView_t *v, lv_obj_t *parent,
                     lv_event_cb_t on_status_click,
                     lv_event_cb_t on_menu,
                     lv_event_cb_t on_cancel_job)
{
    const bool land = ui_screen_is_landscape();

    lv_obj_t *root = ui_create_page_root(parent);
    v->root = root;
    lv_obj_set_style_bg_color(root, UI_COLOR_BG, 0);

    lv_obj_t *content = ui_create_content(root);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_min_height(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    if (!land) {
        home_build_portrait(v, content, on_status_click, on_menu, on_cancel_job);
        return;
    }

    home_build_landscape(v, content, on_status_click, on_menu);
    v->footer = ui_create_page_footer(root);
    v->btn_cancel_job = ui_create_btn_bind(v->footer, "停止任务", LV_PCT(100),
                                           ui_land_btn_h_sm(), UI_BTN_OUTLINE, on_cancel_job, NULL);
    lv_obj_add_flag(v->footer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(v->btn_cancel_job, LV_OBJ_FLAG_HIDDEN);
}
