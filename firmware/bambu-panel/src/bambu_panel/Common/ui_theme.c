/*
 * SPDX-FileCopyrightText: 2024-2026 Kevincoooool
 * SPDX-License-Identifier: Apache-2.0
 *
 * KSDIY ESP32-P4/C5 Development Board
 * Author : Kevincoooool (https://github.com/kevincoooool)
 * Store  : 淘宝店铺「酷世DIY」
 * GitHub : https://github.com/kevincoooool/ESP32P4_KSDIY
 *
 * KSDIY 通用应用模板 - UI 主题实现
 *
 * 1. 中文字体: FreeType 矢量(voice_call.ttf, 全字形覆盖)优先, 失败回退 BSP
 *    点阵 myFont; 两者都挂 montserrat fallback 补 LV_SYMBOL_* 图标混排。
 *    启动即后台预载 TTF(与显示初始化并行), 主题初始化时未完成则先用点阵,
 *    预载完成后 80ms 定时器热升级 + 全屏重绘（UI 不阻塞秒开）。
 * 2. 产品级 Toast 与确认对话框。
 */
#include "ui_theme.h"
#include "ui_symbol_font.h"

#include "i18n.h"
#include "ui_modal.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "ui_theme";

/* ──────────────────── 配色方案（暗色/亮色） ──────────────────── */

const ui_palette_t g_ui_pal_dark = {
    .bg        = LV_COLOR_MAKE(0x14, 0x14, 0x14),
    .bg_bar    = LV_COLOR_MAKE(0x1C, 0x1C, 0x1C),
    .card      = LV_COLOR_MAKE(0x23, 0x23, 0x23),
    .card_hi   = LV_COLOR_MAKE(0x30, 0x30, 0x30),
    .primary   = LV_COLOR_MAKE(0x00, 0xAE, 0x42),
    .accent    = LV_COLOR_MAKE(0x00, 0xAE, 0x42),
    .success   = LV_COLOR_MAKE(0x00, 0xAE, 0x42),
    .danger    = LV_COLOR_MAKE(0xE5, 0x4B, 0x4B),
    .warning   = LV_COLOR_MAKE(0xF0, 0x9A, 0x17),
    .text      = LV_COLOR_MAKE(0xF2, 0xF2, 0xF2),
    .text_sec  = LV_COLOR_MAKE(0xA3, 0xA3, 0xA3),
    .border    = LV_COLOR_MAKE(0x38, 0x38, 0x38),
    .track     = LV_COLOR_MAKE(0x3A, 0x3A, 0x3A),
};

const ui_palette_t g_ui_pal_light = {
    .bg        = LV_COLOR_MAKE(0xF3, 0xF4, 0xF5),
    .bg_bar    = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .card      = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .card_hi   = LV_COLOR_MAKE(0xF0, 0xF1, 0xF2),
    .primary   = LV_COLOR_MAKE(0x00, 0xAE, 0x42),
    .accent    = LV_COLOR_MAKE(0x00, 0xAE, 0x42),
    .success   = LV_COLOR_MAKE(0x00, 0xAE, 0x42),
    .danger    = LV_COLOR_MAKE(0xD8, 0x3D, 0x3D),
    .warning   = LV_COLOR_MAKE(0xD9, 0x87, 0x13),
    .text      = LV_COLOR_MAKE(0x1A, 0x1A, 0x1A),
    .text_sec  = LV_COLOR_MAKE(0x75, 0x75, 0x75),
    .border    = LV_COLOR_MAKE(0xE8, 0xE9, 0xEB),
    .track     = LV_COLOR_MAKE(0xE5, 0xE7, 0xE8),
};

static ui_palette_t s_brand_dark;    /* 品牌色覆盖副本（RAM，set_brand 后启用） */
static ui_palette_t s_brand_light;
static bool s_branded = false;
static ui_theme_mode_t s_mode = UI_THEME_DARK;
static const ui_palette_t *s_pal = &g_ui_pal_dark;

const ui_palette_t *ui_palette(void)
{
    return s_pal;
}

void ui_theme_set_brand(lv_color_t primary, lv_color_t accent)
{
    s_brand_dark = g_ui_pal_dark;
    s_brand_dark.primary = primary;
    s_brand_dark.accent = accent;
    s_brand_light = g_ui_pal_light;
    s_brand_light.primary = primary;
    s_brand_light.accent = accent;
    s_branded = true;
    ui_theme_set_mode(s_mode);   /* 立即切换到品牌副本 */
}

