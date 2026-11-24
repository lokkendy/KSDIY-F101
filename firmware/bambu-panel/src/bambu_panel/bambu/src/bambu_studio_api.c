#include "bambu_studio_api.h"

#include "bambu_service.h"

#include "bambu_cJSON.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "bambu_studio";

void bambu_project_print_opts_defaults(bambu_project_print_opts_t *o)
{
    if (!o) {
        return;
    }
    memset(o, 0, sizeof(*o));
    o->bed_leveling = true;
    o->flow_cali = false;
    o->vibration_cali = false;
    o->layer_inspect = false;
    o->timelapse = false;
    o->use_ams = false;
    o->bed_type = "auto";
    o->ams_mapping[0] = 0;
    o->ams_mapping_count = 1;
}

static esp_err_t pub_print(cJSON *print)
{
    return bambu_mqtt_publish_section("print", print);
}

static esp_err_t pub_system(cJSON *sys)
{
    return bambu_mqtt_publish_section("system", sys);
}

static esp_err_t pub_info(cJSON *info)
{
    return bambu_mqtt_publish_section("info", info);
}

static esp_err_t pub_camera(cJSON *cam)
{
    return bambu_mqtt_publish_section("camera", cam);
}

esp_err_t bambu_studio_get_version(void)
{
    cJSON *info = cJSON_CreateObject();
    if (!info) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(info, "command", "get_version");
    return pub_info(info);
}

esp_err_t bambu_studio_get_access_code(void)
{
    cJSON *sys = cJSON_CreateObject();
    if (!sys) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(sys, "command", "get_access_code");
    return pub_system(sys);
}

esp_err_t bambu_studio_print_pause(void)
{
    return bambu_service_pause();
}

esp_err_t bambu_studio_print_resume(void)
{
    return bambu_service_resume();
}

esp_err_t bambu_studio_print_stop(void)
{
    return bambu_service_stop_print();
}

esp_err_t bambu_studio_print_speed_level(int level_1_to_4)
{
    return bambu_service_set_speed_level(level_1_to_4);
}

esp_err_t bambu_studio_skip_objects(const int *part_ids, size_t count)
{
    if (!part_ids || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "skip_objects");
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(part_ids[i]));
    }
    cJSON_AddItemToObject(print, "obj_list", arr);
    return pub_print(print);
}

esp_err_t bambu_studio_clean_print_error(const char *subtask_id, int print_error)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "clean_print_error");
    cJSON_AddStringToObject(print, "subtask_id", subtask_id ? subtask_id : "0");
    cJSON_AddNumberToObject(print, "print_error", print_error);
    return pub_print(print);
}

esp_err_t bambu_studio_stop_buzzer(void)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "buzzer_ctrl");
    cJSON_AddNumberToObject(print, "mode", 0);
    return pub_print(print);
}

esp_err_t bambu_studio_close_air_filter(void)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "close_air_filt");
    return pub_print(print);
}

esp_err_t bambu_studio_start_calibration(bool vibration, bool bed_leveling, bool xcam_cali,
                                         bool motor_noise, bool nozzle_cali, bool bed_cali,
                                         bool clumppos_cali)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    int opt = 0;
    if (clumppos_cali) {
        opt |= 1 << 6;
    }
    if (bed_cali) {
        opt |= 1 << 5;
    }
    if (nozzle_cali) {
        opt |= 1 << 4;
    }
    if (motor_noise) {
        opt |= 1 << 3;
    }
    if (vibration) {
        opt |= 1 << 2;
    }
    if (bed_leveling) {
        opt |= 1 << 1;
    }
    if (xcam_cali) {
        opt |= 1 << 0;
    }
    cJSON_AddStringToObject(print, "command", "calibration");
    cJSON_AddNumberToObject(print, "option", opt);
    return pub_print(print);
}

esp_err_t bambu_studio_print_option_auto_recovery(bool on)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "print_option");
    cJSON_AddNumberToObject(print, "option", on ? (1 << 0) : 0);
    cJSON_AddBoolToObject(print, "auto_recovery", on);
    return pub_print(print);
}

