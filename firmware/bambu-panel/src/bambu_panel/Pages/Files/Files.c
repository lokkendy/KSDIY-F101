#include "Files.h"

#include "bambu_ftp.h"
#include "bambu_cloud.h"
#include "bambu_profile.h"
#include "bambu_service.h"
#include "i18n.h"
#include "page_manager.h"
#include "ui_bambu.h"
#include "ui_dispatch.h"
#include "ui_modal.h"
#include "ui_screen.h"

#include "../Print/Print.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_tls_errors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    FILES_SRC_CACHE = 0,
    FILES_SRC_SDCARD,
    FILES_SRC_TFCARD,
} files_src_t;

typedef struct {
    bambu_ftp_entry_t entries[CONFIG_BAMBU_FTP_LIST_MAX];
    size_t count;
    esp_err_t err;
    files_src_t src;
    bool read_only;
    char cloud_serial[41];
    uint32_t gen;
    char directory[BAMBU_FTP_NAME_MAX];
    char printer_serial[sizeof(((bambu_lan_config_t *)0)->serial)];
} files_load_result_t;

typedef struct {
    uint32_t gen;
    bambu_ack_state_t ack;
    bool send_fail;
    char result[24];
    char name[BAMBU_FTP_NAME_MAX];
    uint32_t printer_generation;
} files_start_ctx_t;

static files_start_ctx_t s_confirm_print;
static char s_confirm_print_body[BAMBU_FTP_NAME_MAX + 256];

typedef struct {
    uint32_t gen;
    esp_err_t err;
    char name[BAMBU_FTP_NAME_MAX];
} files_del_ctx_t;

typedef struct {
    bambu_lan_config_t cfg;
    char local[BAMBU_FTP_NAME_MAX],remote[BAMBU_FTP_NAME_MAX];
    volatile bool cancel,done;
    unsigned refs;
    uint32_t sent,total;
    esp_err_t err;
} files_upload_ctx_t;

static const char *TAG = "files";

typedef struct {
    lv_obj_t *list;
    lv_obj_t *lbl_path;
    lv_obj_t *lbl_storage;
    lv_obj_t *lbl_detail_name;
    lv_obj_t *lbl_detail_meta;
    lv_obj_t *preview;
    lv_obj_t *img_preview;      /* 文件封面（.3mf 内嵌 plate PNG，选中即拉） */
    lv_obj_t *lbl_preview_ph;   /* 非 3mf / 拉取失败的占位图标 */
    lv_obj_t *storage_btns[3];
    lv_obj_t *btn_refresh;
    lv_obj_t *btn_parent;
    lv_obj_t *btn_start_print;
    lv_obj_t *btn_delete_file;
    files_src_t src;
    bool read_only;
    char cloud_serial[41];
    char directory[BAMBU_FTP_NAME_MAX];
    int selected;
    uint32_t load_gen;
    uint32_t op_gen;
    bool load_busy;
    bool start_busy;
    bool del_busy;
    files_load_result_t *pending;
    files_upload_ctx_t *upload;
    lv_timer_t *upload_timer;
    /* 详情封面状态：cover_png 为当前显示（换选释放），cover_name 记账防重拉 */
    lv_image_dsc_t cover_dsc;
    uint8_t *cover_png;
    size_t cover_len;
    char cover_name[BAMBU_FTP_NAME_MAX];
    bool cover_busy;
} files_ui_t;

static files_ui_t s_files;
static uint32_t s_files_generation;
static bool s_files_alive;

static bambu_ftp_scope_t scope_for_src(files_src_t s)
{
    return (s == FILES_SRC_CACHE) ? BAMBU_FTP_SCOPE_CACHE : BAMBU_FTP_SCOPE_SDCARD;
}

static bool files_browses_directory(files_src_t src, bool read_only)
{
    return src == FILES_SRC_SDCARD || (src == FILES_SRC_CACHE && read_only);
}

static const char *path_title_for_src(files_src_t s)
{
    switch (s) {
    case FILES_SRC_CACHE:
        return tr("缓存 > 打印文件");
    case FILES_SRC_SDCARD:
        return tr("打印机 TF 卡");
    default:
        return tr("本机 TF卡 > /sdcard");
    }
}

static void storage_btn_set_selected(int idx)
{
    for (int i = 0; i < 3; i++) {
        if (!s_files.storage_btns[i]) {
            continue;
        }
        bool sel = (i == idx);
        lv_obj_set_style_border_width(s_files.storage_btns[i], sel ? 2 : 1, 0);
        lv_obj_set_style_border_color(s_files.storage_btns[i],
                                      sel ? UI_COLOR_ACCENT : UI_COLOR_BORDER, 0);
        lv_obj_set_style_shadow_width(s_files.storage_btns[i], sel ? 10 : 0, 0);
        lv_obj_set_style_shadow_opa(s_files.storage_btns[i], sel ? LV_OPA_40 : LV_OPA_TRANSP, 0);
        lv_obj_t *ic = lv_obj_get_child(s_files.storage_btns[i], 0);
        if (ic) {
            lv_obj_set_style_text_color(ic, sel ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SEC, 0);
        }
    }
}

static void list_clear(void)
{
    if (!s_files.list) {
        return;
    }
    lv_obj_clean(s_files.list);
}

/** 自适应尺寸文本：MB 一位小数 / KB 整数 / 未知返回 fallback */
static void fmt_size(uint32_t bytes, char *out, size_t cap, const char *fallback)
{
    if (bytes >= 1024UL * 1024UL) {
        snprintf(out, cap, "%u.%u MB", (unsigned)(bytes >> 20),
                 (unsigned)((bytes >> 10) & 0x3FF) * 10U / 1024U);
    } else if (bytes > 0) {
        snprintf(out, cap, "%u KB", (unsigned)(bytes >> 10));
    } else {
        snprintf(out, cap, "%s", fallback);
    }
}

/* ── 详情封面：选中 .3mf 即经 FTPS 拉内嵌 plate PNG（复用 Job 页同款流式解析） ── */

enum { k_files_cover_buf = 192u * 1024u, k_files_cover_pad = 6 };