void ui_theme_set_mode(ui_theme_mode_t mode)
{
    s_mode = mode;
    if (s_branded) {
        s_pal = (mode == UI_THEME_LIGHT) ? &s_brand_light : &s_brand_dark;
    } else {
        s_pal = (mode == UI_THEME_LIGHT) ? &g_ui_pal_light : &g_ui_pal_dark;
    }
}

ui_theme_mode_t ui_theme_get_mode(void)
{
    return s_mode;
}

/* ──────────────────── 中文字体（带符号回退） ──────────────────── */

lv_font_t ui_font_display; /* 40px 超大读数 */
lv_font_t ui_font_title;   /* 26px 标题 */
lv_font_t ui_font_list;    /* 22px 列表主标题 */
lv_font_t ui_font_body;    /* 20px 正文 */
lv_font_t ui_font_sub;     /* 18px 列表副标题 / 右侧当前值 */
lv_font_t ui_font_caption; /* 16px 小标签 */
lv_font_t ui_font_num;     /* 48px 大数字 */
lv_font_t ui_font_num_sm;  /* 30px 中数字 */

/* Font initialization is synchronous after lv_init; no preload task needed. */
void ui_theme_font_preload_async(void)
{
    /* The compressed font is unpacked once when the theme creates its fonts. */
}

#if LV_USE_FREETYPE
#include "src/libs/freetype/lv_freetype.h"
#include "src/misc/lv_fs_private.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GZIP_H
#include FT_SYSTEM_H
extern const uint8_t ksdiy_ui_ttf_start[];
extern const uint8_t ksdiy_ui_ttf_end[];
extern const uint32_t ksdiy_ui_ttf_raw_size;
static uint8_t *s_ft_buf;
static lv_fs_path_ex_t s_ft_path;
static bool s_ft_path_ready;
static bool s_ft_ready;

static void *font_zip_alloc(FT_Memory memory, long size)
{
    (void)memory;
    return size > 0 ? lv_malloc((size_t)size) : NULL;
}

static void font_zip_free(FT_Memory memory, void *block)
{
    (void)memory;
    lv_free(block);
}

static void *font_zip_realloc(FT_Memory memory, long old_size,
                              long new_size, void *block)
{
    (void)memory;
    (void)old_size;
    return new_size > 0 ? lv_realloc(block, (size_t)new_size) : NULL;
}

static bool font_buffer_init(void)
{
    if (s_ft_path_ready) return true;
    const uint32_t packed_size = (uint32_t)(ksdiy_ui_ttf_end - ksdiy_ui_ttf_start);
    FT_ULong raw_size = ksdiy_ui_ttf_raw_size;
    struct FT_MemoryRec_ memory = {
        .user = NULL, .alloc = font_zip_alloc,
        .free = font_zip_free, .realloc = font_zip_realloc
    };
    s_ft_buf = lv_malloc((size_t)raw_size);
    if (!s_ft_buf) {
        ESP_LOGE(TAG, "FreeType font buffer allocation failed (%lu bytes)", raw_size);
        return false;
    }
    FT_Error error = FT_Gzip_Uncompress(&memory, s_ft_buf, &raw_size,
                                       ksdiy_ui_ttf_start, packed_size);
    if (error || raw_size != ksdiy_ui_ttf_raw_size) {
        ESP_LOGE(TAG, "FreeType font unpack failed: error=%d bytes=%lu", error, raw_size);
        lv_free(s_ft_buf);
        s_ft_buf = NULL;
        return false;
    }
    lv_fs_make_path_from_buffer(&s_ft_path, LV_FS_MEMFS_LETTER,
                               s_ft_buf, (uint32_t)raw_size);
    s_ft_path_ready = true;
    ESP_LOGI(TAG, "FreeType font unpacked: %u -> %lu bytes", packed_size, raw_size);
    return true;
}

static lv_font_t *ft_create_px(int px)
{
    if (!font_buffer_init()) return NULL;
    return lv_freetype_font_create((const char *)&s_ft_path,
                                  LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                  px, LV_FREETYPE_FONT_STYLE_NORMAL);
}

