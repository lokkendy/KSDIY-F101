#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char state_text[96];
    char mode_text[64];
    char file_caption[64];
    char file_name[96];
    char elapsed_text[16];
    char remaining_text[16];
    char nozzle_text[32];
    char bed_text[32];
    char chamber_text[32];
    char layer_line[64];
    int progress;          /* 0..100 */
    int progress_tenths;   /* 0..1000 for x.x% */
    bool show_chamber;
    bool can_control;
    bool progress_known;
    bool can_pause;
    bool can_resume;
    bool can_cancel;
    bool job_active;
    bool paused;
    bool printing;
    bool chamber_light_on;
    bool work_light_on;

    /* 模型封面拉取线索（断连分支恒为零） */
    char gcode_file[96];  /* gcode_file 原文（X1 为 /data/… ramdisk，仅作回退候选） */
    int plate_idx;        /* 0 未知，否则 1..N */
} JobModel_t;

void JobModel_GetSnapshot(JobModel_t *out, const char *cloud_serial);
