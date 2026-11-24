#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "lwip/netif.h"
typedef struct netif esp_netif_t;
typedef struct { struct { uint32_t addr; } ip, netmask, gw; } esp_netif_ip_info_t;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
esp_err_t esp_netif_get_ip_info(esp_netif_t *iface, esp_netif_ip_info_t *info);
