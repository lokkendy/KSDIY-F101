#include "bambu_model.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef struct {
    bambu_model_t id;
    const char *ui_name;
    const char *short_name;
    const char *lan_hint;
    uint32_t pushall_min_ms;
    bool chamber;
} bambu_model_info_t;

static const bambu_model_info_t s_models[] = {
    { BAMBU_MODEL_P1P,      "P1P",      "P1P",      "LAN：打印机 IP + 访问码（常规 LAN 模式）", 300000, false },
    { BAMBU_MODEL_P1S,      "P1S",      "P1S",      "LAN：同 P1 系列，IP + 8 位访问码", 300000, false },
    { BAMBU_MODEL_X1,       "X1",       "X1",       "LAN：IP + 访问码", 180000, false },
    { BAMBU_MODEL_X1C,      "X1 Carbon", "X1C",     "LAN：IP + 访问码（有机箱温湿度字段）", 180000, true },
    { BAMBU_MODEL_X1E,      "X1E",      "X1E",      "LAN：IP + 访问码（有机箱温度）", 180000, true },
    { BAMBU_MODEL_A1,       "A1",       "A1",       "LAN：IP + 访问码", 300000, false },
    { BAMBU_MODEL_A1_MINI,  "A1 mini",  "A1 mini",  "LAN：IP + 访问码", 300000, false },
    { BAMBU_MODEL_H2S,      "H2S",      "H2S",      "须 LAN Only + 开发者模式，再填 IP/访问码", 180000, true },
    { BAMBU_MODEL_H2C,      "H2C",      "H2C",      "须 LAN Only + 开发者模式", 180000, true },
    { BAMBU_MODEL_H2D,      "H2D",      "H2D",      "须 LAN Only + 开发者模式", 180000, true },
    { BAMBU_MODEL_OTHER,    "其它机型",  "Bambu",    "LAN MQTT 通用：IP + SN + 访问码", 300000, false },
};

static char s_dropdown_buf[256];

const char *bambu_model_dropdown_text(void)
{
    if (s_dropdown_buf[0]) {
        return s_dropdown_buf;
    }
    size_t pos = 0;
    for (size_t i = 0; i < sizeof(s_models) / sizeof(s_models[0]); i++) {
        const char *name = s_models[i].ui_name;
        size_t n = strlen(name);
        if (pos + n + 2 >= sizeof(s_dropdown_buf)) {
            break;
        }
        if (pos > 0) {
            s_dropdown_buf[pos++] = '\n';
        }
        memcpy(s_dropdown_buf + pos, name, n);
        pos += n;
    }
    s_dropdown_buf[pos] = 0;
    return s_dropdown_buf;
}

size_t bambu_model_ui_count(void)
{
    return sizeof(s_models) / sizeof(s_models[0]);
}

bambu_model_t bambu_model_from_ui_index(unsigned index)
{
    if (index >= bambu_model_ui_count()) {
        return BAMBU_MODEL_OTHER;
    }
    return s_models[index].id;
}

unsigned bambu_model_to_ui_index(bambu_model_t model)
{
    for (size_t i = 0; i < bambu_model_ui_count(); i++) {
        if (s_models[i].id == model) {
            return (unsigned)i;
        }
    }
    return bambu_model_to_ui_index(BAMBU_MODEL_OTHER);
}

static const bambu_model_info_t *info_for(bambu_model_t model)
{
    for (size_t i = 0; i < bambu_model_ui_count(); i++) {
        if (s_models[i].id == model) {
            return &s_models[i];
        }
    }
    return &s_models[bambu_model_ui_count() - 1];
}

const char *bambu_model_short_name(bambu_model_t model)
{
    return info_for(model)->short_name;
}

const char *bambu_model_lan_hint(bambu_model_t model)
{
    return info_for(model)->lan_hint;
}

uint32_t bambu_model_pushall_min_ms(bambu_model_t model)
{
    return info_for(model)->pushall_min_ms;
}

bool bambu_model_supports_chamber(bambu_model_t model)
{
    return info_for(model)->chamber;
}

bambu_model_t bambu_model_from_nvs(uint8_t stored)
{
    if (stored == 0 || stored >= BAMBU_MODEL_COUNT) {
        return BAMBU_MODEL_P1P;
    }
    return (bambu_model_t)stored;
}

uint8_t bambu_model_to_nvs(bambu_model_t model)
{
    if (model <= BAMBU_MODEL_UNKNOWN || model >= BAMBU_MODEL_COUNT) {
        return (uint8_t)BAMBU_MODEL_OTHER;
    }
    return (uint8_t)model;
}

static bool streq_ci(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) {
            return false;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static bool contains_ci(const char *hay, const char *needle)
{
    if (!hay || !needle || !needle[0]) {
        return false;
    }
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0) {
            return true;
        }
    }
    return false;
}

bambu_model_t bambu_model_from_devmodel(const char *dev_model)
{
    if (!dev_model || !dev_model[0]) {
        return BAMBU_MODEL_OTHER;
    }
    if (streq_ci(dev_model, "C11") || contains_ci(dev_model, "P1P")) {
        return BAMBU_MODEL_P1P;
    }
    if (streq_ci(dev_model, "C12") || contains_ci(dev_model, "P1S")) {
        return BAMBU_MODEL_P1S;
    }
    if (streq_ci(dev_model, "BL-P002") || streq_ci(dev_model, "3DPrinter-X1")) {
        return BAMBU_MODEL_X1;
    }
    if (streq_ci(dev_model, "BL-P001") || contains_ci(dev_model, "X1-Carbon")
        || contains_ci(dev_model, "X1 Carbon")) {
        return BAMBU_MODEL_X1C;
    }
    if (streq_ci(dev_model, "C13") || contains_ci(dev_model, "X1E")) {
        return BAMBU_MODEL_X1E;
    }
    if (streq_ci(dev_model, "N2S") || (contains_ci(dev_model, "A1") && !contains_ci(dev_model, "mini"))) {
        return BAMBU_MODEL_A1;
    }
    if (streq_ci(dev_model, "N1") || contains_ci(dev_model, "A1 mini") || contains_ci(dev_model, "A1-mini")) {
        return BAMBU_MODEL_A1_MINI;
    }
    if (contains_ci(dev_model, "H2S")) {
        return BAMBU_MODEL_H2S;
    }
    if (contains_ci(dev_model, "H2C")) {
        return BAMBU_MODEL_H2C;
    }
    if (streq_ci(dev_model, "O1D") || contains_ci(dev_model, "H2D")) {
        return BAMBU_MODEL_H2D;
    }
    return BAMBU_MODEL_OTHER;
}

bool bambu_model_camera_tcp_jpeg(bambu_model_t model)
{
    switch (model) {
    case BAMBU_MODEL_P1P:
    case BAMBU_MODEL_P1S:
    case BAMBU_MODEL_A1:
    case BAMBU_MODEL_A1_MINI:
    case BAMBU_MODEL_UNKNOWN:
    case BAMBU_MODEL_OTHER:
        return true;
    default:
        return false;
    }
}
