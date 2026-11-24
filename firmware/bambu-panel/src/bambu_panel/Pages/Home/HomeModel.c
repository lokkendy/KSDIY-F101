#include "HomeModel.h"

#include "i18n.h"
#include "bambu_cloud.h"
#include "bambu_profile.h"
#include "bambu_service.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool prefer_cloud;

void HomeModel_SetCloudPreferred(bool preferred)
{
    prefer_cloud = preferred;
}

static const char *print_state_text(bambu_print_state_t state)
{
    switch (state) {
    case BAMBU_PRINT_RUNNING:  return "打印中";
    case BAMBU_PRINT_PAUSED:   return "已暂停";
    case BAMBU_PRINT_PREPARE:  return "准备中";
    case BAMBU_PRINT_FINISHED: return "已完成";
    case BAMBU_PRINT_FAILED:   return "失败";
    case BAMBU_PRINT_IDLE:     return "空闲中";
    default:                   return "—";
    }
}

static void fmt_temp(char *val, size_t val_cap, char *tgt, size_t tgt_cap, float cur, float target)
{
    if (isfinite(cur)) snprintf(val, val_cap, "%.0f°C", cur);
    else strlcpy(val, "—", val_cap);
    if (isfinite(target)) snprintf(tgt, tgt_cap, tr("目标 %.0f°C"), target);
    else strlcpy(tgt, tr("目标 —"), tgt_cap);
}

static void identify(HomeModel_t *out, const char *name, bambu_model_t model)
{
    out->model = model;
    out->show_chamber = bambu_model_supports_chamber(model);
    snprintf(out->printer_name, sizeof(out->printer_name), "%s",
             name[0] ? name : bambu_model_short_name(model));
    snprintf(out->identity_text, sizeof(out->identity_text), "%s  |  %s",
             bambu_model_short_name(model), out->cloud ? tr("公网只读") : "LAN");
}

static void apply_status(HomeModel_t *out, const bambu_status_t *p)
{
    strlcpy(out->idle_sub, tr(print_state_text(p->state)), sizeof(out->idle_sub));
    strlcpy(out->state_text, out->idle_sub, sizeof(out->state_text));
    if (out->connected) {
        if (p->state == BAMBU_PRINT_FAILED) out->card_level = HOME_CARD_ERROR;
        else if (p->state == BAMBU_PRINT_PAUSED) out->card_level = HOME_CARD_WARN;
        else out->card_level = HOME_CARD_OK;
    }

    out->has_job_detail = p->state == BAMBU_PRINT_RUNNING || p->state == BAMBU_PRINT_PAUSED ||
                          p->state == BAMBU_PRINT_PREPARE;
    out->job_active = out->has_job_detail;
    strlcpy(out->gcode_file, p->gcode_file, sizeof(out->gcode_file));
    out->plate_idx = p->plate_idx;
    if (p->task_name[0]) strlcpy(out->file_text, p->task_name, sizeof(out->file_text));
    else if (!out->has_job_detail) strlcpy(out->file_text, tr("暂无任务"), sizeof(out->file_text));

    if (p->progress_percent >= 0) {
        out->progress_pct = p->progress_percent > 100 ? 100 : p->progress_percent;
        snprintf(out->progress_text, sizeof(out->progress_text), "%d%%", out->progress_pct);
    }
    if (p->remaining_minutes > 0 || (out->cloud && p->remaining_minutes == 0)) {
        snprintf(out->remain_text, sizeof(out->remain_text), tr("剩余 %d 分钟"), p->remaining_minutes);
    }

    if (out->has_job_detail && p->layer_total > 0 && p->layer_current >= 0) {
        snprintf(out->layer_text, sizeof(out->layer_text), tr("第 %d / %d 层"),
                 p->layer_current, p->layer_total);
    }

    fmt_temp(out->nozzle_val, sizeof(out->nozzle_val), out->nozzle_tgt, sizeof(out->nozzle_tgt),
             p->nozzle_temp, p->nozzle_target);
    fmt_temp(out->bed_val, sizeof(out->bed_val), out->bed_tgt, sizeof(out->bed_tgt),
             p->bed_temp, p->bed_target);
    if (out->show_chamber) {
        fmt_temp(out->chamber_val, sizeof(out->chamber_val), out->chamber_tgt, sizeof(out->chamber_tgt),
                 p->chamber_temp, out->cloud ? NAN : 35.f);
    }
}

