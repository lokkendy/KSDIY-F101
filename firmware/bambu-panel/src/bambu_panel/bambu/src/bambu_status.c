#include "bambu_status.h"
#include "bambu_cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static cJSON *field(cJSON *obj, const char *name)
{
    return cJSON_IsObject(obj) ? cJSON_GetObjectItemCaseSensitive(obj, name) : NULL;
}

static bool merge_number(cJSON *obj, const char *name, float *value)
{
    cJSON *v = field(obj, name);
    if (!cJSON_IsNumber(v)) return false;
    *value = (float)v->valuedouble;
    return true;
}

static bool merge_int(cJSON *obj, const char *name, int *value)
{
    cJSON *v = field(obj, name);
    if (!cJSON_IsNumber(v)) return false;
    *value = (int)v->valuedouble;
    return true;
}

static bambu_print_state_t parse_state(const char *value)
{
    if (!value) return BAMBU_PRINT_UNKNOWN;
    if (strcmp(value, "RUNNING") == 0 || strcmp(value, "PRINTING") == 0) {
        return BAMBU_PRINT_RUNNING;
    }
    if (strcmp(value, "PAUSE") == 0 || strcmp(value, "PAUSING") == 0) {
        return BAMBU_PRINT_PAUSED;
    }
    if (strcmp(value, "PREPARE") == 0) return BAMBU_PRINT_PREPARE;
    if (strcmp(value, "FINISH") == 0) return BAMBU_PRINT_FINISHED;
    if (strcmp(value, "FAILED") == 0) return BAMBU_PRINT_FAILED;
    if (strcmp(value, "IDLE") == 0) return BAMBU_PRINT_IDLE;
    return BAMBU_PRINT_UNKNOWN;
}

static int fun_hex_bit(const char *fun,int start,int count)
{
    if(!fun || start<0 || count<1 || count>30)return 0;
    size_t n=strlen(fun);unsigned result=0;
    for(int bit=0;bit<count;++bit){
        size_t nibble=(unsigned)(start+bit)/4;
        if(nibble>=n)continue;
        unsigned char c=fun[n-1-nibble];
        int v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
        if(v<0)continue;
        if(v&(1u<<((start+bit)%4)))result|=1u<<bit;
    }
    return (int)result;
}

static bool json_int(cJSON *v,int *out)
{
    if(cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble>=-2147483647.0 && v->valuedouble<=2147483647.0){*out=(int)v->valuedouble;return true;}
    if(cJSON_IsString(v) && v->valuestring){
        char *end=NULL;long value=strtol(v->valuestring,&end,10);
        if(end!=v->valuestring && (!*end || strcmp(end,"dBm")==0) && value>=-2147483647L && value<=2147483647L){*out=(int)value;return true;}
    }
    return false;
}

static void merge_fun_caps(cJSON *print, bambu_status_t *status, bool *changed)
{
    cJSON *fun = field(print, "fun");
    if (!cJSON_IsString(fun) || !fun->valuestring || !fun->valuestring[0]) {
        return;
    }
    strncpy(status->fun_hex, fun->valuestring, sizeof(status->fun_hex) - 1);
    status->fun_hex[sizeof(status->fun_hex) - 1] = 0;
    status->cap_mqtt_homing = fun_hex_bit(status->fun_hex, 32, 1) != 0;
    status->cap_mqtt_axis = fun_hex_bit(status->fun_hex, 38, 1) != 0;
    status->cap_mqtt_bed_temp = fun_hex_bit(status->fun_hex, 39, 1) != 0;
    *changed = true;
}

void bambu_status_reset(bambu_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    status->state = BAMBU_PRINT_UNKNOWN;
    status->remaining_minutes = -1;
    status->ams_active_tray=-1;
    for(unsigned i=0;i<BAMBU_AMS_TRAYS;++i){status->ams_trays[i].unit_id=-1;status->ams_trays[i].slot_id=-1;status->ams_trays[i].remain_percent=-1;}
}