esp_err_t bambu_studio_print_option_ams_auto_refill(bool on)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "print_option");
    cJSON_AddBoolToObject(print, "auto_switch_filament", on);
    return pub_print(print);
}

esp_err_t bambu_studio_print_option_air_print_detect(bool on)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "print_option");
    cJSON_AddBoolToObject(print, "air_print_detect", on);
    return pub_print(print);
}

esp_err_t bambu_studio_ams_change_filament(bool load, int ams_id, int slot_id, int curr_temp,
                                           int tar_temp)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "ams_change_filament");
    cJSON_AddNumberToObject(print, "ams_id", ams_id);
    cJSON_AddNumberToObject(print, "curr_temp", curr_temp);
    cJSON_AddNumberToObject(print, "tar_temp", tar_temp);
    if (!load) {
        cJSON_AddNumberToObject(print, "target", 255);
        cJSON_AddNumberToObject(print, "slot_id", 255);
    } else {
        int tray_id = 0;
        if (ams_id >= 0 && ams_id < 16) {
            tray_id = ams_id * 4 + slot_id;
        }
        if (tray_id == 0) {
            cJSON_AddNumberToObject(print, "target", ams_id);
        } else {
            cJSON_AddNumberToObject(print, "target", tray_id);
        }
        cJSON_AddNumberToObject(print, "slot_id", slot_id);
    }
    ESP_LOGI(TAG, "ams_change_filament load=%d ams=%d slot=%d", load, ams_id, slot_id);
    return pub_print(print);
}

esp_err_t bambu_studio_ams_control(const char *action)
{
    if (!action) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "ams_control");
    cJSON_AddStringToObject(print, "param", action);
    return pub_print(print);
}

esp_err_t bambu_studio_ams_reset(void)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "ams_reset");
    return pub_print(print);
}

esp_err_t bambu_studio_ams_drying_stop(void)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "auto_stop_ams_dry");
    return pub_print(print);
}

esp_err_t bambu_studio_ams_refresh_rfid(int ams_id, int slot_id)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "ams_get_rfid");
    cJSON_AddNumberToObject(print, "ams_id", ams_id);
    cJSON_AddNumberToObject(print, "slot_id", slot_id);
    return pub_print(print);
}

esp_err_t bambu_studio_ams_select_tray_gcode(int tray_id)
{
    char line[32];
    snprintf(line, sizeof(line), "M620 P%d \n", tray_id);
    return bambu_service_send_gcode_line(line);
}

esp_err_t bambu_studio_set_bed_temp(int celsius)
{
    return bambu_service_set_bed_target_c(celsius);
}

esp_err_t bambu_studio_set_nozzle_temp(int extruder_index, int celsius)
{
    if (extruder_index != 0) {
        cJSON *print = cJSON_CreateObject();
        if (!print) {
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(print, "command", "set_nozzle_temp");
        cJSON_AddNumberToObject(print, "extruder_index", extruder_index);
        cJSON_AddNumberToObject(print, "target_temp", celsius);
        return pub_print(print);
    }
    return bambu_service_set_nozzle_target_c(celsius);
}

esp_err_t bambu_studio_refresh_nozzle(void)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "refresh_nozzle");
    return pub_print(print);
}

esp_err_t bambu_studio_set_extrusion_length(int extruder_index, int length_mm)
{
    if (extruder_index != 0) {
        cJSON *print = cJSON_CreateObject();
        if (!print) {
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(print, "command", "set_extrusion_length");
        cJSON_AddNumberToObject(print, "extruder_index", extruder_index);
        cJSON_AddNumberToObject(print, "length", length_mm);
        return pub_print(print);
    }
    return bambu_service_extrude_mm((float)length_mm);
}

cJSON *bambu_studio_gcode_payload(const char *param_path)
{
    if (!param_path || !param_path[0]) {
        return NULL;
    }
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return NULL;
    }
    cJSON_AddStringToObject(print, "command", "gcode_file");
    cJSON_AddStringToObject(print, "param", param_path);
    return print;
}

