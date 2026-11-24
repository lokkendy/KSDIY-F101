#pragma once

#include "Page.h"

typedef struct {
    lv_obj_t *root;
    /* 左栏 · 设备 */
    lv_obj_t *card_device;
    lv_obj_t *lbl_printer_name;
    lv_obj_t *lbl_online;
    lv_obj_t *status_dot;
    lv_obj_t *lbl_idle;
    lv_obj_t *img_printer;
    lv_obj_t *img_photo;      /* 机型照片（lodepng 解码，随 model 切换） */
    lv_obj_t *lbl_photo_ph;   /* 无照片时的占位图标 */
    lv_obj_t *btn_cloud;
    lv_obj_t *lbl_cloud;
    lv_obj_t *img_camera;
    lv_obj_t *lbl_camera;
    /* 中栏 · 当前打印 */
    lv_obj_t *card_job;
    lv_obj_t *lbl_job_file;
    lv_obj_t *img_job_preview;
    lv_obj_t *img_job_cover;
    lv_obj_t *lbl_job_cover_ph;
    lv_obj_t *lbl_job_pct;
    lv_obj_t *bar_job;
    lv_obj_t *lbl_job_remain;
    lv_obj_t *lbl_job_layer;
    lv_obj_t *lbl_nozzle_val;
    lv_obj_t *lbl_nozzle_tgt;
    lv_obj_t *lbl_bed_val;
    lv_obj_t *lbl_bed_tgt;
    lv_obj_t *tile_chamber;
    lv_obj_t *lbl_chamber_val;
    lv_obj_t *lbl_chamber_tgt;
    /* 竖屏遗留 */
    lv_obj_t *card_status;
    lv_obj_t *lbl_state;
    lv_obj_t *lbl_progress;
    lv_obj_t *lbl_file;
    lv_obj_t *lbl_identity;
    lv_obj_t *btn_cancel_job;
    lv_obj_t *footer;
} HomeView_t;

void HomeView_Create(HomeView_t *v, lv_obj_t *parent,
                     lv_event_cb_t on_status_click,
                     lv_event_cb_t on_menu,
                     lv_event_cb_t on_cancel_job);