bool bambu_status_apply_json(bambu_status_t *status,
                             const char *json, size_t json_len)
{
    if (!status || !json || json_len == 0) return false;
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return false;
    cJSON *print = field(root, "print");
    if (!cJSON_IsObject(print)) {
        cJSON_Delete(root);
        return false;
    }

    bool changed = false;
    cJSON *v = field(print, "gcode_state");
    if (cJSON_IsString(v) && v->valuestring) {
        bambu_print_state_t next = parse_state(v->valuestring);
        if (next != BAMBU_PRINT_UNKNOWN) status->state = next;
        changed = true;
    }
    changed |= merge_number(print, "nozzle_temper", &status->nozzle_temp);
    changed |= merge_number(print, "nozzle_target_temper", &status->nozzle_target);
    changed |= merge_number(print, "bed_temper", &status->bed_temp);
    changed |= merge_number(print, "bed_target_temper", &status->bed_target);
    changed |= merge_number(print, "chamber_temper", &status->chamber_temp);
    changed |= merge_int(print, "mc_percent", &status->progress_percent);
    changed |= merge_int(print, "mc_remaining_time", &status->remaining_minutes);
    changed |= merge_int(print, "layer_num", &status->layer_current);
    changed |= merge_int(print, "total_layer_num", &status->layer_total);

    v = field(print, "subtask_name");
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(status->task_name, v->valuestring, sizeof(status->task_name) - 1);
        status->task_name[sizeof(status->task_name) - 1] = 0;
        changed = true;
    } else {
        v = field(print, "gcode_file");
        if (cJSON_IsString(v) && v->valuestring) {
            strncpy(status->task_name, v->valuestring, sizeof(status->task_name) - 1);
            status->task_name[sizeof(status->task_name) - 1] = 0;
            changed = true;
        }
    }

    /* gcode_file 原文单独留存（封面候选路径用），与 task_name 的优先级合并解耦 */
    v = field(print, "gcode_file");
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(status->gcode_file, v->valuestring, sizeof(status->gcode_file) - 1);
        status->gcode_file[sizeof(status->gcode_file) - 1] = 0;
        changed = true;
    }
    {
        int plate = 0;
        if (merge_int(print, "plate_idx", &plate) && plate > 0) {
            status->plate_idx = plate;
            changed = true;
        }
    }

    if (status->progress_percent < 0) status->progress_percent = 0;
    if (status->progress_percent > 100) status->progress_percent = 100;
    if (changed) status->has_data = true;
    cJSON_Delete(root);
    return changed;
}

static bool merge_fan_pct(cJSON *print,const char *name,int *pct)
{
    int raw;if(!json_int(field(print,name),&raw) || raw<0 || raw>15)return false;
    *pct=(raw*100+7)/15;return true;
}

static void merge_lights(cJSON *print, bambu_status_t *status, bool *changed)
{
    cJSON *arr = field(print, "lights_report");
    if (!cJSON_IsArray(arr)) {
        return;
    }
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsObject(it)) {
            continue;
        }
        cJSON *node = field(it, "node");
        cJSON *mode = field(it, "mode");
        if (!cJSON_IsString(node) || !cJSON_IsString(mode)) {
            continue;
        }
        bool on = strcmp(mode->valuestring, "on") == 0;
        if (strcmp(node->valuestring, "chamber_light") == 0) {
            status->chamber_light_on = on;
            *changed = true;
        } else if (strcmp(node->valuestring, "work_light") == 0) {
            status->work_light_on = on;
            *changed = true;
        }
    }
}

static void merge_hms(cJSON *print, bambu_status_t *status, bool *changed)
{
    cJSON *arr = field(print, "hms");
    if (!cJSON_IsArray(arr)) {
        return;
    }
    int n = cJSON_GetArraySize(arr);
    if (n <= 0) {
        status->hms_count = 0;
        status->hms_summary[0] = 0;
        *changed=true;
        return;
    }
    status->hms_count = n;
    cJSON *first = cJSON_GetArrayItem(arr, 0);
    if (cJSON_IsObject(first)) {
        cJSON *attr = field(first, "attr");
        cJSON *code = field(first, "code");
        unsigned a = cJSON_IsNumber(attr) ? (unsigned)attr->valuedouble : 0;
        unsigned c = cJSON_IsNumber(code) ? (unsigned)code->valuedouble : 0;
        snprintf(status->hms_summary, sizeof(status->hms_summary),
                 "HMS %u:%u%s", a, c, n > 1 ? " …" : "");
    } else {
        snprintf(status->hms_summary, sizeof(status->hms_summary), "HMS ×%d", n);
    }
    *changed = true;
}

