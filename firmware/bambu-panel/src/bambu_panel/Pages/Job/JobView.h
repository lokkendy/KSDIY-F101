#pragma once

#include "Page.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *img_cam;
    lv_obj_t *cam_panel, *cam_parent, *exit_fullscreen;
    bool fullscreen;
    lv_obj_t *lbl_cam_status;
    lv_obj_t *lbl_camera_name;
    lv_obj_t *box_cover;    /* 模型封面 96×96 容器 */
    lv_obj_t *img_cover;    /* 封面图（加载后显示，缩放至盒内） */
    lv_obj_t *lbl_cover_ph; /* 未加载占位图标 */
    lv_obj_t *lbl_file;
    lv_obj_t *lbl_state;
    lv_obj_t *lbl_layer;
    lv_obj_t *lbl_pct;
    lv_obj_t *bar_progress;
    lv_obj_t *lbl_elapsed;
    lv_obj_t *lbl_remaining;
    lv_obj_t *lbl_nozzle;
    lv_obj_t *lbl_bed;
    lv_obj_t *lbl_chamber;
    lv_obj_t *tile_chamber;
    lv_obj_t *btn_pause;
    lv_obj_t *btn_cancel;
    lv_obj_t *btn_light;
    lv_obj_t *btn_record;
} JobView_t;

void JobView_Create(JobView_t *v, lv_obj_t *parent,
                    lv_event_cb_t on_pause,
                    lv_event_cb_t on_cancel,
                    lv_event_cb_t on_light,
                    lv_event_cb_t on_photo, lv_event_cb_t on_fullscreen, lv_event_cb_t on_record);
void JobView_SetFullscreen(JobView_t *v, bool fullscreen);
