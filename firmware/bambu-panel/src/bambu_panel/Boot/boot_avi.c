/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - 开机 AVI 开机动画
 *
 * MJPEG 硬解 + LVGL 全屏 lv_img。动画文件默认 /storage/boot.avi
 * (SPIFFS, 与字体同分区), 缺失自动跳过；Kconfig 可关掉整体功能与音轨。
 */
#include "boot_avi.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_APP_BOOT_AVI_ENABLE

#include "avi_player.h"
#include "app_jpeg_dec.h"
#include "driver/i2c.h"
#include "esp_codec_dev.h"
#include "ksdiy_lvgl_port.h"
#include "ksdiy_p4c5_audio.h"
#include "lvgl.h"

static const char *TAG = "boot_avi";

static volatile bool s_play_done;
static lv_obj_t *s_video_img;
static uint8_t *s_frame_buf;
static uint32_t s_frame_w;
static uint32_t s_frame_h;
static lv_image_dsc_t s_video_dsc;

static void boot_avi_end_cb(void *arg)
{
    (void)arg;
    s_play_done = true;
}

static esp_err_t render_frame(const uint8_t *rgb565, uint32_t w, uint32_t h)
{
    if (!rgb565 || w == 0 || h == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t frame_size = w * h * 2U;
    if (s_frame_w != w || s_frame_h != h || s_frame_buf == NULL) {
        if (s_frame_buf) {
            heap_caps_free(s_frame_buf);
            s_frame_buf = NULL;
        }
        s_frame_buf = (uint8_t *)heap_caps_aligned_alloc(128, frame_size,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_frame_buf) {
            return ESP_ERR_NO_MEM;
        }
        s_frame_w = w;
        s_frame_h = h;
        memset(&s_video_dsc, 0, sizeof(s_video_dsc));
        s_video_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_video_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_video_dsc.header.w = w;
        s_video_dsc.header.h = h;
        s_video_dsc.header.stride = w * 2U;
        s_video_dsc.data_size = frame_size;
        s_video_dsc.data = s_frame_buf;
    }

    memcpy(s_frame_buf, rgb565, frame_size);

    if (!ksdiy_lvgl_lock(50)) {
        return ESP_ERR_TIMEOUT;
    }
    lv_img_set_src(s_video_img, &s_video_dsc);
    lv_obj_set_size(s_video_img, (lv_coord_t)w, (lv_coord_t)h);
    lv_obj_center(s_video_img);
    lv_obj_invalidate(s_video_img);
    ksdiy_lvgl_unlock();
    return ESP_OK;
}

static void on_video_frame(frame_data_t *data, void *arg)
{
    (void)arg;
    if (!data || !s_video_img) {
        return;
    }
    uint8_t *rgb565 = NULL;
    if (app_jpeg_dec_process(data->data, data->data_bytes, &rgb565) != ESP_OK || !rgb565) {
        return;
    }
    (void)render_frame(rgb565, (uint32_t)rgb_width, (uint32_t)rgb_height);
}

#if CONFIG_APP_BOOT_AVI_AUDIO
static void on_audio_frame(frame_data_t *data, void *arg)
{
    (void)arg;
    if (output_dev_ == NULL || data == NULL || data->data_bytes == 0) {
        return;
    }
    (void)esp_codec_dev_write(output_dev_, data->data, data->data_bytes);
}

static void on_audio_set_clock(uint32_t rate, uint32_t bits_cfg, uint32_t ch, void *arg)
{
    (void)arg;
    if (rate == 0 || bits_cfg == 0 || ch == 0 || output_dev_ == NULL) {
        return;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = (uint8_t)bits_cfg,
        .channel = (uint8_t)ch,
        .sample_rate = rate,
    };
    (void)esp_codec_dev_close(output_dev_);
    if (esp_codec_dev_open(output_dev_, &fs) == ESP_OK) {
        (void)esp_codec_dev_set_out_vol(output_dev_, 80);
    }
}
#endif

static void boot_avi_ui_create(void)
{
    ksdiy_lvgl_lock(-1);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    s_video_img = lv_img_create(scr);
    lv_obj_center(s_video_img);
    ksdiy_lvgl_unlock();
}

static void boot_avi_ui_destroy(void)
{
    ksdiy_lvgl_lock(-1);
    if (s_video_img) {
        lv_obj_delete(s_video_img);
        s_video_img = NULL;
    }
    lv_obj_clean(lv_scr_act());
    ksdiy_lvgl_unlock();

    if (s_frame_buf) {
        heap_caps_free(s_frame_buf);
        s_frame_buf = NULL;
    }
    s_frame_w = s_frame_h = 0;
}

esp_err_t boot_avi_play(const char *path)
{
    if (!path || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        ESP_LOGW(TAG, "boot avi not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "playing boot avi: %s (%ld bytes)", path, (long)st.st_size);

    esp_err_t err = app_jpeg_dec_init();
    if (err != ESP_OK) {
        return err;
    }

#if CONFIG_APP_BOOT_AVI_AUDIO
    Codec_I2S_init(touch_i2c_bus_, I2C_NUM_0);
#endif

    boot_avi_ui_create();

    avi_player_config_t avi_cfg = {
        .buffer_size = 128 * 1024,
        .video_cb = on_video_frame,
#if CONFIG_APP_BOOT_AVI_AUDIO
        .audio_cb = on_audio_frame,
        .audio_set_clock_cb = on_audio_set_clock,
#else
        .audio_cb = NULL,
        .audio_set_clock_cb = NULL,
#endif
        .avi_play_end_cb = boot_avi_end_cb,
        .priority = 5,
        .coreID = 0,
        .user_data = NULL,
    };

    err = avi_player_init(avi_cfg);
    if (err != ESP_OK) {
        boot_avi_ui_destroy();
        app_jpeg_dec_deinit();
        return err;
    }

    s_play_done = false;
    err = avi_player_play_from_file(path);
    if (err != ESP_OK) {
        avi_player_deinit();
        boot_avi_ui_destroy();
        app_jpeg_dec_deinit();
        return err;
    }

    while (!s_play_done) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    avi_player_play_stop();
    vTaskDelay(pdMS_TO_TICKS(50));
    avi_player_deinit();
    boot_avi_ui_destroy();
    app_jpeg_dec_deinit();

    ESP_LOGI(TAG, "boot avi finished");
    return ESP_OK;
}

#else /* !CONFIG_APP_BOOT_AVI_ENABLE */

esp_err_t boot_avi_play(const char *path)
{
    (void)path;
    return ESP_OK;
}

#endif
