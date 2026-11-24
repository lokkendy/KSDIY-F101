#include "bambu_job_cover.h"
#include "ui_dispatch.h"
#include "bambu_cover_png.h"

#include "bambu_ftp.h"
#include "bambu_profile.h"
#include "bambu_service.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "bambu_cover";

#define COVER_BUF_SZ (192u * 1024u)

#ifndef BAMBU_FTP_TASK_STACK
#define BAMBU_FTP_TASK_STACK 8192
#endif

typedef struct {
    uint32_t gen;
    int plate_idx;
    char gcode_file[96];
    char task_name[96];
    uint32_t owner_id;
} cover_req_t;

typedef struct cover_done {
    uint32_t gen;
    char key[384];
    uint8_t *png; /* decoded BGRA pixels, never compressed PNG in LVGL */
    size_t len;
    lv_image_dsc_t dsc;
    uint32_t owner_id;
} cover_done_t;

struct bambu_job_cover {
    lv_obj_t *img;
    lv_obj_t *lbl_ph;
    cover_done_t *cover;
    char cover_key[384];
    bool cover_busy;
    char request_key[384];
    uint32_t retry_at;
    uint32_t busy_since;
    uint32_t cover_gen;
    uint32_t id;
    struct bambu_job_cover *next;
};

/* UI-thread registry: workers retain only an ID, including across theme rebuilds. */
static bambu_job_cover_t *s_live_covers;
static uint32_t s_next_cover_id;

static void cover_free_png(bambu_job_cover_t *c)
{
    if (!c || !c->cover) {
        return;
    }
    lv_image_set_src(c->img, NULL);
    lv_image_cache_drop(&c->cover->dsc);
    lv_image_header_cache_drop(&c->cover->dsc);
    free(c->cover->png);
    free(c->cover);
    c->cover = NULL;
}

static void cover_apply_scale(bambu_job_cover_t *c, lv_image_dsc_t *d, uint32_t w, uint32_t h)
{
    lv_obj_t *box = lv_obj_get_parent(c->img);
    if (!box) {
        box = c->img;
    }
    lv_obj_update_layout(box);
    int32_t bw = lv_obj_get_width(box);
    int32_t bh = lv_obj_get_height(box);
    enum { k_pad = 6 };
    int32_t inner_w = bw - 2 * k_pad;
    int32_t inner_h = bh - 2 * k_pad;
    if (inner_w < 8) {
        inner_w = 8;
    }
    if (inner_h < 8) {
        inner_h = 8;
    }
    int32_t inner = inner_w < inner_h ? inner_w : inner_h;
    uint32_t max_wh = w > h ? w : h;
    if (max_wh == 0) {
        max_wh = 1;
    }
    lv_image_set_src(c->img, d);
    lv_image_set_scale(c->img, (uint16_t)(inner * 256 / max_wh));
    lv_image_set_antialias(c->img, true);
    lv_obj_center(c->img);
    lv_obj_clear_flag(c->img, LV_OBJ_FLAG_HIDDEN);
    if (c->lbl_ph) {
        lv_obj_add_flag(c->lbl_ph, LV_OBJ_FLAG_HIDDEN);
    }
}

static void cover_ui_done(void *p)
{
    cover_done_t *done = p;
    if (!done) return;
    bambu_job_cover_t *c = s_live_covers;
    while (c && c->id != done->owner_id) c = c->next;
    if (!c || done->gen != c->cover_gen) {
        free(done->png);
        free(done);
        return;
    }
    c->cover_busy = false;
    if (!done->png) {
        c->retry_at = lv_tick_get() + 15000;
        free(done);
        return;
    }
    snprintf(c->cover_key, sizeof(c->cover_key), "%s", done->key);
    cover_free_png(c);
    c->cover = done;
    cover_apply_scale(c, &done->dsc, done->dsc.header.w, done->dsc.header.h);
}

static void cover_push_one(char cands[][BAMBU_FTP_NAME_MAX], int *nc, const char *name)
{
    if (*nc >= 4) {
        return;
    }
    for (int i = 0; i < *nc; i++) {
        if (strcmp(cands[i], name) == 0) {
            return;
        }
    }
    snprintf(cands[(*nc)++], BAMBU_FTP_NAME_MAX, "%s", name);
}