static bool fonts_init_freetype(void)
{
    static const int sizes[] = {48, 40, 30, 26, 22, 20, 18, 16};
    lv_font_t *faces[8] = {0};
    lv_font_t *targets[] = {&ui_font_num, &ui_font_display, &ui_font_num_sm,
                           &ui_font_title, &ui_font_list, &ui_font_body,
                           &ui_font_sub, &ui_font_caption};
    if (s_ft_ready) return true;
    for (unsigned i = 0; i < 8; ++i) {
        faces[i] = ft_create_px(sizes[i]);
        if (!faces[i]) {
            for (unsigned j = 0; j < i; ++j) lv_freetype_font_delete(faces[j]);
            ESP_LOGE(TAG, "FreeType font create failed at %d px", sizes[i]);
            return false;
        }
    }
    for (unsigned i = 0; i < 8; ++i) {
        *targets[i] = *faces[i];
        targets[i]->fallback = ui_symbol_font(sizes[i]);
    }
    s_ft_ready = true;
    ESP_LOGI(TAG, "FreeType embedded voice_call.ttf: %u bytes, 48/40/30/26/22/20/18/16 px",
             (unsigned)ksdiy_ui_ttf_raw_size);
    return true;
}

bool ui_theme_freetype_ready(void)
{
    return s_ft_ready;
}

const lv_font_t *ui_theme_acquire_font_px(int px)
{
    static struct { int px; lv_font_t *font; } cache[8];
    if (px <= 0 || !s_ft_ready) return NULL;
    for (unsigned i = 0; i < 8; ++i)
        if (cache[i].font && cache[i].px == px) return cache[i].font;
    for (unsigned i = 0; i < 8; ++i) {
        if (!cache[i].font) {
            lv_font_t *font = ft_create_px(px);
            if (!font) return NULL;
            font->fallback = ui_symbol_font(px);
            cache[i].px = px;
            cache[i].font = font;
            return font;
        }
    }
    ESP_LOGW(TAG, "FreeType extra-size cache full (%d px)", px);
    return NULL; /* Existing fonts may still be referenced by widgets. */
}
#else
bool ui_theme_freetype_ready(void) { return false; }
const lv_font_t *ui_theme_acquire_font_px(int px) { (void)px; return NULL; }
#endif

/* FreeType 不可用时回退到 BSP 的 24 和 20px 点阵中文。 */
static void theme_init_fallback(void)
{
    ui_font_num = lv_font_montserrat_48;
    ui_font_num_sm = lv_font_montserrat_28;

    ui_font_display = myFont_24;
    ui_font_display.fallback = ui_symbol_font(48);


    ui_font_title = myFont_24;
    ui_font_title.fallback = ui_symbol_font(26);


    ui_font_list = myFont_24;
    ui_font_list.fallback = ui_symbol_font(22);


    ui_font_body = myFont_20;
    ui_font_body.fallback = ui_symbol_font(20);


    ui_font_sub = myFont_20;
    ui_font_sub.fallback = ui_symbol_font(18);


    ui_font_caption = myFont_20;   /* 原用 myFont_14，为省 flash 合并到 20px */
    ui_font_caption.fallback = ui_symbol_font(16);


    ESP_LOGI(TAG, "theme fallback ready (myFont_24/20/14 + montserrat fallback)");
}

static void fonts_init_native(void)
{
#if LV_USE_FREETYPE
    if (fonts_init_freetype()) {
        ESP_LOGI(TAG, "theme ready (FreeType + symbol fallback)");
        return;
    }
    ESP_LOGW(TAG, "FreeType unavailable, using bitmap fallback");
#endif
    theme_init_fallback();
}

void ui_theme_apply_lvgl(void)
{
#if LV_USE_THEME_DEFAULT
    lv_display_t *disp = lv_display_get_default();
    if (disp) lv_theme_default_init(disp, UI_COLOR_PRIMARY, UI_COLOR_ACCENT,
                                    ui_theme_get_mode() != UI_THEME_LIGHT, UI_FONT_BODY);
#endif
    lv_obj_t *screen = lv_scr_act();
    if (screen) lv_obj_set_style_text_font(screen, UI_FONT_BODY, 0);
}
void ui_theme_init(void)
{
    fonts_init_native();
    ui_theme_apply_lvgl();
}

/* ──────────────────── Toast ──────────────────── */

static lv_obj_t *s_toast = NULL;

static void toast_set_opa_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)v, 0);
}

static void toast_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
}

