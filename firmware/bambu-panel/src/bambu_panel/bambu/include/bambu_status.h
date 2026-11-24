#pragma once

/*
 * Bambu MQTT reports are incremental patches: a missing field means
 * "unchanged", not zero.  This transport-neutral state object keeps the merge
 * rule in one place so the Windows/OpenSSL client and a future ESP-IDF MQTT
 * port can share the same printer semantics.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BAMBU_PRINT_UNKNOWN = 0,
    BAMBU_PRINT_IDLE,
    BAMBU_PRINT_RUNNING,
    BAMBU_PRINT_PAUSED,
    BAMBU_PRINT_PREPARE,
    BAMBU_PRINT_FINISHED,
    BAMBU_PRINT_FAILED,
} bambu_print_state_t;

#define BAMBU_AMS_UNITS 4
#define BAMBU_AMS_TRAYS (BAMBU_AMS_UNITS * 4)

typedef struct {
    bool present;
    int unit_id,slot_id;
    int remain_percent; /* -1 means printer did not report an estimate */
    char color_hex[9];   /* RRGGBBAA 或空 */
    char type[16];       /* PLA / PETG … */
    char tray_id[4];
} bambu_ams_tray_t;

typedef struct {
    bool has_data;
    bambu_print_state_t state;
    float nozzle_temp;
    float nozzle_target;
    float bed_temp;
    float bed_target;
    float chamber_temp;
    int progress_percent;
    int remaining_minutes;
    int layer_current;
    int layer_total;
    char task_name[96];
    char gcode_file[96];  /* 当前任务 gcode_file 原文（/data/… 为 ramdisk 不可达，候选回退 subtask） */
    int plate_idx;        /* 当前盘号：0 未知，否则 1..N（封面 = Metadata/plate_N.png） */
    int speed_level;       /* spd_lvl 1..4 */
    int speed_mag;         /* 百分比倍率 */
    int wifi_signal;       /* dBm 或设备原始值 */
    int cooling_fan_pct;
    int heatbreak_fan_pct;
    int big_fan1_pct;
    int big_fan2_pct;
    uint32_t print_error;
    int hms_count;
    char hms_summary[80];  /* 首条 HMS 或条数摘要 */
    bool chamber_light_on;
    bool work_light_on;
    bambu_ams_tray_t ams_trays[BAMBU_AMS_TRAYS];
    int ams_tray_count;
    bool ams_installed;
    int ams_active_tray;
    char fw_version[32];
    char fw_machine[32];
    /** print.fun 十六进制能力串（report 合并）；对齐 Bambu Studio DevUtil::get_flag_bits */
    char fun_hex[48];
    bool cap_mqtt_homing;     /* fun bit 32 → back_to_center */
    bool cap_mqtt_axis;       /* fun bit 38 → xyz_ctrl */
    bool cap_mqtt_bed_temp;   /* fun bit 39 → set_bed_temp */
} bambu_status_t;

void bambu_status_reset(bambu_status_t *status);
bool bambu_status_apply_json(bambu_status_t *status,
                             const char *json, size_t json_len);
/** 从完整 report JSON 合并 print 扩展字段与 info.get_version（可与 stream 结果叠加） */
bool bambu_status_apply_report_json(bambu_status_t *status,
                                    const char *json, size_t json_len);

#ifdef __cplusplus
}
#endif
