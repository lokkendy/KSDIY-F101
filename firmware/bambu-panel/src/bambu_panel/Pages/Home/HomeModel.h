#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bambu_model.h"

typedef enum {
    HOME_CARD_DISCONNECTED = 0,
    HOME_CARD_OK,
    HOME_CARD_WARN,
    HOME_CARD_ERROR,
} home_card_level_t;

typedef struct {
    home_card_level_t card_level;
    bool connected;
    bool cloud;
    bool has_job_detail;
    bool show_chamber;
    bambu_model_t model;   /* 已 coerce（UNKNOWN→P1P），首页照片按此选择 */
    char printer_name[48];
    char serial[41];
    char online_line[16];
    char idle_sub[32];
    char cloud_line[80];
    char state_text[96];
    char file_text[96];
    char gcode_file[96];
    int plate_idx;
    bool job_active;
    char progress_text[16];
    int progress_pct;
    char remain_text[48];
    char layer_text[32];
    char nozzle_val[12];
    char nozzle_tgt[24];
    char bed_val[12];
    char bed_tgt[24];
    char chamber_val[12];
    char chamber_tgt[24];
    /** 竖屏等简化布局仍用 */
    char identity_text[96];
} HomeModel_t;

void HomeModel_SetCloudPreferred(bool preferred);
void HomeModel_GetSnapshot(HomeModel_t *out);
