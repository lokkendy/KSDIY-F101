#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    int status;
    char *body; /* caller frees; NULL on failure */
} bambu_https_response_t;

/* Fixed Bambu China API host. No redirects. */
esp_err_t bambu_https_cn(const char *path, const char *json,
                         const char *bearer, bambu_https_response_t *out);
