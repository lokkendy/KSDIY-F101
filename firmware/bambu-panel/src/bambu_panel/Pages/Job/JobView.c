#include "JobView.h"

#include "i18n.h"
#include "ui_bambu.h"
#include "ui_screen.h"

/* 封面行：96×96 深色圆角盒（占位图标→加载后换 PNG）+ 右侧文字列；返回文字列 */
static lv_obj_t *cover_block(JobView_t *v, lv_obj_t *parent)
{
    lv_obj_t *row = ui_create_row(parent);

    v->box_cover = lv_obj_create(row);
    lv_obj_remove_style_all(v->box_cover);
    lv_obj_set_size(v->box_cover, 96, 96);
    lv_obj_set_style_radius(v->box_cover, 12, 0);
    lv_obj_set_style_bg_color(v->box_cover, UI_BAMBU_BG, 0);
    lv_obj_set_style_bg_opa(v->box_cover, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(v->box_cover, 1, 0);
    lv_obj_set_style_border_color(v->box_cover, UI_COLOR_BORDER, 0);
    lv_obj_clear_flag(v->box_cover, LV_OBJ_FLAG_SCROLLABLE);

    v->lbl_cover_ph = ui_create_label(v->box_cover, LV_SYMBOL_IMAGE, UI_FONT_BODY, UI_COLOR_TEXT_SEC);
    lv_obj_center(v->lbl_cover_ph);

    v->img_cover = lv_image_create(v->box_cover);
    lv_obj_add_flag(v->img_cover, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    return col;
}

static void fit_portrait_preview(lv_obj_t *viewport)
{
    lv_coord_t width = lv_obj_get_width(viewport);
    if (width <= 0) return;
    lv_coord_t height = (width * 9 + 8) / 16;
    if (lv_obj_get_height(viewport) != height) lv_obj_set_height(viewport, height);
}

static void on_portrait_preview_size(lv_event_t *event)
{
    JobView_t *view = lv_event_get_user_data(event);
    if (!view->fullscreen) fit_portrait_preview(lv_event_get_target_obj(event));
}

void JobView_Create(JobView_t *v, lv_obj_t *parent,
                    lv_event_cb_t on_pause,
                    lv_event_cb_t on_cancel,
                    lv_event_cb_t on_light,
                    lv_event_cb_t on_photo, lv_event_cb_t on_fullscreen, lv_event_cb_t on_record)
{
    const bool land = ui_screen_is_landscape();
    const int gap = ui_land_gap();

    lv_obj_t *root = ui_create_page_root(parent);
    v->root = root;
    lv_obj_set_style_bg_color(root, UI_BAMBU_BG, 0);

    lv_obj_t *content = ui_create_content(root);
    lv_obj_set_style_pad_all(content, land ? UI_PAD_SM : UI_PAD, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_min_height(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    if (land) {
        /* 横屏：一行主内容 + 一行快捷钮，无重复底栏/快照条 */
        lv_obj_t *main_row = lv_obj_create(content);
        lv_obj_remove_style_all(main_row);
        lv_obj_set_width(main_row, LV_PCT(100));
        lv_obj_set_flex_grow(main_row, 1);
        lv_obj_set_style_min_height(main_row, 0, 0);
        lv_obj_set_flex_flow(main_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(main_row, gap, 0);
        lv_obj_clear_flag(main_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *cam_col = lv_obj_create(main_row);
        lv_obj_remove_style_all(cam_col);
        lv_obj_set_width(cam_col, LV_PCT(56));
        lv_obj_set_height(cam_col, LV_PCT(100));
        lv_obj_clear_flag(cam_col, LV_OBJ_FLAG_SCROLLABLE);
        v->cam_parent=cam_col;
        v->cam_panel=ui_bambu_cam_panel(cam_col, &v->img_cam, &v->lbl_cam_status,
                                               &v->lbl_camera_name);
        lv_image_set_inner_align(v->img_cam,LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_add_flag(v->img_cam,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(v->img_cam,on_fullscreen,LV_EVENT_CLICKED,NULL);
        v->exit_fullscreen=ui_create_btn(root,LV_SYMBOL_CLOSE " 退出全屏",150,44,UI_BTN_SUBTLE,on_fullscreen,NULL);
        lv_obj_add_flag(v->exit_fullscreen,LV_OBJ_FLAG_FLOATING|LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(v->exit_fullscreen,LV_ALIGN_TOP_RIGHT,-12,8);

        lv_obj_t *stat_col = lv_obj_create(main_row);
        lv_obj_remove_style_all(stat_col);
        lv_obj_set_flex_grow(stat_col, 1);
        lv_obj_set_height(stat_col, LV_PCT(100));
        lv_obj_set_flex_flow(stat_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(stat_col, 4, 0);
        lv_obj_clear_flag(stat_col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *info = ui_create_card(stat_col);
        lv_obj_set_style_bg_color(info, UI_BAMBU_CARD, 0);
        lv_obj_set_flex_grow(info, 1);
        lv_obj_t *cover_col = cover_block(v, info);
        lv_obj_t *file_row = ui_create_row(cover_col);
        v->lbl_file = ui_create_label(file_row, "—", UI_FONT_BODY, UI_COLOR_TEXT);
        lv_obj_set_flex_grow(v->lbl_file, 1);
        lv_label_set_long_mode(v->lbl_file, LV_LABEL_LONG_DOT);

        lv_obj_t *st_row = ui_create_row(cover_col);
        v->lbl_state = ui_create_label(st_row, tr("空闲"), UI_FONT_BODY, UI_COLOR_PRIMARY);
        lv_obj_set_flex_grow(v->lbl_state, 1);

        lv_obj_t *prog_row = ui_create_row(info);
        v->lbl_pct = ui_create_label(prog_row, "0%", UI_FONT_NUM_SM, UI_COLOR_PRIMARY);
        v->bar_progress = lv_bar_create(prog_row);
        lv_obj_set_flex_grow(v->bar_progress, 1);
        lv_obj_set_height(v->bar_progress, 10);
        lv_bar_set_range(v->bar_progress, 0, 100);
        lv_obj_set_style_bg_color(v->bar_progress, UI_COLOR_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_color(v->bar_progress, UI_COLOR_PRIMARY, LV_PART_INDICATOR);

        v->lbl_layer = ui_create_label(info, tr("层 — / —"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

        lv_obj_t *time_row = ui_create_row(info);
        v->lbl_elapsed = ui_create_label(time_row, "--:--", UI_FONT_CAPTION, UI_COLOR_TEXT);
        lv_obj_set_flex_grow(v->lbl_elapsed, 1);
        v->lbl_remaining = ui_create_label(time_row, "--:--", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);

        lv_obj_t *temp_row = ui_create_row(stat_col);
        ui_bambu_temp_tile(temp_row, tr("喷头"), &v->lbl_nozzle, lv_color_hex(0xFF8C42));
        ui_bambu_temp_tile(temp_row, tr("热床"), &v->lbl_bed, lv_color_hex(0x4DA3FF));
        v->tile_chamber = ui_bambu_temp_tile(temp_row, tr("腔体"), &v->lbl_chamber,
                                              lv_color_hex(0x8899AA));

        lv_obj_t *actions = lv_obj_create(content);
        lv_obj_remove_style_all(actions);
        lv_obj_set_width(actions, LV_PCT(100));
        /* 须 ≥ ui_bambu_action_btn 内容高（符号 + 中文），46 会裁切底部笔画 */
        lv_obj_set_height(actions, ui_screen_is_landscape() ? 62 : 64);
        lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(actions, gap, 0);
        ui_bambu_action_btn(actions, LV_SYMBOL_IMAGE, tr("拍照"), true, on_photo, NULL);
        v->btn_record = ui_bambu_action_btn(actions, LV_SYMBOL_VIDEO, tr("录制"), false, on_record, NULL);
        v->btn_light = ui_bambu_action_btn(actions, LV_SYMBOL_BULLET, tr("灯光"), false, on_light,
                                           NULL);
        ui_bambu_action_btn(actions, LV_SYMBOL_EYE_OPEN, tr("全屏"), false, on_fullscreen, NULL);

        lv_obj_t *job_actions=ui_create_row(stat_col);
        v->btn_pause=ui_create_btn(job_actions,tr("暂停"),0,36,UI_BTN_FILLED,on_pause,NULL);
        v->btn_cancel=ui_create_btn(job_actions,tr("停止"),0,36,UI_BTN_OUTLINE,on_cancel,NULL);
        return;
    }

    /* 竖屏：可滚动 + 底栏暂停/停止 */
    ui_create_tab_root_header_bind(content, "相机", NULL);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, UI_PAD_SM, 0);
    v->cam_parent=content;
    v->cam_panel=ui_bambu_cam_panel(content, &v->img_cam, &v->lbl_cam_status,
                                           &v->lbl_camera_name);
    lv_obj_set_height(v->cam_panel, LV_SIZE_CONTENT);
    lv_obj_t *viewport = lv_obj_get_parent(v->img_cam);
    lv_obj_set_flex_grow(viewport, 0);
    lv_obj_set_height(viewport, 1);
    lv_obj_add_event_cb(viewport, on_portrait_preview_size, LV_EVENT_SIZE_CHANGED, v);
    lv_image_set_inner_align(v->img_cam,LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_add_flag(v->img_cam,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(v->img_cam,on_fullscreen,LV_EVENT_CLICKED,NULL);
    v->exit_fullscreen=ui_create_btn(root,LV_SYMBOL_CLOSE " 退出全屏",150,44,UI_BTN_SUBTLE,on_fullscreen,NULL);
    lv_obj_add_flag(v->exit_fullscreen,LV_OBJ_FLAG_FLOATING|LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(v->exit_fullscreen,LV_ALIGN_TOP_RIGHT,-12,8);
    lv_obj_t *info = ui_create_card(content);
    lv_obj_t *cover_col = cover_block(v, info);
    v->lbl_file = ui_create_label(cover_col, "—", UI_FONT_BODY, UI_COLOR_TEXT);
    v->lbl_state = ui_create_label(cover_col, tr("空闲"), UI_FONT_BODY, UI_COLOR_PRIMARY);
    lv_obj_set_width(v->lbl_file, LV_PCT(100));
    lv_label_set_long_mode(v->lbl_file, LV_LABEL_LONG_DOT);
    v->lbl_pct = ui_create_label(info, "0%", UI_FONT_NUM_SM, UI_COLOR_TEXT);
    v->bar_progress = lv_bar_create(info);
    lv_obj_set_width(v->bar_progress, LV_PCT(100));
    lv_bar_set_range(v->bar_progress, 0, 100);
    v->lbl_layer = ui_create_label(info, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    v->lbl_elapsed = ui_create_label(info, "--:--", UI_FONT_BODY, UI_COLOR_TEXT);
    v->lbl_remaining = ui_create_label(info, "--:--", UI_FONT_BODY, UI_COLOR_TEXT_SEC);
    lv_obj_t *temp_row = ui_create_row(content);
    ui_bambu_temp_tile(temp_row, tr("喷头"), &v->lbl_nozzle, lv_color_hex(0xFF8C42));
    ui_bambu_temp_tile(temp_row, tr("热床"), &v->lbl_bed, lv_color_hex(0x4DA3FF));
    v->tile_chamber = ui_bambu_temp_tile(temp_row, tr("腔体"), &v->lbl_chamber,
                                          lv_color_hex(0x8899AA));

    lv_obj_t *portrait_actions=ui_create_row(content);lv_obj_set_height(portrait_actions,66);
    ui_bambu_action_btn(portrait_actions,LV_SYMBOL_IMAGE,tr("拍照"),false,on_photo,NULL);
    v->btn_record = ui_bambu_action_btn(portrait_actions,LV_SYMBOL_VIDEO,tr("录制"),false,on_record,NULL);
    v->btn_light=ui_bambu_action_btn(portrait_actions,LV_SYMBOL_BULLET,tr("灯光"),false,on_light,NULL);
    ui_bambu_action_btn(portrait_actions,LV_SYMBOL_EYE_OPEN,tr("全屏"),false,on_fullscreen,NULL);

    lv_obj_t *footer = ui_create_page_footer(root);
    lv_obj_t *btns = ui_create_row(footer);
    v->btn_pause = ui_create_btn(btns, tr("暂停"), LV_PCT(48), UI_BTN_H, UI_BTN_FILLED, on_pause,
                                 NULL);
    v->btn_cancel = ui_create_btn(btns, tr("停止"), LV_PCT(48), UI_BTN_H, UI_BTN_DANGER, on_cancel,
                                  NULL);
}

void JobView_SetFullscreen(JobView_t *v,bool fullscreen)
{
    if(!v || !v->cam_panel || fullscreen==v->fullscreen)return;
    v->fullscreen=fullscreen;
    lv_obj_t *viewport = lv_obj_get_parent(v->img_cam);
    if(fullscreen){
        lv_obj_set_flex_grow(viewport, 1);
        lv_obj_set_parent(v->cam_panel,v->root);
        lv_obj_add_flag(v->cam_panel,LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(v->cam_panel,LV_PCT(100),LV_PCT(100));
        lv_obj_align(v->cam_panel,LV_ALIGN_CENTER,0,0);
        lv_obj_move_foreground(v->cam_panel);
        lv_obj_remove_flag(v->exit_fullscreen,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(v->exit_fullscreen);
    }else{
        lv_obj_set_parent(v->cam_panel,v->cam_parent);
        lv_obj_remove_flag(v->cam_panel,LV_OBJ_FLAG_FLOATING);
        lv_obj_set_pos(v->cam_panel,0,0);
        bool landscape = ui_screen_is_landscape();
        lv_obj_set_size(v->cam_panel, LV_PCT(100), landscape ? LV_PCT(100) : LV_SIZE_CONTENT);
        if (!landscape) {
            lv_obj_set_flex_grow(viewport, 0);
            fit_portrait_preview(viewport);
        }
        lv_obj_add_flag(v->exit_fullscreen,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_to_index(v->cam_panel, landscape ? 0 : 1);
    }
}