typedef struct {
    uint32_t generation;
    uint32_t load_gen;
    bool read_only;
    char cloud_serial[41];
    char name[BAMBU_FTP_NAME_MAX];
    uint8_t *png;   /* NULL = 拉取失败（name 仍记账防风暴） */
    size_t len;
} files_cover_done_t;

/* 后缀 ".3mf" 忽略大小写（与 Job.c ends_with_3mf 同规则） */
static bool files_name_is_3mf(const char *s)
{
    size_t n = strlen(s);
    if (n < 5) {
        return false;
    }
    const char *e = s + n - 4;
    return e[0] == '.' &&
           (e[1] | 0x20) == '3' && (e[2] | 0x20) == 'm' && (e[3] | 0x20) == 'f';
}

static void files_cover_show_placeholder(void)
{
    if (s_files.img_preview) {
        lv_obj_add_flag(s_files.img_preview, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_files.lbl_preview_ph) {
        lv_obj_clear_flag(s_files.lbl_preview_ph, LV_OBJ_FLAG_HIDDEN);
    }
}

static const bambu_ftp_entry_t *files_current_entry(void)
{
    if (!s_files.pending || s_files.selected < 0 ||
        (size_t)s_files.selected >= s_files.pending->count) {
        return NULL;
    }
    return &s_files.pending->entries[s_files.selected];
}

static void files_cover_maybe_fetch(void);
static void files_request_reload(void);

static void update_file_actions(void)
{
    const bambu_ftp_entry_t *entry = files_current_entry();
    bool local = s_files.src == FILES_SRC_TFCARD;
    bool has_file = entry && !entry->directory;
    bool printable = has_file && (local || bambu_ftp_is_printable_path(entry->name));
    if (printable && (!local || !s_files.read_only)) {
        lv_obj_clear_flag(s_files.btn_start_print, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_files.btn_start_print, LV_OBJ_FLAG_HIDDEN);
    }
    if (!local && !s_files.read_only && has_file) {
        lv_obj_clear_flag(s_files.btn_delete_file, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_files.btn_delete_file, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *label = lv_obj_get_child(s_files.btn_start_print, 0);
    if (label) lv_label_set_text(label, tr(local ? "上传到打印机" : "开始打印"));
}

static void files_cover_ui_done(void *p)
{
    files_cover_done_t *done = p;
    if (!done) return;
    if (!s_files_alive || done->generation != s_files_generation ||
        done->load_gen != s_files.load_gen || done->read_only != s_files.read_only ||
        strcmp(done->cloud_serial, s_files.cloud_serial)) {
        free(done->png);
        free(done);
        return;
    }
    s_files.cover_busy = false;
    const bambu_ftp_entry_t *sel = files_current_entry();
    bool current = sel && strcmp(sel->name, done->name) == 0;
    if (current) {
        snprintf(s_files.cover_name, sizeof(s_files.cover_name), "%s", done->name);
    }
    if (!done->png) {
        free(done);
        if (current) {
            files_cover_show_placeholder();
        }
        if(s_files.src==FILES_SRC_TFCARD)files_cover_show_placeholder();
    else files_cover_maybe_fetch();   /* 拉取期间选中已变 → 按新选中补拉 */
        return;
    }
    if (!current) {
        free(done->png);
        free(done);
        if(s_files.src==FILES_SRC_TFCARD)files_cover_show_placeholder();
    else files_cover_maybe_fetch();
        return;
    }

    if (s_files.img_preview) lv_image_set_src(s_files.img_preview, NULL);
    lv_image_cache_drop(&s_files.cover_dsc);
    lv_image_header_cache_drop(&s_files.cover_dsc);
    free(s_files.cover_png);
    s_files.cover_png = done->png;
    s_files.cover_len = done->len;

    /* PNG IHDR 宽高（big-endian @16..23）；压缩源由 LodePNG 解码，不能标为原始像素 */
    const uint8_t *png = done->png;
    uint32_t w = ((uint32_t)png[16] << 24) | ((uint32_t)png[17] << 16) |
                 ((uint32_t)png[18] << 8) | (uint32_t)png[19];
    uint32_t h = ((uint32_t)png[20] << 24) | ((uint32_t)png[21] << 16) |
                 ((uint32_t)png[22] << 8) | (uint32_t)png[23];
    if (w == 0 || h == 0 || w > 4096 || h > 4096) {
        files_cover_show_placeholder();
        free(done);
        return;
    }
    lv_image_dsc_t *d = &s_files.cover_dsc;
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    d->header.w = (uint16_t)w;
    d->header.h = (uint16_t)h;
    d->header.stride = 0;
    d->data_size = (uint32_t)done->len;
    d->data = png;
    lv_image_set_src(s_files.img_preview, d);

    /* 等比缩进预览盒（竖屏固定高度 / 横屏 flex 撑满详情列） */
    lv_obj_update_layout(s_files.preview);
    int32_t bw = lv_obj_get_width(s_files.preview);
    int32_t bh = lv_obj_get_height(s_files.preview);
    int32_t inner_w = bw - 2 * k_files_cover_pad;
    int32_t inner_h = bh - 2 * k_files_cover_pad;
    if (inner_w < 8) {
        inner_w = 8;
    }
    if (inner_h < 8) {
        inner_h = 8;
    }
    int32_t inner = inner_w < inner_h ? inner_w : inner_h;
    uint32_t max_wh = w > h ? w : h;
    lv_image_set_scale(s_files.img_preview, (uint16_t)(inner * 256 / max_wh));
    lv_image_set_antialias(s_files.img_preview, true);
    lv_obj_center(s_files.img_preview);
    lv_obj_clear_flag(s_files.img_preview, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_files.lbl_preview_ph, LV_OBJ_FLAG_HIDDEN);
    free(done);
}

static void files_cover_worker(void *arg)
{
    files_cover_done_t *done = arg;
    bambu_lan_config_t cfg;
    const char *serial = done->read_only ? done->cloud_serial : NULL;
    bool available = ui_bambu_copy_media_config(serial, &cfg);
    uint8_t *buf = available ? heap_caps_malloc(k_files_cover_buf, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    size_t len = 0;
    if (buf && bambu_ftp_fetch_cover(&cfg, done->name, 0, buf, k_files_cover_buf, &len) == ESP_OK &&
        len >= 32) {
        uint8_t *exact = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (exact) {
            memcpy(exact, buf, len);
            done->png = exact;
            done->len = len;
        }
    } else {
        ESP_LOGD(TAG, "cover miss (非 3mf 或无内嵌 PNG 时常见)");
    }
    memset(&cfg, 0, sizeof(cfg));
    free(buf);
    if (!ui_post(files_cover_ui_done, done)) { free(done->png); free(done); }
    vTaskDelete(NULL);
}

static void files_cover_maybe_fetch(void)
{
    const bambu_ftp_entry_t *sel = files_current_entry();
    if (!s_files.img_preview || !sel) {
        return;
    }
    if (sel->directory || !files_name_is_3mf(sel->name)) {
        files_cover_show_placeholder();
        return;
    }
    if (strcmp(sel->name, s_files.cover_name) == 0 || s_files.cover_busy) {
        return;   /* 已显示 / 已尝试；在途拉取完成后若选中已变会按新选中重拉 */
    }
    files_cover_done_t *done = calloc(1, sizeof(*done));
    if (!done) {
        return;
    }
    done->generation = s_files_generation;
    done->load_gen = s_files.load_gen;
    done->read_only = s_files.read_only;
    strlcpy(done->cloud_serial, s_files.cloud_serial, sizeof(done->cloud_serial));
    snprintf(done->name, sizeof(done->name), "%s", sel->name);
    s_files.cover_busy = true;
    if (xTaskCreate(files_cover_worker, "bambu_fcover", BAMBU_FTP_TASK_STACK, done, 5, NULL) != pdPASS) {
        s_files.cover_busy = false;
        free(done);
    }
}

static void update_detail(int idx)
{
    if (!s_files.pending || idx < 0 || (size_t)idx >= s_files.pending->count) {
        return;
    }
    s_files.selected = idx;
    update_file_actions();
    const bambu_ftp_entry_t *e = &s_files.pending->entries[idx];
    lv_label_set_text(s_files.lbl_detail_name, e->name);
    char meta[48];
    if (e->directory) strlcpy(meta, tr("目录"), sizeof(meta));
    else fmt_size(e->size_bytes, meta, sizeof(meta),
                  s_files.src==FILES_SRC_TFCARD ? tr("来自本机 TF卡") : tr("来自打印机 FTPS"));
    lv_label_set_text(s_files.lbl_detail_meta, meta);
    if (!s_files.list) {
        return;
    }
    uint32_t n = lv_obj_get_child_count(s_files.list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_get_child(s_files.list, i);
        bool sel = (int)i == idx;
        lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
        lv_obj_set_style_border_color(row, sel ? UI_COLOR_ACCENT : UI_COLOR_BORDER, 0);
        lv_obj_set_style_shadow_width(row, sel ? 8 : 0, 0);
        lv_obj_set_style_shadow_opa(row, sel ? LV_OPA_30 : LV_OPA_TRANSP, 0);
    }
    if(s_files.src==FILES_SRC_TFCARD)files_cover_show_placeholder();
    else files_cover_maybe_fetch();
}

static void on_file_row(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED) {
        return;
    }
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (files_browses_directory(s_files.src, s_files.read_only) &&
        s_files.pending && idx >= 0 && (size_t)idx < s_files.pending->count &&
        s_files.pending->entries[idx].directory) {
        if (!s_files.load_busy && !s_files.start_busy && !s_files.del_busy) {
            strlcpy(s_files.directory, s_files.pending->entries[idx].name, sizeof(s_files.directory));
            files_request_reload();
        }
        return;
    }
    if (idx == s_files.selected) {
        return;
    }
    update_detail(idx);
}

static void update_path_label(void)
{
    const bool browse = files_browses_directory(s_files.src, s_files.read_only);
    if (browse) {
        char label[BAMBU_FTP_NAME_MAX + 32];
        snprintf(label, sizeof(label), "%s /%s", tr("打印机 TF 卡"), s_files.directory);
        lv_label_set_text(s_files.lbl_path, label);
    } else {
        lv_label_set_text(s_files.lbl_path, path_title_for_src(s_files.src));
    }
    bool can_go_up = browse && s_files.directory[0] &&
                     !(s_files.src == FILES_SRC_CACHE && !strcmp(s_files.directory, "cache"));
    if (can_go_up) lv_obj_clear_flag(s_files.btn_parent, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_files.btn_parent, LV_OBJ_FLAG_HIDDEN);
}

static void on_parent_directory(lv_event_t *e)
{
    (void)e;
    if (!files_browses_directory(s_files.src, s_files.read_only) ||
        s_files.load_busy || s_files.start_busy || s_files.del_busy || !s_files.directory[0] ||
        (s_files.src == FILES_SRC_CACHE && !strcmp(s_files.directory, "cache"))) return;
    char *slash = strrchr(s_files.directory, '/');
    if (slash) *slash = 0;
    else s_files.directory[0] = 0;
    files_request_reload();
}

static void apply_load_result(files_load_result_t *res)
{
    if (!res || !s_files_alive || res->gen != s_files.load_gen ||
        res->read_only != s_files.read_only || strcmp(res->cloud_serial, s_files.cloud_serial)) {
        free(res);
        return;
    }

    s_files.load_busy = false;
    ui_loading_hide();
    if (s_files.pending) {
        free(s_files.pending);
    }
    s_files.pending = res;

    list_clear();
    update_path_label();
    s_files.selected = -1;
    s_files.cover_name[0] = 0;
    update_file_actions();

    if (res->err != ESP_OK || res->count == 0) {
        char hint[64];
        if (res->err == ESP_ERR_INVALID_RESPONSE) {
            snprintf(hint, sizeof(hint), "%s", tr("访问码错误或 FTPS 拒绝"));
        } else if (res->err == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST) {
            snprintf(hint, sizeof(hint), "%s", tr("无法连接 FTPS :990"));
        } else if (res->err == ESP_ERR_NOT_FOUND || res->err == ESP_ERR_INVALID_STATE) {
            snprintf(hint, sizeof(hint), "%s", tr(res->read_only && res->src != FILES_SRC_TFCARD ?
                     "当前打印机暂未连到局域网" : "请先连接打印机"));
        } else {
            snprintf(hint, sizeof(hint), "%s", tr(res->err == ESP_OK ? "目录为空" : "读取目录失败"));
        }
        lv_label_set_text(s_files.lbl_storage, hint);
        if (res->err != ESP_OK && res->err != ESP_ERR_NOT_FOUND) {
            ui_toast(hint, UI_TOAST_ERROR);
        }
        lv_label_set_text(s_files.lbl_detail_name, "—");
        lv_label_set_text(s_files.lbl_detail_meta, "");
        files_cover_show_placeholder();
        s_files.cover_name[0] = 0;   /* 列表已空：清封面记账 */
        return;
    }

    char stat[48];
    bool browse = files_browses_directory(res->src, res->read_only);
    snprintf(stat, sizeof(stat), tr(browse ? "%u 项" : "%u 文件"), (unsigned)res->count);
    lv_label_set_text(s_files.lbl_storage, stat);

    for (size_t i = 0; i < res->count; i++) {
        char meta[24];
        const bambu_ftp_entry_t *entry = &res->entries[i];
        const char *name = entry->name;
        if (browse) {
            const char *slash = strrchr(name, '/');
            if (slash) name = slash + 1;
        }
        if (entry->directory) strlcpy(meta, tr("目录"), sizeof(meta));
        else fmt_size(entry->size_bytes, meta, sizeof(meta), "—");
        lv_obj_t *r = ui_bambu_file_row_rich(s_files.list, name, meta,
                                             res->src==FILES_SRC_TFCARD?tr("TF卡"):tr("打印机"), i == 0);
        lv_obj_add_event_cb(r, on_file_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(r, on_file_row, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    }
    update_detail(0);
}

static void load_ui_done(void *p)
{
    apply_load_result((files_load_result_t *)p);
}

/* A bounded root/subdirectory scan. Local files are never removed by the FTP delete button. */
static void scan_local(files_load_result_t *pack,const char *path,unsigned depth)
{
    DIR *dir=opendir(path);if(!dir){if(depth==0)pack->err=ESP_ERR_NOT_FOUND;return;}
    struct dirent *de;
    while(pack->count<CONFIG_BAMBU_FTP_LIST_MAX && (de=readdir(dir))){
        if(de->d_name[0]=='.')continue;
        char name[BAMBU_FTP_NAME_MAX];
        if(snprintf(name,sizeof(name),"%s/%s",path,de->d_name)>=(int)sizeof(name))continue;
        struct stat st;if(stat(name,&st))continue;
        if(S_ISDIR(st.st_mode)){if(depth<1)scan_local(pack,name,depth+1);continue;}
        const char *ext=strrchr(name,'.');
        if(!S_ISREG(st.st_mode)||!ext||(strcasecmp(ext,".3mf")&&strcasecmp(ext,".gcode")))continue;
        if(st.st_size<0 || (uint64_t)st.st_size>UINT32_MAX)continue;
        bambu_ftp_entry_t *entry=&pack->entries[pack->count++];
        snprintf(entry->name,sizeof(entry->name),"%s",name);entry->size_bytes=(uint32_t)st.st_size;
    }
    closedir(dir);
}

static void files_load_worker(void *arg)
{
    /* Request owns a snapshot; no worker reads a rebuilt page's globals. */
    files_load_result_t *pack = arg;
    if (pack->src == FILES_SRC_TFCARD) {
        pack->err = ESP_OK;
        scan_local(pack,"/sdcard",0);
        if (!ui_post(load_ui_done, pack)) free(pack);
        vTaskDelete(NULL);
        return;
    }

    bambu_lan_config_t cfg;
    const char *serial = pack->read_only ? pack->cloud_serial : NULL;
    if (!ui_bambu_copy_media_config(serial, &cfg)) {
        pack->err = ESP_ERR_NOT_FOUND;
        if (!ui_post(load_ui_done, pack)) free(pack);
        vTaskDelete(NULL);
        return;
    }
    strlcpy(pack->printer_serial, cfg.serial, sizeof(pack->printer_serial));
    size_t n = 0;
    if (files_browses_directory(pack->src, pack->read_only)) {
        pack->err = bambu_ftp_list_directory(&cfg, pack->directory, pack->entries,
                                              CONFIG_BAMBU_FTP_LIST_MAX, &n);
    } else {
        pack->err = bambu_ftp_list_files(&cfg, scope_for_src(pack->src), pack->entries,
                                        CONFIG_BAMBU_FTP_LIST_MAX, &n);
    }
    memset(&cfg, 0, sizeof(cfg));
    pack->count = n;
    if (!ui_post(load_ui_done, pack)) free(pack);
    vTaskDelete(NULL);
}

static void files_request_reload(void)
{
    if (s_files.load_busy || s_files.start_busy || s_files.del_busy) {
        return;
    }
    files_load_result_t *request = calloc(1, sizeof(*request));
    if (!request) return;
    s_files.load_gen = ++s_files_generation;
    request->gen = s_files.load_gen;
    request->src = s_files.src;
    request->read_only = s_files.read_only;
    strlcpy(request->cloud_serial, s_files.cloud_serial, sizeof(request->cloud_serial));
    strlcpy(request->directory, s_files.directory, sizeof(request->directory));
    s_files.cover_busy = false;
    s_files.load_busy = true;
    /* 非阻塞：全屏遮罩会吞掉列表点击，FTPS 较慢时像“点不动” */
    ui_loading_show(false);
    if (xTaskCreate(files_load_worker, "bambu_ftp", BAMBU_FTP_TASK_STACK,
                    request, 5, NULL) != pdPASS) {
        free(request);
        s_files.load_busy = false;
        ui_loading_hide();
        ui_toast(tr("列表任务启动失败"), UI_TOAST_ERROR);
    }
}

static void on_storage(lv_event_t *e)
{
    if(s_files.load_busy || s_files.start_busy || s_files.del_busy)return;
    files_src_t src = (files_src_t)(intptr_t)lv_event_get_user_data(e);
    if (src == s_files.src && !s_files.load_busy) {
        return;
    }
    s_files.src = src;
    strlcpy(s_files.directory, src == FILES_SRC_CACHE ? "cache" : "", sizeof(s_files.directory));
    storage_btn_set_selected((int)src);
    s_files.selected = -1;
    update_file_actions();
    files_request_reload();
}

/* ── 开始打印：发送 → 等 result 应答（最多 5s）→ 按 ack 结果提示 ── */

static void start_print_ui_done(void *p)
{
    files_start_ctx_t *ctx = p;
    if (!ctx || !s_files_alive || ctx->gen != s_files.op_gen) {
        free(ctx);
        return;
    }
    s_files.start_busy = false;
    ui_loading_hide();
    if (ctx->send_fail) {
        ui_toast(tr("未能下发，请检查打印机连接和空闲状态"), UI_TOAST_ERROR);
    } else if (ctx->ack == BAMBU_ACK_OK) {
        ui_toast(tr("打印机已接受任务"), UI_TOAST_SUCCESS);
        if (pm_page_is_top(&page_files)) pm_push(&page_print);
    } else if (ctx->ack == BAMBU_ACK_FAIL) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s: %s", tr("打印机拒绝"), ctx->result[0] ? ctx->result : "?");
        ui_toast(buf, UI_TOAST_ERROR);
    } else {
        ui_toast(tr("已发送，等待打印机确认"), UI_TOAST_INFO);
    }
    free(ctx);
}

static void start_print_worker(void *arg)
{
    files_start_ctx_t *ctx = arg;
    esp_err_t err = bambu_service_start_sd_print(ctx->name, ctx->printer_generation);
    if (err != ESP_OK) {
        ctx->ack = BAMBU_ACK_FAIL;
        ctx->send_fail = true;
    } else {
        uint32_t seq = bambu_service_last_seq();
        ctx->ack = bambu_service_wait_ack(seq, 5000, ctx->result, sizeof(ctx->result));
    }
    if (!ui_post(start_print_ui_done, ctx)) free(ctx);
    vTaskDelete(NULL);
}

static void files_request_reload(void);
static void upload_release(files_upload_ctx_t *ctx)
{
    taskENTER_CRITICAL();bool last=--ctx->refs==0;taskEXIT_CRITICAL();
    if(last){memset(&ctx->cfg,0,sizeof(ctx->cfg));free(ctx);}
}
static bool upload_progress(uint32_t sent,uint32_t total,void *arg)
{
    files_upload_ctx_t *ctx=arg;
    taskENTER_CRITICAL();ctx->sent=sent;ctx->total=total;bool keep=!ctx->cancel;taskEXIT_CRITICAL();
    return keep;
}
static void upload_worker(void *arg)
{
    files_upload_ctx_t *ctx=arg;
    esp_err_t err=bambu_ftp_upload_file(&ctx->cfg,ctx->local,ctx->remote,upload_progress,ctx);
    taskENTER_CRITICAL();ctx->err=err;ctx->done=true;taskEXIT_CRITICAL();
    upload_release(ctx);vTaskDelete(NULL);
}
static void upload_tick(lv_timer_t *timer)
{
    files_upload_ctx_t *ctx=s_files.upload;
    if(!ctx)return;
    taskENTER_CRITICAL();bool done=ctx->done;uint32_t sent=ctx->sent,total=ctx->total;esp_err_t err=ctx->err;taskEXIT_CRITICAL();
    char text[64];snprintf(text,sizeof(text),"上传 %lu / %lu KB",(unsigned long)(sent/1024),(unsigned long)(total/1024));
    lv_label_set_text(s_files.lbl_storage,text);
    if(!done)return;
    s_files.upload=NULL;s_files.upload_timer=NULL;s_files.start_busy=false;lv_timer_delete(timer);
    upload_release(ctx);
    if(err==ESP_OK){ui_toast(tr("上传完成，可在缓存中选择打印"),UI_TOAST_SUCCESS);s_files.src=FILES_SRC_CACHE;storage_btn_set_selected(0);
        s_files.selected = -1;
        update_file_actions();
        files_request_reload();}
    else ui_toast(err==ESP_ERR_INVALID_STATE?tr("已取消或同名文件已存在"):tr("上传失败，请检查连接和存储空间"),UI_TOAST_ERROR);
}
static void start_upload(const bambu_ftp_entry_t *entry)
{
    files_upload_ctx_t *ctx=calloc(1,sizeof(*ctx));if(!ctx)return;
    if(bambu_service_copy_lan_config(&ctx->cfg)!=ESP_OK){free(ctx);return;}
    const char *base=strrchr(entry->name,'/');base=base?base+1:entry->name;
    if(snprintf(ctx->remote,sizeof(ctx->remote),"cache/%s",base)>=(int)sizeof(ctx->remote)){free(ctx);return;}
    snprintf(ctx->local,sizeof(ctx->local),"%s",entry->name);ctx->refs=2;
    s_files.upload=ctx;s_files.start_busy=true;
    s_files.upload_timer=lv_timer_create(upload_tick,200,NULL);
    if(!s_files.upload_timer || xTaskCreate(upload_worker,"bambu_upload",BAMBU_FTP_TASK_STACK,ctx,5,NULL)!=pdPASS){
        if(s_files.upload_timer)lv_timer_delete(s_files.upload_timer);
        s_files.upload_timer=NULL;s_files.upload=NULL;s_files.start_busy=false;upload_release(ctx);upload_release(ctx);
        ui_toast(tr("任务启动失败"),UI_TOAST_ERROR);
    }
}

static void confirm_sd_print(void *arg)
{
    const files_start_ctx_t *confirmed = arg;
    const bambu_ftp_entry_t *entry = files_current_entry();
    if (!s_files_alive || !pm_page_is_top(&page_files) || confirmed->gen != s_files.load_gen ||
        s_files.start_busy || s_files.load_busy || s_files.del_busy ||
        !entry || entry->directory || strcmp(entry->name, confirmed->name)) return;

    files_start_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        ui_toast(tr("任务启动失败"), UI_TOAST_ERROR);
        return;
    }
    *ctx = *confirmed;
    ctx->gen = s_files.op_gen;
    s_files.start_busy = true;
    ui_loading_show(true);
    if (xTaskCreate(start_print_worker, "bambu_start", 6144, ctx, 5, NULL) != pdPASS) {
        s_files.start_busy = false;
        ui_loading_hide();
        free(ctx);
        ui_toast(tr("任务启动失败"), UI_TOAST_ERROR);
    }
}

static void open_lan_print(void *arg)
{
    (void)arg;
    if (pm_page_is_top(&page_files)) pm_push(&page_print);
}

static void on_open_lan_print(void *arg)
{
    (void)arg;
    (void)ui_post(open_lan_print, NULL);
}

static void on_start_print(lv_event_t *e)
{
    (void)e;
    if (s_files.upload) {
        s_files.upload->cancel = true;
        ui_toast(tr("正在取消上传"), UI_TOAST_INFO);
        return;
    }
    if (s_files.start_busy || s_files.load_busy || s_files.del_busy) return;
    const bambu_ftp_entry_t *entry = files_current_entry();
    if (!entry || entry->directory) return;
    if (s_files.src == FILES_SRC_TFCARD) {
        if (s_files.read_only) return;
        bambu_svc_snapshot_t snap;
        bambu_service_snapshot(&snap);
        if (!snap.connected) {
            ui_toast(tr("请先连接打印机"), UI_TOAST_ERROR);
            return;
        }
        start_upload(entry);
        return;
    }
    if (!bambu_ftp_is_printable_path(entry->name)) return;

    char printer[48];
    uint32_t generation;
    esp_err_t ready = bambu_service_prepare_sd_print(s_files.pending->printer_serial,
                                                    &generation, printer, sizeof(printer));
    if (ready == ESP_ERR_INVALID_STATE) {
        ui_toast(tr("打印机未空闲，暂不能启动新任务"), UI_TOAST_INFO);
        return;
    }
    if (ready != ESP_OK) {
        ui_confirm_dialog(tr("连接打印机"), tr("请先通过局域网连接这台打印机，再从 TF 卡选择文件打印。"),
                          tr("去连接"), on_open_lan_print, NULL);
        return;
    }

    s_confirm_print.gen = s_files.load_gen;
    s_confirm_print.printer_generation = generation;
    strlcpy(s_confirm_print.name, entry->name, sizeof(s_confirm_print.name));
    const char *base = strrchr(entry->name, '/');
    base = base ? base + 1 : entry->name;
    char plate[40] = "";
    if (files_name_is_3mf(base))
        snprintf(plate, sizeof(plate), "\n%s %d", tr("打印盘"), bambu_service_project_plate(base));
    snprintf(s_confirm_print_body, sizeof(s_confirm_print_body), "%s\n/%s%s\n%s", printer,
             entry->name, plate, tr("请确认文件已切片并适配这台打印机。"));
    ui_confirm_dialog(tr("确认打印"), s_confirm_print_body, tr("开始打印"),
                      confirm_sd_print, &s_confirm_print);
}

/* ── 删除文件：ui_confirm_dialog 确认 → FTPS DELE → 重载列表 ──
 * 对话框不拷贝文本指针 → 用页面静态缓冲（Files 单例 + modal 互斥，无重入） */
static char s_del_name[BAMBU_FTP_NAME_MAX];
static char s_del_body[BAMBU_FTP_NAME_MAX + 48];

static void files_request_reload(void);

static void del_ui_done(void *p)
{
    files_del_ctx_t *ctx = p;
    if (!ctx || !s_files_alive || ctx->gen != s_files.op_gen) {
        free(ctx);
        return;
    }
    s_files.del_busy = false;
    ui_loading_hide();
    if (ctx->err == ESP_OK) {
        char buf[112];
        snprintf(buf, sizeof(buf), "%s", tr("已删除，正在刷新"));
        ui_toast(buf, UI_TOAST_SUCCESS);
        free(ctx);
        files_request_reload();
        return;
    }
    char buf[96];
    if (ctx->err == ESP_ERR_INVALID_RESPONSE) {
        snprintf(buf, sizeof(buf), "%s", tr("访问码错误或 FTPS 拒绝"));
    } else if (ctx->err == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST) {
        snprintf(buf, sizeof(buf), "%s", tr("无法连接 FTPS :990"));
    } else {
        snprintf(buf, sizeof(buf), "%s", tr("删除失败（打印机正占用该文件？）"));
    }
    ui_toast(buf, UI_TOAST_ERROR);
    free(ctx);
}

static void del_worker(void *arg)
{
    files_del_ctx_t *ctx = arg;
    bambu_lan_config_t cfg;
    if (bambu_service_copy_lan_config(&cfg) != ESP_OK &&
        bambu_profile_load(&cfg) != ESP_OK) {
        ctx->err = ESP_ERR_NOT_FOUND;
    } else {
        ctx->err = bambu_ftp_delete_file(&cfg, ctx->name);
    }
    if (!ui_post(del_ui_done, ctx)) free(ctx);
    vTaskDelete(NULL);
}

static void del_spawn(const char *name)
{
    if (s_files.del_busy) {
        return;
    }
    files_del_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        ui_toast(tr("删除失败"), UI_TOAST_ERROR);
        return;
    }
    ctx->gen = s_files.op_gen;
    snprintf(ctx->name, sizeof(ctx->name), "%s", name);

    s_files.del_busy = true;
    ui_loading_show(true);
    if (xTaskCreate(del_worker, "bambu_del", BAMBU_FTP_TASK_STACK, ctx, 5, NULL) != pdPASS) {
        s_files.del_busy = false;
        ui_loading_hide();
        free(ctx);
        ui_toast(tr("删除失败"), UI_TOAST_ERROR);
    }
}

static void on_del_ok(void *ctx)
{
    (void)ctx;
    if (!s_files.read_only) del_spawn(s_del_name);
}

static void on_delete_file(lv_event_t *e)
{
    (void)e;
    if (s_files.read_only) return;
    if (!s_files.pending || s_files.selected < 0 ||
        (size_t)s_files.selected >= s_files.pending->count) {
        ui_toast(tr("请选择文件"), UI_TOAST_INFO);
        return;
    }
    if (s_files.src==FILES_SRC_TFCARD || s_files.start_busy || s_files.del_busy || s_files.load_busy) {
        return;
    }
    snprintf(s_del_name, sizeof(s_del_name), "%s",
             s_files.pending->entries[s_files.selected].name);
    snprintf(s_del_body, sizeof(s_del_body), "%s\n%s", s_del_name,
             tr("将从打印机存储删除，不可恢复"));
    ui_confirm_dialog(tr("删除文件"), s_del_body, tr("删除"), on_del_ok, NULL);
}

static void on_refresh_list(lv_event_t *e)
{
    (void)e;
    files_request_reload();
}

static void files_retranslate_static(void)
{
    if (s_files.lbl_path && lv_obj_is_valid(s_files.lbl_path)) update_path_label();
    if (s_files.btn_start_print && lv_obj_is_valid(s_files.btn_start_print)) update_file_actions();
    static const char *const stor_keys[] = { "缓存", "打印机 TF 卡", "本机TF卡" };
    for (int i = 0; i < 3; i++) {
        if (!s_files.storage_btns[i] || !lv_obj_is_valid(s_files.storage_btns[i])) {
            continue;
        }
        lv_obj_t *lb = lv_obj_get_child(s_files.storage_btns[i], 1);
        if (lb) {
            lv_label_set_text(lb, tr(stor_keys[i]));
        }
    }
    if (s_files.btn_refresh && lv_obj_is_valid(s_files.btn_refresh)) {
        lv_obj_t *lbl = lv_obj_get_child(s_files.btn_refresh, 0);
        if (lbl) {
            char refr_lbl[32];
            snprintf(refr_lbl, sizeof(refr_lbl), LV_SYMBOL_REFRESH " %s", tr("刷新"));
            lv_label_set_text(lbl, refr_lbl);
        }
    }
}

static void files_on_language(void *user_data)
{
    (void)user_data;
    files_retranslate_static();
}

static void files_destroy_page(void *user_data)
{
    (void)user_data;
    s_files_alive = false;
    if(s_files.upload){s_files.upload->cancel=true;upload_release(s_files.upload);s_files.upload=NULL;}
    if(s_files.upload_timer){lv_timer_delete(s_files.upload_timer);s_files.upload_timer=NULL;}
    s_files.op_gen = s_files.load_gen = ++s_files_generation;
    ui_loading_hide();
    if (s_files.pending) {
        free(s_files.pending);
        s_files.pending = NULL;
    }
    if (s_files.img_preview) lv_image_set_src(s_files.img_preview, NULL);
    lv_image_cache_drop(&s_files.cover_dsc);
    lv_image_header_cache_drop(&s_files.cover_dsc);
    free(s_files.cover_png);
    s_files.cover_png = NULL;
    s_files.cover_len = 0;
    s_files.cover_name[0] = 0;
    s_files.cover_busy = false;   /* 页面重建后由 update_detail 重新拉 */
}

static void files_build_list_column(lv_obj_t *list_card)
{
    lv_obj_set_style_bg_color(list_card, UI_COLOR_CARD, 0);
    lv_obj_set_flex_flow(list_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *hdr = ui_create_row(list_card);
    s_files.lbl_path = ui_create_label(hdr, path_title_for_src(FILES_SRC_SDCARD), UI_FONT_CAPTION,
                                       UI_COLOR_TEXT_SEC);
    lv_obj_set_flex_grow(s_files.lbl_path, 1);
    lv_label_set_long_mode(s_files.lbl_path, LV_LABEL_LONG_DOT);
    s_files.btn_parent = ui_create_btn(hdr, "上一级", LV_SIZE_CONTENT, ui_land_btn_h_sm(),
                                       UI_BTN_SUBTLE, on_parent_directory, NULL);
    lv_obj_add_flag(s_files.btn_parent, LV_OBJ_FLAG_HIDDEN);
    char refr_lbl[32];
    snprintf(refr_lbl, sizeof(refr_lbl), LV_SYMBOL_REFRESH " %s", tr("刷新"));
    s_files.btn_refresh = ui_create_btn(hdr, refr_lbl, LV_SIZE_CONTENT, ui_land_btn_h_sm(),
                                        UI_BTN_SUBTLE, on_refresh_list, NULL);

    s_files.list = lv_obj_create(list_card);
    lv_obj_remove_style_all(s_files.list);
    lv_obj_set_width(s_files.list, LV_PCT(100));
    lv_obj_set_flex_grow(s_files.list, 1);
    lv_obj_set_flex_flow(s_files.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_files.list, UI_PAD_SM, 0);
    lv_obj_set_style_min_height(s_files.list, 120, 0);
    lv_obj_add_flag(s_files.list, LV_OBJ_FLAG_SCROLLABLE);
}

static void files_build_detail_panel(lv_obj_t *detail)
{
    const bool land = ui_screen_is_landscape();

    lv_obj_set_style_bg_color(detail, UI_COLOR_CARD, 0);
    s_files.lbl_detail_name = ui_create_label(detail, "—", UI_FONT_BODY, UI_COLOR_TEXT);
    lv_label_set_long_mode(s_files.lbl_detail_name, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_files.lbl_detail_name, LV_PCT(100));
    s_files.preview = lv_obj_create(detail);
    lv_obj_set_width(s_files.preview, LV_PCT(100));
    if (land) {
        lv_obj_set_height(s_files.preview, LV_PCT(100));
        lv_obj_set_flex_grow(s_files.preview, 1);
        lv_obj_set_style_min_height(s_files.preview, 96, 0);
    } else {
        lv_obj_set_height(s_files.preview, 140);
    }
    lv_obj_set_style_bg_color(s_files.preview, UI_COLOR_CARD_HI, 0);
    lv_obj_set_style_radius(s_files.preview, 12, 0);
    lv_obj_clear_flag(s_files.preview, LV_OBJ_FLAG_SCROLLABLE);
    s_files.lbl_preview_ph = ui_create_label(s_files.preview, LV_SYMBOL_IMAGE, UI_FONT_SYMBOL,
                                             UI_COLOR_TEXT_SEC);
    lv_obj_center(s_files.lbl_preview_ph);
    s_files.img_preview = lv_image_create(s_files.preview);
    lv_obj_center(s_files.img_preview);
    lv_obj_add_flag(s_files.img_preview, LV_OBJ_FLAG_HIDDEN);
    s_files.lbl_detail_meta = ui_create_label(detail, "", UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
    s_files.btn_start_print = ui_create_btn_bind(detail, "开始打印", LV_PCT(100),
                  land ? ui_land_btn_h() : ui_land_btn_h_sm(), UI_BTN_FILLED, on_start_print, NULL);
    s_files.btn_delete_file = ui_create_btn_bind(detail, "删除文件", LV_PCT(100),
                  land ? ui_land_btn_h() : ui_land_btn_h_sm(), UI_BTN_DANGER, on_delete_file, NULL);
}

static void files_build_storage_btns(lv_obj_t *parent, bool portrait)
{
    if (portrait) {
        lv_obj_t *stor = ui_create_row(parent);
        lv_obj_set_style_pad_column(stor, UI_PAD_SM, 0);
        for (int i = 0; i < 3; i++) {
            lv_obj_t *cell = lv_obj_create(stor);
            lv_obj_remove_style_all(cell);
            lv_obj_set_flex_grow(cell, 1);
            lv_obj_set_height(cell, LV_SIZE_CONTENT);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *btn = NULL;
            if (i == 0) {
                btn = ui_bambu_storage_nav_btn(cell, LV_SYMBOL_DRIVE, tr("缓存"), false, on_storage,
                                               (void *)(intptr_t)FILES_SRC_CACHE);
            } else if (i == 1) {
                btn = ui_bambu_storage_nav_btn(cell, LV_SYMBOL_SD_CARD, tr("打印机 TF 卡"), true, on_storage,
                                               (void *)(intptr_t)FILES_SRC_SDCARD);
            } else {
                btn = ui_bambu_storage_nav_btn(cell, LV_SYMBOL_WIFI, tr("本机TF卡"), false, on_storage,
                                               (void *)(intptr_t)FILES_SRC_TFCARD);
            }
            s_files.storage_btns[i] = btn;
        }
        s_files.lbl_storage = ui_create_label(parent, tr("加载中…"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
        storage_btn_set_selected(FILES_SRC_SDCARD);
        return;
    }

    lv_obj_set_width(parent, LV_PCT(20));
    lv_obj_set_height(parent, LV_PCT(100));
    s_files.storage_btns[0] =
        ui_bambu_storage_nav_btn(parent, LV_SYMBOL_DRIVE, tr("缓存"), false, on_storage,
                                 (void *)(intptr_t)FILES_SRC_CACHE);
    s_files.storage_btns[1] =
        ui_bambu_storage_nav_btn(parent, LV_SYMBOL_SD_CARD, tr("打印机 TF 卡"), true, on_storage,
                                 (void *)(intptr_t)FILES_SRC_SDCARD);
    s_files.storage_btns[2] =
        ui_bambu_storage_nav_btn(parent, LV_SYMBOL_WIFI, tr("本机TF卡"), false, on_storage,
                                 (void *)(intptr_t)FILES_SRC_TFCARD);
    storage_btn_set_selected(FILES_SRC_SDCARD);
    s_files.lbl_storage = ui_create_label(parent, tr("加载中…"), UI_FONT_CAPTION, UI_COLOR_TEXT_SEC);
}

static void sync_media_view(void)
{
    const char *serial = ui_bambu_media_cloud_serial();
    s_files.read_only = serial != NULL;
    strlcpy(s_files.cloud_serial, serial ? serial : "", sizeof(s_files.cloud_serial));
    update_file_actions();
}

static void files_on_resume(void *user_data)
{
    (void)user_data;
    bool was_read_only = s_files.read_only;
    sync_media_view();
    if ((was_read_only || s_files.read_only) && !s_files.start_busy && !s_files.del_busy) {
        s_files.selected = -1;
        update_file_actions();
        s_files.src = FILES_SRC_SDCARD;
        s_files.directory[0] = 0;
        storage_btn_set_selected(FILES_SRC_SDCARD);
        s_files.load_busy = false;
        list_clear();
        files_request_reload();
    }
}

static lv_obj_t *files_create(lv_obj_t *parent, void *user_data)
{
    (void)user_data;
    memset(&s_files, 0, sizeof(s_files));
    s_files_alive = true;
    s_files.op_gen = ++s_files_generation;
    s_files.src = FILES_SRC_SDCARD;
    s_files.selected = -1;
    s_files.pending = calloc(1, sizeof(files_load_result_t));

    const bool land = ui_screen_is_landscape();

    lv_obj_t *root = ui_create_page_root(parent);
    lv_obj_set_style_bg_color(root, UI_COLOR_BG, 0);
    lv_obj_t *content = ui_create_content(root);
    ui_create_tab_root_header_bind(content, "设备存储", NULL);
    if(land)files_build_storage_btns(content,true);

    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_min_height(content, 0, 0);

    if (!land) {
        lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(content, UI_PAD_SM, 0);

        lv_obj_t *stor_card = ui_create_card(content);
        lv_obj_set_style_bg_color(stor_card, UI_COLOR_CARD, 0);
        files_build_storage_btns(stor_card, true);

        lv_obj_t *list_card = ui_create_card(content);
        lv_obj_set_width(list_card, LV_PCT(100));
        lv_obj_set_height(list_card, ui_page_body_h() / 2);
        files_build_list_column(list_card);

        lv_obj_t *detail = ui_create_card(content);
        lv_obj_set_width(detail, LV_PCT(100));
        files_build_detail_panel(detail);
    } else {
        lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *row = lv_obj_create(content);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_flex_grow(row, 1);
        lv_obj_set_style_min_height(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, UI_PAD_SM, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);



        lv_obj_t *list_card = ui_create_card(row);
        lv_obj_set_flex_grow(list_card, 1);
        lv_obj_set_height(list_card, LV_PCT(100));
        lv_obj_set_style_min_height(list_card, 0, 0);
        files_build_list_column(list_card);

        lv_obj_t *detail = ui_create_card(row);
        lv_obj_set_width(detail, LV_PCT(36));
        lv_obj_set_height(detail, LV_PCT(100));
        files_build_detail_panel(detail);
    }

    sync_media_view();
    files_request_reload();
    return root;
}

const page_t page_files = {
    .name = "文件",
    .create = files_create,
    .destroy = files_destroy_page,
    .on_language = files_on_language,
    .on_resume = files_on_resume,
    .cache = PM_CACHE_KEEP_ALIVE,
    .user_data = NULL,
};
