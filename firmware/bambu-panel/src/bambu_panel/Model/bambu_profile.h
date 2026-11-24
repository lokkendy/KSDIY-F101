#pragma once

#include "bambu_service.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bambu_profile_load(bambu_lan_config_t *out);
esp_err_t bambu_profile_save(const bambu_lan_config_t *cfg);
esp_err_t bambu_profile_start_saved(void);

#ifdef __cplusplus
}
#endif
