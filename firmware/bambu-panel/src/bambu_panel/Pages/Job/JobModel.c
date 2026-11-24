#include "JobModel.h"

#include "bambu_model.h"
#include "bambu_cloud.h"
#include "bambu_profile.h"
#include "bambu_service.h"

#include <math.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

static void fmt_time(char *buf, size_t cap, int total_sec)
{
    if (total_sec < 0) {
        snprintf(buf, cap, "--:--:--");
        return;
    }
    unsigned s = (unsigned)total_sec;
    snprintf(buf, cap, "%02u:%02u:%02u", s / 3600, (s % 3600) / 60, s % 60);
}

static const char *state_text(bambu_print_state_t st)
{
    switch (st) {
    case BAMBU_PRINT_RUNNING:  return "打印中";
    case BAMBU_PRINT_PAUSED:   return "已暂停";
    case BAMBU_PRINT_PREPARE:  return "准备中";
    case BAMBU_PRINT_FINISHED: return "打印完成";
    case BAMBU_PRINT_FAILED:   return "打印出错";
    case BAMBU_PRINT_IDLE:     return "空闲";
    default:                   return "未连接";
    }
}

static void fmt_temperature(char *out, size_t cap, float current, float target)
{
    if (!isfinite(current)) snprintf(out, cap, "--");
    else if (!isfinite(target)) snprintf(out, cap, "%.0f°C", current);
    else snprintf(out, cap, "%.0f / %.0f", current, target);
}

void JobModel_GetSnapshot(JobModel_t *out, const char *cloud_serial)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    bambu_status_t printer;
    const char *connection_state = "未连接";
    bool available = false;
    bool stale = false;
    if (cloud_serial) {
        bambu_cloud_view_t cloud;
        bambu_cloud_device_t selected;
        bambu_cloud_selected_snapshot(&cloud, &selected);
        connection_state = "请重新选择打印机";
        if (cloud.signed_in && cloud_serial[0] && !strcasecmp(cloud_serial, selected.serial)) {
            printer = cloud.status;
            available = printer.has_data;
            stale = available && !cloud.live;
            connection_state = "等待打印机上报";
            out->show_chamber = bambu_model_supports_chamber(bambu_model_from_devmodel(selected.model));
            snprintf(out->mode_text, sizeof(out->mode_text), "%.55s",
                     selected.name[0] ? selected.name : selected.serial);
        }
    } else {
        bambu_lan_config_t profile = {0};
        if (bambu_service_copy_lan_config(&profile) != ESP_OK) bambu_profile_load(&profile);
        bambu_svc_snapshot_t snap;
        bambu_service_snapshot(&snap);
        printer = snap.printer;
        available = snap.connected && printer.has_data;
        out->can_control = snap.connected;
        out->show_chamber = bambu_model_supports_chamber(profile.model);
        snprintf(out->mode_text, sizeof(out->mode_text), "%.55s",
                 profile.nickname[0] ? profile.nickname : bambu_model_short_name(profile.model));
    }

    if (!available) {
        snprintf(out->state_text, sizeof(out->state_text), "%s", connection_state);
        snprintf(out->file_name, sizeof(out->file_name), "--");
        snprintf(out->layer_line, sizeof(out->layer_line), "层 -- / --");
        fmt_time(out->elapsed_text, sizeof(out->elapsed_text), -1);
        fmt_time(out->remaining_text, sizeof(out->remaining_text), -1);
        snprintf(out->nozzle_text, sizeof(out->nozzle_text), "--");
        snprintf(out->bed_text, sizeof(out->bed_text), "--");
        snprintf(out->chamber_text, sizeof(out->chamber_text), "--");
        return;
    }

    const bambu_status_t *p = &printer;
    out->printing = p->state == BAMBU_PRINT_RUNNING;
    out->paused = p->state == BAMBU_PRINT_PAUSED;
    out->job_active = out->printing || out->paused || p->state == BAMBU_PRINT_PREPARE;
    out->can_pause = out->can_control && out->printing;
    out->can_resume = out->can_control && out->paused;
    out->can_cancel = out->can_control && out->job_active;

    snprintf(out->state_text, sizeof(out->state_text), "%s", stale ? "状态待更新" : state_text(p->state));

    snprintf(out->file_caption, sizeof(out->file_caption), "当前任务");
    if (p->layer_total > 0 && p->layer_current >= 0) {
        snprintf(out->layer_line, sizeof(out->layer_line), "第 %d / %d 层", p->layer_current,
                 p->layer_total);
    } else {
        snprintf(out->layer_line, sizeof(out->layer_line), "层 -- / --");
    }
    strlcpy(out->file_name, p->task_name[0] ? p->task_name : "--", sizeof(out->file_name));
    strlcpy(out->gcode_file, p->gcode_file, sizeof(out->gcode_file));
    out->plate_idx = p->plate_idx;

    out->progress_known = p->progress_percent >= 0;
    out->progress = p->progress_percent;
    if (out->progress < 0) {
        out->progress = 0;
    }
    if (out->progress > 100) {
        out->progress = 100;
    }
    out->progress_tenths = out->progress * 10;

    int remain_s = p->remaining_minutes >= 0 ? p->remaining_minutes * 60 : -1;
    fmt_time(out->remaining_text, sizeof(out->remaining_text),
             out->printing ? remain_s : -1);

    int elapsed_s = -1;
    if (out->printing && remain_s >= 0 && out->progress > 0 && out->progress < 100) {
        int total = (remain_s * 100) / (100 - out->progress);
        elapsed_s = total - remain_s;
        if (elapsed_s < 0) {
            elapsed_s = 0;
        }
    }
    fmt_time(out->elapsed_text, sizeof(out->elapsed_text), elapsed_s);

    fmt_temperature(out->nozzle_text, sizeof(out->nozzle_text), p->nozzle_temp, p->nozzle_target);
    fmt_temperature(out->bed_text, sizeof(out->bed_text), p->bed_temp, p->bed_target);
    fmt_temperature(out->chamber_text, sizeof(out->chamber_text), p->chamber_temp, NAN);
    if (isfinite(p->chamber_temp) && p->chamber_temp > 0.5f) out->show_chamber = true;

    out->chamber_light_on = p->chamber_light_on;
    out->work_light_on = p->work_light_on;
}
