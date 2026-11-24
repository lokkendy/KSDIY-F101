#pragma once

#include "bambu_camera.h"
#include "lvgl.h"

/* The active page owns the single JPEG decoder and the displayed buffer. */
void bambu_preview_open(lv_obj_t *image, const bambu_lan_config_t *config);
void bambu_preview_close(lv_obj_t *image);
bool bambu_preview_update(lv_obj_t *image, bambu_cam_state_t *state,
                          char *message, size_t message_size);
