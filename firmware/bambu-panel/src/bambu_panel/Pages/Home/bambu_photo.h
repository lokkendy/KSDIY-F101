#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bambu_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 机型 → 首页展示照片（内置 PNG 字节，lodepng 解码渲染）。
 * 返回 false 表示该型号无内置照片（如 OTHER），调用方保留占位图标。
 * X1 与 X1C 外观一致，官网 X1 产品页同样复用 X1C 图，这里共用一份。
 */
bool bambu_photo_for_model(bambu_model_t model, const uint8_t **png, size_t *len);

#ifdef __cplusplus
}
#endif
