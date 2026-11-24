/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - P4 硬件 JPEG 解码 + PPA 缩放至屏幕
 *
 * 未经授权禁止用于商业倒卖。本代码仅供酷世DIY开发板用户学习使用。
 */

#include "app_jpeg_dec.h"

#include <string.h>

#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ksdiy_lvgl_port.h"

static const char *TAG = "jpeg_dec";

size_t rgb_width;
size_t rgb_height;

#define JPEG_IN_BUF_SIZE       (128 * 1024)
/** 单边最大像素（支持 1280×800 横屏与 800×1280 竖屏） */
#define JPEG_MAX_DIM           1280
#define ALIGN_UP(num, align)   (((num) + ((align) - 1)) & ~((align) - 1))

static jpeg_decoder_handle_t s_jpeg;
static ppa_client_handle_t s_ppa;
static uint8_t *s_in_buf;
static uint32_t s_in_size;
static uint8_t *s_dec_buf;
static uint32_t s_dec_size;
static uint8_t *s_scaled_buf;
static uint32_t s_scaled_size;
static bool s_inited;

static const jpeg_decode_cfg_t s_dec_cfg = {
    .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
    .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
};

static uint8_t *dec_mem_alloc(uint32_t size, bool input, uint32_t *actual)
{
    jpeg_decode_memory_alloc_cfg_t cfg = {
        .buffer_direction = input ? JPEG_DEC_ALLOC_INPUT_BUFFER : JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    return (uint8_t *)jpeg_alloc_decoder_mem(size, &cfg, (size_t *)actual);
}

static esp_err_t ensure_dec_buffer(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0 || w > JPEG_MAX_DIM || h > JPEG_MAX_DIM) {
        ESP_LOGW(TAG, "JPEG 尺寸不支持 %lux%lu（单边最大 %d）",
                 (unsigned long)w, (unsigned long)h, JPEG_MAX_DIM);
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t req = w * h * 2; /* RGB565 输出 */
    if (s_dec_buf != NULL && req <= s_dec_size) {
        return ESP_OK;
    }

    if (s_dec_buf) {
        heap_caps_free(s_dec_buf);
        s_dec_buf = NULL;
        s_dec_size = 0;
    }

    s_dec_buf = dec_mem_alloc(req, false, &s_dec_size);
    if (s_dec_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "解码缓冲扩展: %lux%lu -> %lu KB",
             (unsigned long)w, (unsigned long)h, (unsigned long)(s_dec_size / 1024));
    return ESP_OK;
}

static esp_err_t scale_to_fit(const uint8_t *src, uint32_t src_w, uint32_t src_h,
                              uint32_t max_w, uint32_t max_h, bool allow_upscale,
                              uint8_t **out, uint32_t *out_w, uint32_t *out_h)
{
    float sx = (float)max_w / (float)src_w;
    float sy = (float)max_h / (float)src_h;
    float scale = (sx < sy) ? sx : sy;
    if (!allow_upscale && scale > 1.0f) {
        scale = 1.0f;   /* 只缩不放 */
    }

    uint32_t dst_w = (uint32_t)((float)src_w * scale);
    uint32_t dst_h = (uint32_t)((float)src_h * scale);
    if (dst_w == 0) {
        dst_w = 1;
    }
    if (dst_h == 0) {
        dst_h = 1;
    }

    if (dst_w == src_w && dst_h == src_h) {
        *out = (uint8_t *)src;
        *out_w = src_w;
        *out_h = src_h;
        return ESP_OK;
    }

    uint32_t out_bytes = dst_w * dst_h * 2;
    if (out_bytes > s_scaled_size) {
        ESP_LOGE(TAG, "缩放缓冲不足");
        return ESP_ERR_NO_MEM;
    }

    uint32_t pic_w = ALIGN_UP(src_w, 16);
    esp_err_t err = esp_cache_msync((void *)src, pic_w * src_h * 2,
                                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cache msync: %s", esp_err_to_name(err));
    }

    ppa_srm_oper_config_t srm = {
        .in = {
            .buffer = (void *)src,
            .pic_w = pic_w,
            .pic_h = src_h,
            .block_w = src_w,
            .block_h = src_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = s_scaled_buf,
            .buffer_size = s_scaled_size,
            .pic_w = dst_w,
            .pic_h = dst_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = scale,
        .scale_y = scale,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    err = ppa_do_scale_rotate_mirror(s_ppa, &srm);
    if (err != ESP_OK) {
        return err;
    }

    /* PPA 经 DMA 写 PSRAM，CPU 读取前须失效缓存 */
    esp_cache_msync(s_scaled_buf, ALIGN_UP(out_bytes, 64),
                    ESP_CACHE_MSYNC_FLAG_INVALIDATE | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

    *out = s_scaled_buf;
    *out_w = dst_w;
    *out_h = dst_h;
    return ESP_OK;
}

esp_err_t app_jpeg_dec_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    jpeg_decode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms = -1,
    };
    esp_err_t err = jpeg_new_decoder_engine(&engine_cfg, &s_jpeg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "jpeg engine: %s", esp_err_to_name(err));
        return err;
    }

    ppa_client_config_t ppa_cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    err = ppa_register_client(&ppa_cfg, &s_ppa);
    if (err != ESP_OK) {
        jpeg_del_decoder_engine(s_jpeg);
        s_jpeg = NULL;
        ESP_LOGE(TAG, "ppa client: %s", esp_err_to_name(err));
        return err;
    }

    s_in_buf = dec_mem_alloc(JPEG_IN_BUF_SIZE, true, &s_in_size);
    if (s_in_buf == NULL) {
        ppa_unregister_client(s_ppa);
        s_ppa = NULL;
        jpeg_del_decoder_engine(s_jpeg);
        s_jpeg = NULL;
        return ESP_ERR_NO_MEM;
    }

    uint32_t screen_need = KSDIY_LCD_H_RES * KSDIY_LCD_V_RES * 2;
    s_scaled_buf = (uint8_t *)heap_caps_aligned_alloc(128, screen_need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_scaled_buf == NULL) {
        s_scaled_buf = (uint8_t *)heap_caps_aligned_alloc(128, screen_need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_scaled_buf == NULL) {
        heap_caps_free(s_in_buf);
        s_in_buf = NULL;
        ppa_unregister_client(s_ppa);
        s_ppa = NULL;
        jpeg_del_decoder_engine(s_jpeg);
        s_jpeg = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_scaled_size = screen_need;

    /* dec 缓冲按首帧 JPEG 尺寸在 process_to 中按需扩展，避免启动即占满屏 */

    s_inited = true;
    ESP_LOGI(TAG, "HW JPEG+PPA: in=%lu KB scale=%lu KB (dec 按需)",
             (unsigned long)(s_in_size / 1024), (unsigned long)(s_scaled_size / 1024));
    return ESP_OK;
}

esp_err_t app_jpeg_dec_process(const uint8_t *input, size_t len, uint8_t **out_rgb565)
{
    uint32_t w = 0, h = 0;
    esp_err_t err = app_jpeg_dec_process_to(input, len, KSDIY_LCD_H_RES,
                                            KSDIY_LCD_V_RES, out_rgb565, &w, &h);
    return err;
}

esp_err_t app_jpeg_dec_process_to(const uint8_t *input, size_t len,
                                  uint32_t max_w, uint32_t max_h,
                                  uint8_t **out_rgb565, uint32_t *out_w, uint32_t *out_h)
{
    if (!s_inited || input == NULL || out_rgb565 == NULL || len == 0 ||
        out_w == NULL || out_h == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > s_in_size) {
        ESP_LOGW(TAG, "JPEG 帧过大: %u > %lu", (unsigned)len, (unsigned long)s_in_size);
        return ESP_ERR_INVALID_SIZE;
    }

    jpeg_decode_picture_info_t info;
    esp_err_t err = jpeg_decoder_get_info(input, len, &info);
    if (err != ESP_OK) {
        return err;
    }

    err = ensure_dec_buffer(info.width, info.height);
    if (err != ESP_OK) {
        return err;
    }

    memcpy(s_in_buf, input, len);
    uint32_t in_aligned = ALIGN_UP((uint32_t)len, 16);
    uint32_t ret_size = s_dec_size;

    err = jpeg_decoder_process(s_jpeg, &s_dec_cfg, s_in_buf, in_aligned,
                               s_dec_buf, s_dec_size, &ret_size);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t *display = NULL;
    uint32_t dw = 0;
    uint32_t dh = 0;
    /* 目标恰为整屏（开机动画铺满）时允许放大，画廊等局部显示只缩不放 */
    bool fill_screen = (max_w == KSDIY_LCD_H_RES && max_h == KSDIY_LCD_V_RES);
    err = scale_to_fit(s_dec_buf, info.width, info.height, max_w, max_h,
                       fill_screen, &display, &dw, &dh);
    if (err != ESP_OK) {
        return err;
    }

    rgb_width = dw;
    rgb_height = dh;
    *out_rgb565 = display;
    *out_w = dw;
    *out_h = dh;
    return ESP_OK;
}

esp_err_t app_jpeg_dec_deinit(void)
{
    if (s_dec_buf) {
        heap_caps_free(s_dec_buf);
        s_dec_buf = NULL;
        s_dec_size = 0;
    }
    if (s_scaled_buf) {
        heap_caps_free(s_scaled_buf);
        s_scaled_buf = NULL;
        s_scaled_size = 0;
    }
    if (s_in_buf) {
        heap_caps_free(s_in_buf);
        s_in_buf = NULL;
    }
    if (s_ppa) {
        ppa_unregister_client(s_ppa);
        s_ppa = NULL;
    }
    if (s_jpeg) {
        jpeg_del_decoder_engine(s_jpeg);
        s_jpeg = NULL;
    }
    s_inited = false;
    rgb_width = 0;
    rgb_height = 0;
    return ESP_OK;
}
