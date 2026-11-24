#pragma once
/**
 * LAN MQTT 命令面 — 对齐 Bambu Studio DeviceManager / DeviceCore。
 * 参考：F:/Github/BambuStudio/src/slic3r/GUI/DeviceManager.cpp
 */
#include "bambu_model.h"
#include "esp_err.h"

/* 仅 .c 内包含 cJSON.h；避免 lvgl_simulator 等无 IDF cjson 包含路径 */
struct cJSON;
typedef struct cJSON cJSON;
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── project_file（对齐 PrintParams 子集） ── */
typedef struct {
    bool bed_leveling;
    bool flow_cali;
    bool vibration_cali;
    bool layer_inspect;
    bool timelapse;
    bool use_ams;
    const char *bed_type; /* 默认 "auto" */
    int ams_mapping[8];
    int ams_mapping_count;
} bambu_project_print_opts_t;

void bambu_project_print_opts_defaults(bambu_project_print_opts_t *o);

/** 底层：向 device/{sn}/request 发布任意顶层 JSON（释放 root） */
esp_err_t bambu_mqtt_publish_root(cJSON *root);

/** 向 print/system/info/camera 等段发布（释放 body，自动 sequence_id） */
esp_err_t bambu_mqtt_publish_section(const char *section, cJSON *body);

/* info / pushing */
esp_err_t bambu_studio_get_version(void);
esp_err_t bambu_studio_get_access_code(void);

/* print · 任务 */
esp_err_t bambu_studio_print_pause(void);
esp_err_t bambu_studio_print_resume(void);
esp_err_t bambu_studio_print_stop(void);
esp_err_t bambu_studio_print_speed_level(int level_1_to_4);
esp_err_t bambu_studio_skip_objects(const int *part_ids, size_t count);
esp_err_t bambu_studio_clean_print_error(const char *subtask_id, int print_error);
esp_err_t bambu_studio_stop_buzzer(void);
esp_err_t bambu_studio_close_air_filter(void);

/* print · 校准（option 位掩码同 Studio command_start_calibration） */
esp_err_t bambu_studio_start_calibration(bool vibration, bool bed_leveling, bool xcam_cali,
                                         bool motor_noise, bool nozzle_cali, bool bed_cali,
                                         bool clumppos_cali);

/* print · 选项 */
esp_err_t bambu_studio_print_option_auto_recovery(bool on);
esp_err_t bambu_studio_print_option_ams_auto_refill(bool on);
esp_err_t bambu_studio_print_option_air_print_detect(bool on);

/* print · AMS */
esp_err_t bambu_studio_ams_change_filament(bool load, int ams_id, int slot_id, int curr_temp,
                                           int tar_temp);
esp_err_t bambu_studio_ams_control(const char *action); /* resume|reset|pause|done|abort */
esp_err_t bambu_studio_ams_reset(void);
esp_err_t bambu_studio_ams_drying_stop(void);
esp_err_t bambu_studio_ams_refresh_rfid(int ams_id, int slot_id);
esp_err_t bambu_studio_ams_select_tray_gcode(int tray_id);

/* print · 温控 / 挤出（Studio 原生 command） */
esp_err_t bambu_studio_set_bed_temp(int celsius);
esp_err_t bambu_studio_set_nozzle_temp(int extruder_index, int celsius);
esp_err_t bambu_studio_refresh_nozzle(void);
esp_err_t bambu_studio_set_extrusion_length(int extruder_index, int length_mm);

/* print · 文件 */
/* Construct print payloads for the LAN service. The caller owns the result. */
cJSON *bambu_studio_gcode_payload(const char *param_path);
cJSON *bambu_studio_project_payload(const char *plate_param, const char *subtask_name,
                                   const char *file_url, const bambu_project_print_opts_t *opts);
esp_err_t bambu_studio_gcode_file(const char *param_path);
esp_err_t bambu_studio_project_file(const char *plate_param, const char *subtask_name,
                                    const char *file_url, const bambu_project_print_opts_t *opts);

/* system */
esp_err_t bambu_studio_ledctrl(const char *node, const char *mode);

/* camera */
esp_err_t bambu_studio_camera_timelapse(bool enable);
esp_err_t bambu_studio_camera_record(bool enable);
esp_err_t bambu_studio_camera_resolution(const char *resolution);

#ifdef __cplusplus
}
#endif
