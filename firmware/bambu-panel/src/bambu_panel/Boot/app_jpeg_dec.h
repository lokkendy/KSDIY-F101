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
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 最近一次 app_jpeg_dec_process 输出图像的尺寸（像素） */
extern size_t rgb_width;
extern size_t rgb_height;

esp_err_t app_jpeg_dec_init(void);
esp_err_t app_jpeg_dec_deinit(void);

/** 解码一帧 JPEG 并缩放到屏幕尺寸，输出 RGB565
 *  @param input     JPEG 码流（任意对齐，内部拷入 DMA 缓冲）
 *  @param len       码流字节数
 *  @param out_rgb565 输出缓冲指针（内部静态缓冲，下一帧前有效）
 */
esp_err_t app_jpeg_dec_process(const uint8_t *input, size_t len, uint8_t **out_rgb565);

/** 解码一帧 JPEG 并缩放到不超过 max_w x max_h 的区域（只缩不放）
 *  @note 输出缓冲为内部静态缓冲，下一帧解码前有效；DMA 写入后已做缓存失效
 */
esp_err_t app_jpeg_dec_process_to(const uint8_t *input, size_t len,
                                  uint32_t max_w, uint32_t max_h,
                                  uint8_t **out_rgb565, uint32_t *out_w, uint32_t *out_h);

#ifdef __cplusplus
}
#endif
