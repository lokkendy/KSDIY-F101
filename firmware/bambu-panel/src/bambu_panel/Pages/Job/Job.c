#include "ui_bambu_command.h"
#include "Job.h"
#include "JobModel.h"
#include "JobView.h"

#include "bambu_camera.h"
#include "bambu_job_cover.h"
#include "bambu_preview.h"
#include "bambu_service.h"
#include "HAL_SD.h"
#include "ui_modal.h"
#include <time.h>
#include "i18n.h"
#include "ui_bambu.h"
#include "ui_dispatch.h"

#include "esp_err.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    JobView_t view;
    lv_timer_t *timer;

    bambu_job_cover_t *job_cover;
} job_presenter_t;

static job_presenter_t s_job;

static void update_label(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void set_enabled(lv_obj_t *button, bool enabled)
{
    if (!button || lv_obj_has_state(button, LV_STATE_DISABLED) != enabled) return;
    if (enabled) lv_obj_remove_state(button, LV_STATE_DISABLED);
    else lv_obj_add_state(button, LV_STATE_DISABLED);
}

static void job_update_preview(void)
{
    bambu_lan_config_t cfg;
    const char *serial = ui_bambu_media_cloud_serial();
    char message[96];
    if (ui_bambu_copy_media_config(serial, &cfg)) {
        bambu_cam_state_t state;
        bambu_preview_open(s_job.view.img_cam, &cfg);
        bambu_preview_update(s_job.view.img_cam, &state, message, sizeof(message));
    } else {
        bambu_preview_close(s_job.view.img_cam);
        strlcpy(message, serial ? "当前打印机暂无局域网画面" : "请先连接打印机", sizeof(message));
    }
    memset(&cfg, 0, sizeof(cfg));
    update_label(s_job.view.lbl_cam_status, tr(message));
}

static void job_refresh(void)
{
    JobModel_t m;
    JobModel_GetSnapshot(&m, ui_bambu_media_cloud_serial());
    JobView_t *v = &s_job.view;

    update_label(v->lbl_state, tr(m.state_text));
    update_label(v->lbl_camera_name, m.mode_text[0] ? m.mode_text : tr("实时相机"));
    update_label(v->lbl_file, m.file_name);
    update_label(v->lbl_layer, m.layer_line);
    update_label(v->lbl_elapsed, m.elapsed_text);
    update_label(v->lbl_remaining, m.remaining_text);
    update_label(v->lbl_nozzle, m.nozzle_text);
    update_label(v->lbl_bed, m.bed_text);
    update_label(v->lbl_chamber, m.chamber_text);
    lv_obj_set_flag(v->tile_chamber, LV_OBJ_FLAG_HIDDEN, !m.show_chamber);

    if (lv_bar_get_value(v->bar_progress) != m.progress)
        lv_bar_set_value(v->bar_progress, m.progress, LV_ANIM_OFF);
    char pct[16] = "--";
    if (m.progress_known) snprintf(pct, sizeof(pct), "%d%%", m.progress);
    update_label(v->lbl_pct, pct);

    lv_color_t st_col = UI_COLOR_TEXT;
    if (m.printing) {
        st_col = UI_COLOR_PRIMARY;
    } else if (m.paused) {
        st_col = UI_COLOR_WARNING;
    }
    if (!lv_color_eq(lv_obj_get_style_text_color(v->lbl_state, 0), st_col))
        lv_obj_set_style_text_color(v->lbl_state, st_col, 0);

    if (v->btn_pause) {
        update_label(lv_obj_get_child(v->btn_pause, 0), tr(m.can_resume ? "继续" : "暂停"));
    }
    set_enabled(v->btn_pause, m.can_pause || m.can_resume);
    set_enabled(v->btn_cancel, m.can_cancel);
    set_enabled(v->btn_light, m.can_control);
    set_enabled(v->btn_record, m.can_control);
    if (v->btn_light && m.can_control)
        ui_bambu_action_btn_set_active(v->btn_light, m.chamber_light_on);

    bambu_job_cover_maybe_fetch(s_job.job_cover, m.job_active && !ui_bambu_media_cloud_serial(),
                                m.file_name, m.gcode_file, m.plate_idx);
}

void job_ui_refresh(void)
{
    if (!s_job.view.root || !lv_obj_is_valid(s_job.view.root)) {
        return;
    }
    job_refresh();
}

static void on_pause(lv_event_t *e)
{ (void)e;JobModel_t m;JobModel_GetSnapshot(&m, ui_bambu_media_cloud_serial());ui_bambu_command_result(m.can_resume?bambu_service_resume():m.can_pause?bambu_service_pause():ESP_ERR_INVALID_STATE,m.can_resume?"继续打印":"暂停打印");job_refresh(); }

static void on_light(lv_event_t *e)
{ (void)e;JobModel_t m;JobModel_GetSnapshot(&m, ui_bambu_media_cloud_serial());ui_bambu_command_result(m.can_control?bambu_service_set_chamber_light(!m.chamber_light_on):ESP_ERR_INVALID_STATE,"机箱灯"); }

static bool s_snapshot_busy;
typedef struct {uint8_t *jpeg;size_t bytes;uint32_t seq;} camera_shot_t;
static void shot_result_ui(void *arg)
{
    s_snapshot_busy=false;
    ui_toast(tr((uintptr_t)arg?"照片已保存到 TF 卡":"保存照片失败，请检查 TF 卡"),(uintptr_t)arg?UI_TOAST_SUCCESS:UI_TOAST_ERROR);
}
static void save_shot_task(void *arg)
{
    camera_shot_t *shot=arg;char path[100];
    snprintf(path,sizeof(path),"/sdcard/Bambu_%lu_%lu_%lu.jpg",(unsigned long)time(NULL),(unsigned long)shot->seq,(unsigned long)xTaskGetTickCount());
    bool ok=false;FILE *f=fopen(path,"wb");
    if(f){size_t n=fwrite(shot->jpeg,1,shot->bytes,f);ok=n==shot->bytes&&!ferror(f);if(fflush(f))ok=false;if(fclose(f))ok=false;if(!ok)remove(path);}
    free(shot->jpeg);free(shot);ui_post(shot_result_ui,(void*)(uintptr_t)ok);vTaskDelete(NULL);
}
static void on_photo(lv_event_t *e)
{
    (void)e;
    if(s_snapshot_busy)return;
    if(!HAL_SD_IsMounted()){ui_toast(tr("请先插入 TF 卡"),UI_TOAST_INFO);return;}
    camera_shot_t *shot=calloc(1,sizeof(*shot));
    if(shot)shot->jpeg=malloc(CONFIG_BAMBU_CAMERA_FRAME_MAX);
    if(!shot || !shot->jpeg){if(shot)free(shot);ui_toast(tr("内存不足，暂时无法保存照片"),UI_TOAST_ERROR);return;}
    if(!bambu_camera_read_frame(shot->jpeg,CONFIG_BAMBU_CAMERA_FRAME_MAX,&shot->bytes,&shot->seq)){
        free(shot->jpeg);free(shot);ui_toast(tr("暂未收到相机画面"),UI_TOAST_INFO);return;
    }
    s_snapshot_busy=true;
    if(xTaskCreate(save_shot_task,"bambu_shot",3072,shot,2,NULL)!=pdPASS){s_snapshot_busy=false;free(shot->jpeg);free(shot);}
}
static void on_record_choice(lv_event_t *e)
{
    bool on=(bool)(uintptr_t)lv_event_get_user_data(e);
    JobModel_t model;
    JobModel_GetSnapshot(&model, ui_bambu_media_cloud_serial());
    esp_err_t rc = model.can_control ? bambu_studio_camera_record(on) : ESP_ERR_INVALID_STATE;ui_modal_close_top();
    ui_bambu_command_result(rc,on?"开始录制":"停止录制");
}
static void record_choices(lv_obj_t *card,void *arg)
{
    (void)arg;ui_create_section_title(card,tr("打印机相机录制"));
    ui_create_label(card,tr("录制文件保存到打印机存储卡"),UI_FONT_CAPTION,UI_COLOR_TEXT_SEC);
    lv_obj_t *row=ui_create_row(card);
    ui_create_btn(row,tr("开始录制"),0,40,UI_BTN_FILLED,on_record_choice,(void*)1);
    ui_create_btn(row,tr("停止录制"),0,40,UI_BTN_OUTLINE,on_record_choice,NULL);
}
static void on_record(lv_event_t *e)
{
    (void)e;
    JobModel_t model;
    JobModel_GetSnapshot(&model, ui_bambu_media_cloud_serial());
    if (!model.can_control) return;
    ui_modal_builder_t b={.build=record_choices,.width=ui_screen_is_landscape()?400:380,.close_on_outside=true};ui_modal_open(&b);
}
static void on_fullscreen(lv_event_t *e)
{
    (void)e;bool full=!s_job.view.fullscreen;
    pm_set_chrome_visible(!full);JobView_SetFullscreen(&s_job.view,full);
}

static void on_cancel(lv_event_t *e)
{ (void)e;JobModel_t m;JobModel_GetSnapshot(&m, ui_bambu_media_cloud_serial());ui_bambu_command_result(m.can_cancel?bambu_service_stop_print():ESP_ERR_INVALID_STATE,"停止打印");job_refresh(); }

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    job_refresh();
    job_update_preview();
}

