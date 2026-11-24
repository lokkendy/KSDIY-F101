#pragma once

#include "lvgl.h"
#include "ui_common.h"
#include "bambu_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bambu 命令应答事件（bambu_service ack 回调 → bridge_post_event → ui_events 泵）。
 * 事件 ID 区段 0x40+（框架级保留 0x80+，见 data_bridge.h）。
 */
#define BAMBU_EVT_ACK_OK   0x40  /* arg = sequence_id（低 16 位） */
#define BAMBU_EVT_ACK_FAIL 0x41

/** 设计稿背景/卡片色 */
#define UI_BAMBU_BG      UI_COLOR_BG
#define UI_BAMBU_CARD    UI_COLOR_CARD

/* NULL selects the local printer. A serial pins both media and status to the selected cloud printer. */
void ui_bambu_open_lan_media(bool camera, const char *serial);
const char *ui_bambu_media_cloud_serial(void);
bool ui_bambu_copy_media_config(const char *cloud_serial, bambu_lan_config_t *out);

lv_obj_t *ui_bambu_chip(lv_obj_t *parent, const char *text);

lv_obj_t *ui_bambu_temp_tile(lv_obj_t *parent, const char *title, lv_obj_t **val_out,
                             lv_color_t accent);

lv_obj_t *ui_bambu_cam_panel(lv_obj_t *parent, lv_obj_t **img_out, lv_obj_t **status_out,
                             lv_obj_t **title_out);

lv_obj_t *ui_bambu_action_btn(lv_obj_t *parent, const char *icon, const char *label,
                              bool primary, lv_event_cb_t cb, void *user_data);

/** 更新 action 按钮高亮（如灯光开/关） */
void ui_bambu_action_btn_set_active(lv_obj_t *btn, bool active);

/** AMS 竖列槽（设计稿四列横排） */
typedef struct {
    lv_obj_t *card;
    lv_obj_t *lbl_status;
    lv_obj_t *lbl_mat;
    lv_obj_t *lbl_sub;
    lv_obj_t *spool;
    lv_obj_t *bar;
} ui_bambu_ams_col_t;

lv_obj_t *ui_bambu_ams_column(lv_obj_t *parent, int slot_1based, ui_bambu_ams_col_t *out);
void ui_bambu_ams_column_select(ui_bambu_ams_col_t *col, bool selected);
void ui_bambu_ams_column_tint(ui_bambu_ams_col_t *col, lv_color_t spool, int remain_pct);

/** 文件列表行（缩略图 + 元数据，设计稿） */
lv_obj_t *ui_bambu_file_row_rich(lv_obj_t *parent, const char *name, const char *mat,
                                 const char *time_meta, bool selected);

/** 左侧存储源大按钮（本地/SD/云端） */
lv_obj_t *ui_bambu_storage_nav_btn(lv_obj_t *parent, const char *icon, const char *label,
                                   bool selected, lv_event_cb_t cb, void *user_data);

/** 底部打印任务条（设计稿：缩略图 + 文件名 + 进度 + 层/时间） */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *lbl_file;
    lv_obj_t *lbl_pct;
    lv_obj_t *bar;
    lv_obj_t *lbl_layer;
    lv_obj_t *lbl_time;
} ui_bambu_task_bar_t;

/** 兼容旧名 */
typedef ui_bambu_task_bar_t ui_bambu_job_strip_t;

lv_obj_t *ui_bambu_task_bar_create(lv_obj_t *page_root, ui_bambu_task_bar_t *out);
void ui_bambu_task_bar_update(ui_bambu_task_bar_t *s, const char *file, int pct,
                              const char *layer_line, const char *time_line);

static inline lv_obj_t *ui_bambu_job_strip_create(lv_obj_t *parent, ui_bambu_job_strip_t *out)
{
    return ui_bambu_task_bar_create(parent, out);
}

static inline void ui_bambu_job_strip_update(ui_bambu_job_strip_t *s, const char *file, int pct)
{
    ui_bambu_task_bar_update(s, file, pct, NULL, NULL);
}

/** 步进距离胶囊组（0.1 / 1 / 10） */
void ui_bambu_step_pills_create(lv_obj_t *parent, lv_obj_t *btns_out[3], lv_event_cb_t cb);

void ui_bambu_step_pills_set_active(lv_obj_t *btns[3], int idx_0_to_2);

#ifdef __cplusplus
}
#endif
