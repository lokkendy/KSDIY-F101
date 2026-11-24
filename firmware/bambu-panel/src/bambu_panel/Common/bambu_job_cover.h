/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * 3mf 内嵌 plate PNG 封面：FTP 后台拉取 + LVGL 异步显示（首页/相机页共用）
 */
#pragma once

#include "lvgl.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bambu_job_cover bambu_job_cover_t;

/** 绑定已有 img / 占位 label（label 可为 NULL） */
bambu_job_cover_t *bambu_job_cover_create(lv_obj_t *img, lv_obj_t *lbl_ph);
void bambu_job_cover_destroy(bambu_job_cover_t *c);

/** 页面销毁前调用：作废在途 worker */
void bambu_job_cover_invalidate(bambu_job_cover_t *c);

void bambu_job_cover_show_placeholder(bambu_job_cover_t *c);

void bambu_job_cover_maybe_fetch(bambu_job_cover_t *c, bool job_active,
                                 const char *task_name, const char *gcode_file,
                                 int plate_idx);

#ifdef __cplusplus
}
#endif
