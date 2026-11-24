#pragma once

#include "bambu_model.h"

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BAMBU_DISCOVERY_MAX_DEVICES 12

typedef struct {
    char ip[16];
    char serial[32];
    char name[48];
    char dev_model[32];
    bambu_model_t model;
} bambu_discovered_t;

/**
 * 被动监听 SSDP（239.255.255.250:1990/2021）并发送 M-SEARCH，合并结果。
 * 须在已连接 WiFi 且拿到 IP 后调用；会阻塞当前任务约 listen_ms。
 */
esp_err_t bambu_discovery_scan(uint32_t listen_ms, bambu_discovered_t *out, size_t *in_out_count);

#ifdef __cplusplus
}
#endif
