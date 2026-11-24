#include "bambu_preview.h"

#include "app_jpeg_dec.h"
#include "bambu_cloud.h"
#include "bambu_profile.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    lv_obj_t *image;
    lv_image_dsc_t descriptor;
    bambu_lan_config_t config;
    uint32_t sequence;
    bool explicit_config;
    uint32_t token;
    bool out_of_memory;
} preview_t;

typedef struct {
    uint32_t generation;
    bambu_lan_config_t config;
    bool start;
    bool explicit_config;
} preview_request_t;

static preview_t preview;
static TaskHandle_t worker_handle;
static preview_request_t pending;
static uint32_t completed;
static bool stream_started;
static bool decoder_ready;
static uint8_t *snapshot;
static lv_timer_t *release_timer;

enum { PREVIEW_HANDOFF_MS = 800 };

static void preview_worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        taskENTER_CRITICAL();
        preview_request_t request = pending;
        const bool handled = completed == request.generation;
        memset(&pending.config, 0, sizeof(pending.config));
        taskEXIT_CRITICAL();
        if (handled) continue;

        bool started = false;
        if (request.start) {
            bambu_lan_config_t *cfg = &request.config;
            if (!request.explicit_config && !bambu_cloud_copy_media(cfg)) {
                bambu_profile_load(cfg);
            }
            /* start() rejects these profiles before it would stop the old stream. */
            if (!cfg->printer_ip[0] || !cfg->access_code[0] ||
                !bambu_model_camera_tcp_jpeg(cfg->model)) {
                bambu_camera_stop();
            }
            started = bambu_camera_start(cfg) == ESP_OK;
        } else {
            bambu_camera_stop();
        }
        memset(&request.config, 0, sizeof(request.config));
        taskENTER_CRITICAL();
        completed = request.generation;
        stream_started = started;
        taskEXIT_CRITICAL();
    }
}

static uint32_t queue_operation(bool start)
{
    if (!worker_handle &&
        xTaskCreate(preview_worker, "bambu_preview", 6144, NULL, 3,
                    &worker_handle) != pdPASS) return 0;

    taskENTER_CRITICAL();
    /* Zero represents a request that could not be started. */
    if (++pending.generation == 0) ++pending.generation;
    uint32_t token = pending.generation;
    pending.start = start;
    pending.explicit_config = start && preview.explicit_config;
    pending.config = preview.config;
    taskEXIT_CRITICAL();

    xTaskNotifyGive(worker_handle);
    return token;
}

static void release_image(void)
{
    if (!preview.image) return;

    lv_obj_add_flag(preview.image, LV_OBJ_FLAG_HIDDEN);
    if (preview.descriptor.data) {
        lv_image_set_src(preview.image, NULL);
        lv_image_cache_drop(&preview.descriptor);
        lv_image_header_cache_drop(&preview.descriptor);
    }
    memset(&preview, 0, sizeof(preview));
}

static void release_buffers(lv_timer_t *timer)
{
    if (timer) lv_timer_delete(timer);
    release_timer = NULL;

    if (decoder_ready) app_jpeg_dec_deinit();
    decoder_ready = false;
    free(snapshot);
    snapshot = NULL;
}

static bool same_config(const bambu_lan_config_t *config)
{
    if (!config) return !preview.explicit_config;
    return preview.explicit_config &&
           preview.config.model == config->model &&
           preview.config.verify_printer == config->verify_printer &&
           !strcmp(preview.config.printer_ip, config->printer_ip) &&
           !strcmp(preview.config.serial, config->serial) &&
           !strcmp(preview.config.access_code, config->access_code);
}

void bambu_preview_open(lv_obj_t *image, const bambu_lan_config_t *config)
{
    if (!image || (preview.image == image && same_config(config))) return;

    if (release_timer) {
        lv_timer_delete(release_timer);
        release_timer = NULL;
    }
    release_image();
    preview.image = image;
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    preview.explicit_config = config != NULL;
    if (config) preview.config = *config;
    preview.token = queue_operation(true);
}

void bambu_preview_close(lv_obj_t *image)
{
    if (!image || image != preview.image) return;

    release_image();
    queue_operation(false);

    /* Keep the large contiguous DMA allocation across an immediate page handoff. */
    if (snapshot || decoder_ready) {
        release_timer = lv_timer_create(release_buffers, PREVIEW_HANDOFF_MS, NULL);
        if (!release_timer) release_buffers(NULL);
    }
}

static void read_frame(void)
{
    if (!snapshot) snapshot = malloc(CONFIG_BAMBU_CAMERA_FRAME_MAX);
    if (!snapshot) {
        preview.out_of_memory = true;
        return;
    }

    size_t length = 0;
    if (!bambu_camera_read_frame(snapshot, CONFIG_BAMBU_CAMERA_FRAME_MAX,
                                 &length, &preview.sequence)) return;
    if (!decoder_ready) {
        if (app_jpeg_dec_init() != ESP_OK) return;
        decoder_ready = true;
    }

    uint8_t *rgb = NULL;
    uint32_t width = 0;
    uint32_t height = 0;
    esp_err_t result = app_jpeg_dec_process_to(snapshot, length, 480, 270, &rgb,
                                              &width, &height);
    preview.out_of_memory = result == ESP_ERR_NO_MEM;
    if (result != ESP_OK || !rgb) return;

    lv_image_dsc_t *d = &preview.descriptor;
    lv_image_cache_drop(d);
    lv_image_header_cache_drop(d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = (uint16_t)width;
    d->header.h = (uint16_t)height;
    d->header.stride = width * 2;
    d->data = rgb;
    d->data_size = width * height * 2;
    lv_image_set_src(preview.image, d);
    lv_obj_invalidate(preview.image);
}

bool bambu_preview_update(lv_obj_t *image, bambu_cam_state_t *state,
                          char *message, size_t message_size)
{
    *state = BAMBU_CAM_STOPPED;
    strlcpy(message, "未开启", message_size);
    if (!image || image != preview.image) return false;

    taskENTER_CRITICAL();
    const bool waiting = preview.token && completed != preview.token;
    const bool started = preview.token && stream_started;
    taskEXIT_CRITICAL();
    if (waiting) {
        *state = BAMBU_CAM_CONNECTING;
        strlcpy(message, "正在连接摄像头…", message_size);
        return false;
    }

    bambu_camera_get_status(state, message, message_size);
    if (!started && *state != BAMBU_CAM_UNSUPPORTED) {
        *state = BAMBU_CAM_ERROR;
        strlcpy(message, "摄像头连接失败", message_size);
    }
    if (started && *state == BAMBU_CAM_STREAMING) read_frame();

    bool visible = *state == BAMBU_CAM_STREAMING && preview.descriptor.data;
    if (!visible && *state == BAMBU_CAM_STREAMING) {
        strlcpy(message, preview.out_of_memory ? "相机内存不足" : "等待相机画面",
                message_size);
    }
    if (visible != !lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_flag(image, LV_OBJ_FLAG_HIDDEN, !visible);
    }
    return visible;
}