static bool ends_with_3mf(const char *s, size_t n)
{
    if (n < 5) {
        return false;
    }
    const char *e = s + n - 4;
    return e[0] == '.' &&
           (e[1] | 0x20) == '3' && (e[2] | 0x20) == 'm' && (e[3] | 0x20) == 'f';
}

static void cover_push_name(char cands[][BAMBU_FTP_NAME_MAX], int *nc, const char *raw)
{
    if (!raw) {
        return;
    }
    const char *s = raw;
    while (*s == '/') {
        s++;
    }
    if (strncmp(s, "data/", 5) == 0) {
        return;
    }
    if (strncmp(s, "sdcard/", 7) == 0) {
        s += 7;
    }
    size_t n = strlen(s);
    if (n == 0 || n >= BAMBU_FTP_NAME_MAX - 12) {
        return;
    }
    if (ends_with_3mf(s, n)) {
        cover_push_one(cands, nc, s);
        return;
    }
    char buf[BAMBU_FTP_NAME_MAX];
    snprintf(buf, sizeof(buf), "%s.3mf", s);
    cover_push_one(cands, nc, buf);
    snprintf(buf, sizeof(buf), "%s.gcode.3mf", s);
    cover_push_one(cands, nc, buf);
}

static void cover_worker(void *arg)
{
    cover_req_t *req = arg;
    cover_done_t *done = calloc(1, sizeof(*done));
    uint8_t *buf = heap_caps_malloc(COVER_BUF_SZ, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (done) done->owner_id = req->owner_id;
    if (done && buf) {
        bambu_lan_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        if (bambu_service_copy_lan_config(&cfg) != ESP_OK) {
            (void)bambu_profile_load(&cfg);
        }

        char cands[4][BAMBU_FTP_NAME_MAX];
        int nc = 0;
        cover_push_name(cands, &nc, req->task_name);
        cover_push_name(cands, &nc, req->gcode_file);

        static const char dirs[2][8] = { "cache", "" };
        for (int i = 0; i < nc && !done->png; i++) {
            for (int d = 0; d < 2 && !done->png; d++) {
                char rel[2 * BAMBU_FTP_NAME_MAX + 8];
                size_t pos = 0;
                if (dirs[d][0]) {
                    size_t dl = strlen(dirs[d]);
                    memcpy(rel, dirs[d], dl);
                    rel[dl] = '/';
                    pos = dl + 1;
                }
                size_t sl = strlen(cands[i]);
                if (sl > sizeof(rel) - pos - 1) {
                    sl = sizeof(rel) - pos - 1;
                }
                memcpy(rel + pos, cands[i], sl);
                rel[pos + sl] = 0;
                size_t len = 0;
                if (bambu_ftp_fetch_cover(&cfg, rel, req->plate_idx, buf, COVER_BUF_SZ,
                                          &len) == ESP_OK && len >= 32) {
                    uint8_t *exact = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (exact) {
                        memcpy(exact, buf, len);
                        done->png = exact;
                        done->len = len;
                    }
                }
            }
        }
    }
    /* Release the 192 KB network buffer before allocating the bounded decoder. */
    free(buf);
    buf = NULL;
    if (done && done->png) {
        bambu_cover_pixels_t pixels;
        int rc = bambu_cover_png_decode(done->png, done->len, 160, &pixels);
        free(done->png);
        done->png = NULL;
        if (rc == 0) {
            done->png = pixels.pixels;
            done->len = pixels.stride * pixels.height;
            done->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            done->dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
            done->dsc.header.w = pixels.width;
            done->dsc.header.h = pixels.height;
            done->dsc.header.stride = pixels.stride;
            done->dsc.data = pixels.pixels;
            done->dsc.data_size = done->len;
            ESP_LOGI(TAG, "thumbnail ready %ux%u BGRA (%u bytes)",
                     (unsigned)pixels.width, (unsigned)pixels.height, (unsigned)done->len);
        } else {
            ESP_LOGW(TAG, "thumbnail decode failed; will retry");
        }
    }
    if (done) {
        done->gen = req->gen;
        snprintf(done->key, sizeof(done->key), "%s|%s|%d", req->task_name, req->gcode_file,
                 req->plate_idx);
        if (!done->png) {
            ESP_LOGD(TAG, "cover miss plate=%d", req->plate_idx);
        }
        if (!ui_post(cover_ui_done, done)) {
            free(done->png);
            free(done);
        }
    }
    free(buf);
    free(req);
    vTaskDelete(NULL);
}

bambu_job_cover_t *bambu_job_cover_create(lv_obj_t *img, lv_obj_t *lbl_ph)
{
    if (!img) {
        return NULL;
    }
    bambu_job_cover_t *c = calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    c->id = ++s_next_cover_id;
    if (!c->id) c->id = ++s_next_cover_id;
    c->next = s_live_covers;
    s_live_covers = c;
    c->img = img;
    c->lbl_ph = lbl_ph;
    return c;
}

void bambu_job_cover_destroy(bambu_job_cover_t *c)
{
    if (!c) {
        return;
    }
    bambu_job_cover_t **link = &s_live_covers;
    while (*link && *link != c) link = &(*link)->next;
    if (*link) *link = c->next;
    bambu_job_cover_invalidate(c);
    cover_free_png(c);
    free(c);
}

void bambu_job_cover_invalidate(bambu_job_cover_t *c)
{
    if (!c) {
        return;
    }
    c->cover_gen++;
    c->cover_busy = false;
    c->cover_key[0] = 0;
    c->request_key[0] = 0;
    c->retry_at = 0;
}

void bambu_job_cover_show_placeholder(bambu_job_cover_t *c)
{
    if (!c || !c->img) {
        return;
    }
    lv_obj_add_flag(c->img, LV_OBJ_FLAG_HIDDEN);
    if (c->lbl_ph) {
        lv_obj_clear_flag(c->lbl_ph, LV_OBJ_FLAG_HIDDEN);
    }
}

void bambu_job_cover_maybe_fetch(bambu_job_cover_t *c, bool job_active,
                                 const char *task_name, const char *gcode_file,
                                 int plate_idx)
{
    if (!c || !c->img) {
        return;
    }
    if (!job_active || !task_name || !task_name[0] || strcmp(task_name, "—") == 0) {
        if (c->request_key[0]) {
            bambu_job_cover_invalidate(c);
            cover_free_png(c);
        }
        bambu_job_cover_show_placeholder(c);
        return;
    }
    const char *gcode = gcode_file ? gcode_file : "";
    char key[384];
    snprintf(key, sizeof(key), "%s|%s|%d", task_name, gcode, plate_idx);
    uint32_t now = lv_tick_get();
    if (strcmp(key, c->request_key) != 0) {
        bambu_job_cover_invalidate(c); /* old job/plate result must not attach */
        cover_free_png(c);
        bambu_job_cover_show_placeholder(c);
        snprintf(c->request_key, sizeof(c->request_key), "%s", key);
    }
    if (strcmp(key, c->cover_key) == 0) return;
    if (c->cover_busy && (uint32_t)(now - c->busy_since) < 45000) return;
    if (c->retry_at && (int32_t)(now - c->retry_at) < 0) return;
    c->cover_busy = false; /* also recovers a failed UI dispatch */
    c->retry_at = 0;
    cover_req_t *req = malloc(sizeof(*req));
    if (!req) {
        return;
    }
    req->owner_id = c->id;
    req->gen = ++c->cover_gen;
    req->plate_idx = plate_idx;
    snprintf(req->task_name, sizeof(req->task_name), "%s", task_name);
    snprintf(req->gcode_file, sizeof(req->gcode_file), "%s", gcode);
    c->cover_busy = true;
    c->busy_since = now;
    if (xTaskCreate(cover_worker, "bambu_cover", BAMBU_FTP_TASK_STACK, req, 5, NULL) != pdPASS) {
        c->cover_busy = false;
        free(req);
    }
}