static bool show_cloud(HomeModel_t *out)
{
    bambu_cloud_view_t cloud;
    bambu_cloud_device_t selected;
    bambu_cloud_selected_snapshot(&cloud, &selected);
    if (!cloud.signed_in) return false;

    out->cloud = true;
    strlcpy(out->serial, selected.serial, sizeof(out->serial));
    const char *name = selected.name[0] ? selected.name : selected.serial;
    bambu_model_t model = bambu_model_from_devmodel(selected.model);
    identify(out, name[0] ? name : tr("拓竹云监控"), model);
    strlcpy(out->cloud_line, tr("拓竹云监控"), sizeof(out->cloud_line));

    /* Live telemetry is fresher than the online flag from the device list. */
    bool offline = selected.online_known && !selected.online;
    out->connected = cloud.live || (!cloud.status.has_data && selected.online_known && selected.online);
    out->card_level = out->connected ? HOME_CARD_OK : offline ? HOME_CARD_DISCONNECTED : HOME_CARD_WARN;
    strlcpy(out->online_line, tr(out->connected ? "在线" : offline ? "离线" : "同步中"),
            sizeof(out->online_line));
    if (cloud.status.has_data) apply_status(out, &cloud.status);
    if (!cloud.live) {
        const char *hint = cloud.status.has_data ? "状态待更新" :
                           cloud.count ? "等待打印机上报" : "正在连接拓竹云";
        strlcpy(out->idle_sub, tr(hint), sizeof(out->idle_sub));
    }
    return true;
}

void HomeModel_GetSnapshot(HomeModel_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    strlcpy(out->file_text, "—", sizeof(out->file_text));
    strlcpy(out->progress_text, "—", sizeof(out->progress_text));
    strlcpy(out->remain_text, tr("剩余 —"), sizeof(out->remain_text));
    strlcpy(out->nozzle_val, "—", sizeof(out->nozzle_val));
    strlcpy(out->bed_val, "—", sizeof(out->bed_val));
    strlcpy(out->chamber_val, "—", sizeof(out->chamber_val));

    bambu_svc_snapshot_t snap;
    bambu_service_snapshot(&snap);
    bool lan_up = snap.connected || snap.state == BAMBU_SVC_CONNECTED;
    if ((prefer_cloud || !lan_up) && show_cloud(out)) return;
    if (prefer_cloud) {
        out->cloud = true;
        identify(out, tr("拓竹云监控"), BAMBU_MODEL_UNKNOWN);
        strlcpy(out->cloud_line, tr("拓竹云监控"), sizeof(out->cloud_line));
        strlcpy(out->online_line, tr("离线"), sizeof(out->online_line));
        strlcpy(out->idle_sub, tr("请重新登录"), sizeof(out->idle_sub));
        strlcpy(out->state_text, out->idle_sub, sizeof(out->state_text));
        return;
    }

    bambu_lan_config_t prof;
    bambu_profile_load(&prof);
    if (prof.model == BAMBU_MODEL_UNKNOWN) prof.model = BAMBU_MODEL_P1P;
    strlcpy(out->serial, prof.serial, sizeof(out->serial));
    identify(out, prof.nickname[0] ? prof.nickname : prof.serial, prof.model);
    out->connected = lan_up;
    strlcpy(out->cloud_line, tr(lan_up ? "局域网已连接" : "连接打印机"), sizeof(out->cloud_line));
    strlcpy(out->online_line, tr(lan_up ? "在线" : "离线"), sizeof(out->online_line));
    if (lan_up) {
        out->card_level = HOME_CARD_WARN;
        if (snap.printer.has_data) {
            apply_status(out, &snap.printer);
            return;
        }
    } else if (snap.state == BAMBU_SVC_AUTH_ERROR || snap.state == BAMBU_SVC_NETWORK_ERROR) {
        out->card_level = HOME_CARD_ERROR;
    }
    strlcpy(out->idle_sub, snap.message[0] ? snap.message :
            tr(lan_up ? "正在同步打印机状态…" : "未连接"), sizeof(out->idle_sub));
    strlcpy(out->state_text, out->idle_sub, sizeof(out->state_text));
}