cJSON *bambu_studio_project_payload(const char *plate_param, const char *subtask_name,
                                    const char *file_url, const bambu_project_print_opts_t *opts)
{
    if (!plate_param || !subtask_name || !file_url) {
        return NULL;
    }
    bambu_project_print_opts_t def;
    if (!opts) {
        bambu_project_print_opts_defaults(&def);
        opts = &def;
    }
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return NULL;
    }
    cJSON_AddStringToObject(print, "command", "project_file");
    cJSON_AddStringToObject(print, "param", plate_param);
    cJSON_AddStringToObject(print, "project_id", "0");
    cJSON_AddStringToObject(print, "profile_id", "0");
    cJSON_AddStringToObject(print, "task_id", "0");
    cJSON_AddStringToObject(print, "subtask_id", "0");
    cJSON_AddStringToObject(print, "subtask_name", subtask_name);
    cJSON_AddStringToObject(print, "url", file_url);
    cJSON_AddBoolToObject(print, "timelapse", opts->timelapse);
    cJSON_AddStringToObject(print, "bed_type", opts->bed_type ? opts->bed_type : "auto");
    cJSON_AddBoolToObject(print, "bed_leveling", opts->bed_leveling);
    cJSON_AddBoolToObject(print, "flow_cali", opts->flow_cali);
    cJSON_AddBoolToObject(print, "vibration_cali", opts->vibration_cali);
    cJSON_AddBoolToObject(print, "layer_inspect", opts->layer_inspect);
    cJSON_AddBoolToObject(print, "use_ams", opts->use_ams);
    cJSON *map = cJSON_CreateArray();
    int n = opts->ams_mapping_count > 0 ? opts->ams_mapping_count : 1;
    if (n > 8) {
        n = 8;
    }
    for (int i = 0; i < n; i++) {
        cJSON_AddItemToArray(map, cJSON_CreateNumber(opts->ams_mapping[i]));
    }
    cJSON_AddItemToObject(print, "ams_mapping", map);
    return print;
}

esp_err_t bambu_studio_gcode_file(const char *param_path)
{
    if (!param_path || !param_path[0]) return ESP_ERR_INVALID_ARG;
    cJSON *print = bambu_studio_gcode_payload(param_path);
    return print ? pub_print(print) : ESP_ERR_NO_MEM;
}

esp_err_t bambu_studio_project_file(const char *plate_param, const char *subtask_name,
                                    const char *file_url, const bambu_project_print_opts_t *opts)
{
    if (!plate_param || !subtask_name || !file_url) return ESP_ERR_INVALID_ARG;
    cJSON *print = bambu_studio_project_payload(plate_param, subtask_name, file_url, opts);
    return print ? pub_print(print) : ESP_ERR_NO_MEM;
}

esp_err_t bambu_studio_ledctrl(const char *node, const char *mode)
{
    if (!node || !mode) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *sys = cJSON_CreateObject();
    if (!sys) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(sys, "command", "ledctrl");
    cJSON_AddStringToObject(sys, "led_node", node);
    cJSON_AddStringToObject(sys, "led_mode", mode);
    cJSON_AddNumberToObject(sys, "led_on_time", 0);
    cJSON_AddNumberToObject(sys, "led_off_time", 0);
    cJSON_AddNumberToObject(sys, "loop_times", 0);
    cJSON_AddNumberToObject(sys, "interval_time", 0);
    return pub_system(sys);
}

esp_err_t bambu_studio_camera_timelapse(bool enable)
{
    cJSON *cam = cJSON_CreateObject();
    if (!cam) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(cam, "command", "ipcam_timelapse");
    cJSON_AddStringToObject(cam, "control", enable ? "enable" : "disable");
    return pub_camera(cam);
}

esp_err_t bambu_studio_camera_record(bool enable)
{
    cJSON *cam = cJSON_CreateObject();
    if (!cam) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(cam, "command", "ipcam_record_set");
    cJSON_AddStringToObject(cam, "control", enable ? "enable" : "disable");
    return pub_camera(cam);
}

esp_err_t bambu_studio_camera_resolution(const char *resolution)
{
    if (!resolution) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *cam = cJSON_CreateObject();
    if (!cam) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(cam, "command", "ipcam_resolution_set");
    cJSON_AddStringToObject(cam, "resolution", resolution);
    return pub_camera(cam);
}