static void merge_ams(cJSON *print,bambu_status_t *status,bool *changed)
{
    cJSON *ams=field(print,"ams");if(!cJSON_IsObject(ams))return;
    int now;
    if(json_int(field(ams,"tray_now"),&now)){status->ams_active_tray=now;*changed=true;}
    cJSON *units=field(ams,"ams");if(!cJSON_IsArray(units))return;
    if(!cJSON_GetArraySize(units)){
        memset(status->ams_trays,0,sizeof(status->ams_trays));
        for(unsigned i=0;i<BAMBU_AMS_TRAYS;++i){status->ams_trays[i].unit_id=-1;status->ams_trays[i].slot_id=-1;status->ams_trays[i].remain_percent=-1;}
        status->ams_installed=false;status->ams_tray_count=0;*changed=true;return;
    }
    status->ams_installed=true;
    cJSON *unit;
    cJSON_ArrayForEach(unit,units){
        int uid;if(!json_int(field(unit,"id"),&uid) || uid<0 || uid>15)continue;
        cJSON *trays=field(unit,"tray"),*tr;
        if(!cJSON_IsArray(trays))continue;
        cJSON_ArrayForEach(tr,trays){
            int sid;if(!json_int(field(tr,"id"),&sid) || sid<0 || sid>3)continue;
            bambu_ams_tray_t *target=NULL;
            for(unsigned i=0;i<BAMBU_AMS_TRAYS;++i)if(status->ams_trays[i].unit_id==uid && status->ams_trays[i].slot_id==sid){target=&status->ams_trays[i];break;}
            if(!target)for(unsigned i=0;i<BAMBU_AMS_TRAYS;++i)if(status->ams_trays[i].unit_id<0){target=&status->ams_trays[i];break;}
            if(!target)continue;
            target->unit_id=uid;target->slot_id=sid;snprintf(target->tray_id,sizeof(target->tray_id),"%d",sid);
            cJSON *type=field(tr,"tray_type"),*color=field(tr,"tray_color");
            if(cJSON_IsString(type) && type->valuestring){snprintf(target->type,sizeof(target->type),"%s",type->valuestring);target->present=type->valuestring[0]!=0;}
            if(cJSON_IsString(color) && color->valuestring)snprintf(target->color_hex,sizeof(target->color_hex),"%s",color->valuestring);
            int remain;if(json_int(field(tr,"remain"),&remain))target->remain_percent=remain>=0&&remain<=100?remain:-1;
        }
    }
    status->ams_tray_count=0;
    for(unsigned i=0;i<BAMBU_AMS_TRAYS;++i)if(status->ams_trays[i].present)++status->ams_tray_count;
    *changed=true;
}

bool bambu_status_apply_report_json(bambu_status_t *status,
                                    const char *json, size_t json_len)
{
    if (!status || !json || json_len == 0) {
        return false;
    }

    bool changed = bambu_status_apply_json(status, json, json_len);

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return changed;
    }

    cJSON *print = field(root, "print");
    if (cJSON_IsObject(print)) {
        merge_fun_caps(print, status, &changed);
        changed |= merge_int(print, "spd_lvl", &status->speed_level);
        changed |= merge_int(print, "spd_mag", &status->speed_mag);
        changed |= json_int(field(print,"wifi_signal"),&status->wifi_signal);
        cJSON *pe = field(print, "print_error");
        if (cJSON_IsNumber(pe)) {
            status->print_error = (uint32_t)pe->valuedouble;
            changed = true;
        }
        changed |= merge_fan_pct(print, "cooling_fan_speed", &status->cooling_fan_pct);
        changed |= merge_fan_pct(print, "heatbreak_fan_speed", &status->heatbreak_fan_pct);
        changed |= merge_fan_pct(print, "big_fan1_speed", &status->big_fan1_pct);
        changed |= merge_fan_pct(print, "big_fan2_speed", &status->big_fan2_pct);
        merge_lights(print, status, &changed);
        merge_hms(print, status, &changed);
        merge_ams(print, status, &changed);
    }

    bool printer_changed=changed;
    cJSON *info = field(root, "info");
    if (cJSON_IsObject(info)) {
        cJSON *modules=field(info,"module"),*module;
        cJSON_ArrayForEach(module,modules){
            cJSON *name=field(module,"name"),*version=field(module,"sw_ver"),*product=field(module,"product_name");
            if(cJSON_IsString(name) && name->valuestring && strcmp(name->valuestring,"ota")==0){
                if(cJSON_IsString(version) && version->valuestring){snprintf(status->fw_version,sizeof(status->fw_version),"%s",version->valuestring);changed=true;}
                if(cJSON_IsString(product) && product->valuestring){snprintf(status->fw_machine,sizeof(status->fw_machine),"%s",product->valuestring);changed=true;}
            }
        }
        cJSON *ver = field(info, "version");
        if (cJSON_IsString(ver) && ver->valuestring) {
            strncpy(status->fw_version, ver->valuestring, sizeof(status->fw_version) - 1);
            changed = true;
        }
        cJSON *mach = field(info, "machine");
        if (cJSON_IsString(mach) && mach->valuestring) {
            strncpy(status->fw_machine, mach->valuestring, sizeof(status->fw_machine) - 1);
            changed = true;
        }
    }

    if (printer_changed) status->has_data=true;
    cJSON_Delete(root);
    return changed;
}