void ui_toast(const char *text, ui_toast_type_t type)
{
    /* 挂顶层图层并置顶：盖过 Modal / 键盘（DESIGN §9，Toast 最高 z） */
    lv_obj_t *layer = lv_layer_top();
    if (!layer) return;

    /* 已有 toast 先移除，保持单例 */
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }

    lv_color_t accent;
    const char *icon;
    switch (type) {
    case UI_TOAST_SUCCESS: accent = UI_COLOR_SUCCESS; icon = LV_SYMBOL_OK " "; break;
    case UI_TOAST_ERROR:   accent = UI_COLOR_DANGER;  icon = LV_SYMBOL_CLOSE " "; break;
    default:               accent = UI_COLOR_ACCENT;  icon = LV_SYMBOL_BELL " "; break;
    }

    s_toast = lv_obj_create(layer);
    lv_obj_set_width(s_toast, LV_SIZE_CONTENT);
    lv_obj_set_height(s_toast, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toast, UI_SCREEN_W - 60, 0);
    lv_obj_set_style_bg_color(s_toast, UI_COLOR_CARD_HI, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_border_color(s_toast, accent, 0);
    lv_obj_set_style_radius(s_toast, 12, 0);
    lv_obj_set_style_pad_hor(s_toast, 16, 0);
    lv_obj_set_style_pad_ver(s_toast, 10, 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_shadow_width(s_toast, 0, 0);

    lv_obj_t *label = lv_label_create(s_toast);
    char buf[160];
    snprintf(buf, sizeof(buf), "%s%s", icon, text);
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_font(label, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_center(label);

    /* 置于 Tab 栏上方 + 全图层最高 z */
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -(UI_TAB_BAR_H + 20));
    lv_obj_move_to_index(s_toast, -1);

    /* 入场渐显 + 延时消失（只淡 bg_opa，对象 opa 保持 COVER） */
    lv_obj_set_style_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_toast);
    lv_anim_set_exec_cb(&a, toast_set_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, 180);
    lv_anim_start(&a);

    lv_timer_create(toast_timer_cb, 2600, NULL);
}

/* ──────────────────── 确认对话框（经 ui_modal 统一层） ──────────────────── */

typedef struct {
    const char *title;
    const char *body;
    const char *confirm;         /* NULL = tr("确定") */
    void (*on_ok)(void *ctx);
    void *ctx;
    lv_obj_t *card;
} ui_confirm_ctx_t;

static void confirm_ok_cb(lv_event_t *e)
{
    ui_confirm_ctx_t *c = (ui_confirm_ctx_t *)lv_event_get_user_data(e);
    if (c->on_ok) {
        c->on_ok(c->ctx);
    }
    ui_modal_close(c->card);
}

static void confirm_cancel_cb(lv_event_t *e)
{
    ui_confirm_ctx_t *c = (ui_confirm_ctx_t *)lv_event_get_user_data(e);
    (void)e;
    ui_modal_close(c->card);
}

/* ui_modal 关闭前回调：无论哪种关闭路径（按钮/返回链/业务），唯一释放点 */
static void confirm_on_close(lv_obj_t *card, void *ud)
{
    (void)card;
    lv_free(ud);
}

static void confirm_build(lv_obj_t *card, void *ud)
{
    ui_confirm_ctx_t *c = (ui_confirm_ctx_t *)ud;
    c->card = card;

    ui_create_label(card, c->title, UI_FONT_TITLE, UI_COLOR_TEXT);

    lv_obj_t *body_lbl = lv_label_create(card);
    lv_label_set_text(body_lbl, c->body);
    lv_obj_set_style_text_font(body_lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(body_lbl, UI_COLOR_TEXT_SEC, 0);
    lv_label_set_long_mode(body_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body_lbl, LV_PCT(100));

    /* 按钮行 */
    lv_obj_t *row = ui_create_row(card);
    lv_obj_set_style_pad_column(row, 10, 0);

    ui_create_btn(row, tr("取消"), 0, UI_BTN_H_SM, UI_BTN_SUBTLE,
                  confirm_cancel_cb, c);
    ui_create_btn(row, c->confirm ? c->confirm : tr("确定"), 0, UI_BTN_H_SM,
                  UI_BTN_SUCCESS, confirm_ok_cb, c);
}

void ui_confirm_dialog(const char *title, const char *body, const char *confirm,
                       void (*on_ok)(void *ctx), void *ctx)
{
    if (!title || !body) {
        return;
    }
    ui_confirm_ctx_t *c = lv_malloc_zeroed(sizeof(ui_confirm_ctx_t));
    c->title = title;
    c->body = body;
    c->confirm = confirm;
    c->on_ok = on_ok;
    c->ctx = ctx;

    ui_modal_builder_t b = {
        .build = confirm_build,
        .on_close = confirm_on_close,
        .user_data = c,
        .dim = LV_OPA_60,   /* 与旧版遮罩浓度一致 */
    };
    ui_modal_open(&b);
}
