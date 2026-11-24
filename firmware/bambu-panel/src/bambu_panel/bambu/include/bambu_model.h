#pragma once
#include <stdbool.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BAMBU_MODEL_UNKNOWN = 0,
    BAMBU_MODEL_P1P,
    BAMBU_MODEL_P1S,
    BAMBU_MODEL_X1,
    BAMBU_MODEL_X1C,
    BAMBU_MODEL_X1E,
    BAMBU_MODEL_A1,
    BAMBU_MODEL_A1_MINI,
    BAMBU_MODEL_H2S,
    BAMBU_MODEL_H2C,
    BAMBU_MODEL_H2D,
    BAMBU_MODEL_OTHER,
    BAMBU_MODEL_COUNT,
} bambu_model_t;

/** LVGL dropdown 选项（换行分隔，静态存储） */
const char *bambu_model_dropdown_text(void);

size_t bambu_model_ui_count(void);
bambu_model_t bambu_model_from_ui_index(unsigned index);
unsigned bambu_model_to_ui_index(bambu_model_t model);

const char *bambu_model_short_name(bambu_model_t model);
const char *bambu_model_lan_hint(bambu_model_t model);
uint32_t bambu_model_pushall_min_ms(bambu_model_t model);
bool bambu_model_supports_chamber(bambu_model_t model);

/** P1/A1 等：LAN TLS TCP 6000 推送 JPEG（非 RTSP） */
bool bambu_model_camera_tcp_jpeg(bambu_model_t model);

bambu_model_t bambu_model_from_nvs(uint8_t stored);
uint8_t bambu_model_to_nvs(bambu_model_t model);

/** SSDP DevModel.bambu.com / 旧版 3DPrinter-* 字符串 → 内部机型 */
bambu_model_t bambu_model_from_devmodel(const char *dev_model);

#ifdef __cplusplus
}
#endif