static lv_obj_t *job_create(lv_obj_t *parent, void *user_data)
{
    (void)user_data;
    memset(&s_job, 0, sizeof(s_job));
    JobView_Create(&s_job.view, parent, on_pause, on_cancel, on_light, on_photo, on_fullscreen, on_record);
    s_job.job_cover = bambu_job_cover_create(s_job.view.img_cover, s_job.view.lbl_cover_ph);
    s_job.timer = lv_timer_create(timer_cb, 800, NULL);
    job_update_preview();
    job_refresh();
    return s_job.view.root;
}

static void job_destroy(void *user_data)
{
    (void)user_data;
    if (s_job.timer) {
        lv_timer_delete(s_job.timer);
        s_job.timer = NULL;
    }
    bambu_preview_close(s_job.view.img_cam);
    memset(&s_job.view, 0, sizeof(s_job.view));
    bambu_job_cover_destroy(s_job.job_cover);
    s_job.job_cover = NULL;
}

static void job_on_pause(void *user_data)
{
    (void)user_data;
    if (s_job.timer) {
        lv_timer_pause(s_job.timer);
    }
    if(s_job.view.fullscreen){JobView_SetFullscreen(&s_job.view,false);pm_set_chrome_visible(true);}
    bambu_preview_close(s_job.view.img_cam);
}

static void job_on_resume(void *user_data)
{
    (void)user_data;
    if (s_job.timer) {
        lv_timer_resume(s_job.timer);
    }
    job_update_preview();
    job_refresh();
}

static void job_on_language(void *user_data)
{
    (void)user_data;
    job_refresh();
}

const page_t page_job = {
    .name = "相机",
    .create = job_create,
    .destroy = job_destroy,
    .on_pause = job_on_pause,
    .on_resume = job_on_resume,
    .on_language = job_on_language,
    .cache = PM_CACHE_KEEP_ALIVE,
    .user_data = NULL,
};
