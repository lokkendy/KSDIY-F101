#include "bambu_profile.h"

#include "bambu_camera.h"
#include "bambu_model.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include <ctype.h>
#include <string.h>

#define NS "bambu"

static void profile_normalize(bambu_lan_config_t *out)
{
    if (!out) {
        return;
    }
    size_t n = strlen(out->serial);
    while (n > 0 && isspace((unsigned char)out->serial[n - 1])) {
        out->serial[--n] = 0;
    }
    size_t i = 0;
    while (out->serial[i] && isspace((unsigned char)out->serial[i])) {
        i++;
    }
    if (i > 0) {
        memmove(out->serial, out->serial + i, strlen(out->serial + i) + 1);
    }
    for (char *p = out->serial; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
}

esp_err_t bambu_profile_load(bambu_lan_config_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = sizeof(out->printer_ip);
    nvs_get_str(h, "ip", out->printer_ip, &len);
    len = sizeof(out->serial);
    nvs_get_str(h, "sn", out->serial, &len);
    len = sizeof(out->access_code);
    nvs_get_str(h, "code", out->access_code, &len);
    len = sizeof(out->nickname);
    nvs_get_str(h, "name", out->nickname, &len);
    uint8_t model_u8 = 0;
    if (nvs_get_u8(h, "model", &model_u8) == ESP_OK) {
        out->model = bambu_model_from_nvs(model_u8);
    } else {
        out->model = BAMBU_MODEL_P1P;
    }
    nvs_close(h);
    profile_normalize(out);
    return ESP_OK;
}

esp_err_t bambu_profile_save(const bambu_lan_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err=nvs_set_str(h,"ip",cfg->printer_ip);
    if(err==ESP_OK)err=nvs_set_str(h,"sn",cfg->serial);
    if(err==ESP_OK)err=nvs_set_str(h,"code",cfg->access_code);
    if(err==ESP_OK)err=nvs_set_str(h,"name",cfg->nickname);
    if(err==ESP_OK)err=nvs_set_u8(h,"model",bambu_model_to_nvs(cfg->model));
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t bambu_profile_start_saved(void)
{
    bambu_lan_config_t cfg;
    bambu_profile_load(&cfg);
    if (!cfg.printer_ip[0] || !cfg.serial[0] || !cfg.access_code[0]) {
        return ESP_ERR_NOT_FOUND;
    }
    bambu_service_init();
    bambu_camera_init();
    return bambu_service_apply_lan(&cfg);
}
