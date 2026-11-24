#pragma once

#include "bambu_model.h"
#include "bambu_status.h"
#include "bambu_studio_api.h"

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BAMBU_SVC_STOPPED = 0,
    BAMBU_SVC_CONNECTING,
    BAMBU_SVC_CONNECTED,
    BAMBU_SVC_AUTH_ERROR,
    BAMBU_SVC_NETWORK_ERROR,
} bambu_svc_state_t;

typedef struct {
    char printer_ip[48];
    char serial[32];
    char access_code[64];
    char nickname[48];
    bambu_model_t model;
    bool verify_printer;
} bambu_lan_config_t;

typedef struct {
    bambu_svc_state_t state;
    bool connected;
    char message[96];
    bambu_status_t printer;
} bambu_svc_snapshot_t;

void bambu_service_init(void);
esp_err_t bambu_service_apply_lan(const bambu_lan_config_t *cfg);
void bambu_service_stop(void);
void bambu_service_snapshot(bambu_svc_snapshot_t *out);
/** 当前 LAN 连接参数（与 MQTT 相同；未配置时返回 ESP_ERR_NOT_FOUND） */
esp_err_t bambu_service_copy_lan_config(bambu_lan_config_t *out);

esp_err_t bambu_service_request_pushall(void);
esp_err_t bambu_service_pause(void);
esp_err_t bambu_service_resume(void);
esp_err_t bambu_service_stop_print(void);
esp_err_t bambu_service_set_speed_level(int level_1_to_4);
esp_err_t bambu_service_set_chamber_light(bool on);
esp_err_t bambu_service_set_work_light(bool on);
esp_err_t bambu_service_set_nozzle_target_c(int celsius);
esp_err_t bambu_service_set_bed_target_c(int celsius);
/**
 * LAN 运动/温控（JSON 与 gcode 对齐 Bambu Studio DeviceCore）：
 * - xyz_ctrl / back_to_center / set_bed_temp / set_nozzle_temp / set_extrusion_length
 * - 否则 fallback 为 Studio DevAxisCtrl 同款 M211/M1002/G91 多行 gcode
 */
esp_err_t bambu_service_jog_axis(char axis, float delta_mm, int feedrate_mm_min);
esp_err_t bambu_service_home_axes(void);
esp_err_t bambu_service_extrude_mm(float length_mm);
/** 低级 gcode_line（param 须为单行或 \\n 分隔；优先用上面封装） */
esp_err_t bambu_service_send_gcode_line(const char *gcode);
typedef enum {
    BAMBU_PRINT_STORAGE_ROOT = 0,
    BAMBU_PRINT_STORAGE_CACHE,
} bambu_print_storage_t;

/** 打印 FTPS 上已有文件（.gcode.3mf → project_file，纯 .gcode → gcode_file） */
esp_err_t bambu_service_start_print_file(const char *filename_on_printer,
                                         bambu_print_storage_t storage);
/** 带 Studio 打印选项（调平/AMS/首层检测等） */
esp_err_t bambu_service_start_print_file_ex(const char *filename_on_printer,
                                            bambu_print_storage_t storage,
                                            const bambu_project_print_opts_t *opts);

/** Verify that the printer supplying the file list is the active, idle LAN printer. */
esp_err_t bambu_service_prepare_sd_print(const char *serial, uint32_t *generation,
                                         char *printer_name, size_t name_capacity);
/** Send once, only if that LAN session is still active and idle. */
esp_err_t bambu_service_start_sd_print(const char *path, uint32_t generation);
int bambu_service_project_plate(const char *filename);

/* ── 命令应答门控（report 主题回包 sequence_id/result 关联，架构文档阶段 C） ── */

typedef enum {
    BAMBU_ACK_PENDING = 0,   /* 已发出，尚未收到应答 */
    BAMBU_ACK_OK,            /* result == "success" */
    BAMBU_ACK_FAIL,          /* result 为失败原因（非 success 且非空） */
    BAMBU_ACK_EXPIRED,       /* 序号不在环形记录内（>8 条之前 / 超过 90s） */
} bambu_ack_state_t;

/** 当前调用任务最近一次发布的命令序号（发送函数返回 ESP_OK 后立即读取，用于应答查询） */
uint32_t bambu_service_last_seq(void);

/**
 * 查询某序号的应答。result_out 可为 NULL；返回时填 result 原文（"success"/失败原因）。
 * 环形记录最近 8 条、90s 内有效（push_status 风暴下仍可精确关联）。
 */
bambu_ack_state_t bambu_service_ack_query(uint32_t seq, char *result_out, size_t cap);

/** 同步等待应答（供工作线程调用；UI/LVGL 线程禁用）。超时返回 BAMBU_ACK_PENDING。 */
bambu_ack_state_t bambu_service_wait_ack(uint32_t seq, uint32_t timeout_ms,
                                         char *result_out, size_t cap);

/**
 * 应答通知回调（MQTT 任务上下文！UI 侧须经事件队列转到 LVGL 线程）。
 * ok = (result == "success")；result 为回包原文。
 */
typedef void (*bambu_ack_cb_t)(uint32_t seq, bool ok, const char *result, void *user_data);
void bambu_service_set_ack_cb(bambu_ack_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
