#pragma once

#include "bambu_service.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BAMBU_CAM_STOPPED = 0,
    BAMBU_CAM_CONNECTING,
    BAMBU_CAM_STREAMING,
    BAMBU_CAM_UNSUPPORTED,
    BAMBU_CAM_ERROR,
} bambu_cam_state_t;

void bambu_camera_init(void);
esp_err_t bambu_camera_start(const bambu_lan_config_t *cfg);
void bambu_camera_stop(void);

void bambu_camera_get_status(bambu_cam_state_t *state, char *msg, size_t msg_len);

/** 取最新 JPEG 帧（内部缓冲，下一帧前有效）；seq 变化表示新帧 */
bool bambu_camera_read_frame(uint8_t *jpeg,size_t capacity,size_t *len,uint32_t *seq);

bool bambu_camera_copy_frame(const uint8_t **jpeg, size_t *len, uint32_t *seq);

#ifdef __cplusplus
}
#endif
