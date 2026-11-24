/* Bambu WiFi management backed by the SDK AIC8800D40L SDIO host driver. */
#include "HAL_WiFi.h"
#include "data_bridge.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "wifi.h"
#include "wifi_driver_event.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "wlan_if.h"
#include "plat_config.h"
#include "hal_sdhost.h"
#include "sdmmc.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "hal_gpio.h"
#include "lwip/inet.h"
#include <stdlib.h>
#include <string.h>

static SemaphoreHandle_t s_lock;
static int s_init;
static volatile bool s_scanning, s_connecting;
static volatile wifi_state_t s_state;
static char s_ssid[33];

static struct netif *station_netif(void)
{
    for (struct netif *n = netif_list; n; n = n->next)
        if (n->name[0] != 'l' && netif_is_up(n) && netif_is_link_up(n) && !ip4_addr_isany_val(*netif_ip4_addr(n))) return n;
    return NULL;
}

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key)
{
    (void)key;
    return station_netif();
}
esp_err_t esp_netif_get_ip_info(esp_netif_t *n, esp_netif_ip_info_t *info)
{
    if (!n || !info) return ESP_ERR_INVALID_ARG;
    info->ip.addr = netif_ip4_addr(n)->addr;
    info->netmask.addr = netif_ip4_netmask(n)->addr;
    info->gw.addr = netif_ip4_gw(n)->addr;
    return ESP_OK;
}

static void publish_network(void)
{
    struct netif *n = station_netif();
    bool now = n != NULL;
    bool was = s_state == WIFI_STATE_CONNECTED;
    if (now) {
        char ip[16]; ip4addr_ntoa_r(netif_ip4_addr(n), ip, sizeof(ip));
        s_state = WIFI_STATE_CONNECTED;
        bridge_set_network(true, -127, ip, s_ssid);
        if (!was) { bridge_post_event(WIFI_EVT_CONNECTED, 0); ESP_LOGI("wifi", "DHCP ready: %s", ip); }
    } else if (was) {
        s_state = WIFI_STATE_OFF;
        bridge_set_network(false, -127, "", s_ssid);
        bridge_post_event(WIFI_EVT_LOST, 0);
    }
}
static void network_monitor(void *arg)
{
    (void)arg;
    for (;;) { publish_network(); vTaskDelay(pdMS_TO_TICKS(1000)); }
}
static void dhcp_on_tcpip(void *unused)
{
    (void)unused;
    struct netif *n = netif_find("wl1");
    if (!n) { ESP_LOGE("wifi", "STA associated but wl1 is missing"); return; }
    netif_set_up(n);
    netif_set_link_up(n);
    netif_set_default(n);
    err_t rc = dhcp_start(n);
    ESP_LOGI("wifi", "DHCP start: %d", rc);
}
static int driver_event(const wifi_drv_event *event)
{
    if (event && event->type == WIFI_DRV_EVENT_NET_DEVICE)
        ESP_LOGI("wifi", "AIC8800 driver event: %d", event->node.dev_event.drv_status);
    if (event && event->type == WIFI_DRV_EVENT_STA &&
        event->node.sta_event.event_type == WIFI_STA_EVENT_ON_ASSOC)
        tcpip_callback(dhcp_on_tcpip, NULL);
    return 0;
}

/* SDK-native SDIO enumeration replaces the absent vendor platform helper. */
int sdio_controller_init(int id)
{
    SDC_InitTypeDef hc = {.debug_mask = 0x3c, .dma_use = 1, .pwr_mode = POWER_MODE_330};
#ifdef CONFIG_DETECT_CARD
    hc.cd_mode = CARD_ALWAYS_PRESENT;
#endif
    struct mmc_host *host = hal_sdc_create(id, &hc);
    if (!host || !hal_sdc_init(host)) return -1;
    SDCard_InitTypeDef cc = {.debug_mask = 0x3c, .type = MMC_TYPE_SDIO};
    if (mmc_card_create(id, &cc)) return -1;
    struct mmc_card *card = mmc_card_open(id);
    if (!card) return -1;
    int rc = mmc_rescan(card, id);
    mmc_card_close(id);
    return rc;
}

const char *HAL_WiFi_ReasonText(int reason)
{
    switch (reason) {
    case ESP_ERR_NOT_FOUND: return "未检测到 AIC8800，请检查 SDIO";
    case ESP_ERR_TIMEOUT: return "连接超时，请检查信号和密码";
    default: return "Wi-Fi 连接失败，请检查网络";
    }
}
esp_err_t HAL_WiFi_Init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_init) { xSemaphoreGive(s_lock); return s_init > 0 ? ESP_OK : ESP_ERR_NOT_FOUND; }
#ifdef CONFIG_DRIVERS_AIC8800_BT_TEST
    hal_gpio_pinmux_set_function(GPIO_PD8, GPIO_MUXSEL_OUT);
    hal_gpio_set_data(GPIO_PD8, 0);
#endif
    platform_config_init();
    aic_wifi_reset_power();
    int rc = sdio_controller_init(platform_get_sdc_index());
    if (!rc) {
        tcpip_init(NULL, NULL);
        wifi_drv_event_set_cbk(driver_event);
        rc = aic_wifi_init(WIFI_MODE_UNKNOWN, 0, NULL);
    }
    s_init = rc >= 0 ? 1 : -1;
    if (s_init > 0 && xTaskCreate(network_monitor, "wifi_state", 2048, NULL, 3, NULL) != pdPASS) s_init = -1;
    xSemaphoreGive(s_lock);
    if (s_init < 0) { ESP_LOGE("wifi", "AIC8800 SDIO initialization failed (%d)", rc); return ESP_ERR_NOT_FOUND; }
    ESP_LOGI("wifi", "AIC8800 SDIO backend ready");
    return ESP_OK;
}
esp_err_t HAL_WiFi_Connect(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || !password || strlen(password) > 64) return ESP_ERR_INVALID_ARG;
    esp_err_t rc = HAL_WiFi_Init(); if (rc) return rc;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state = WIFI_STATE_CONNECTING;
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    rc = wlan_start_sta((uint8_t *)ssid, (uint8_t *)password, 15000) >= 0 ? ESP_OK : ESP_ERR_TIMEOUT;
    if (!rc) rc = wlan_sta_connect((uint8_t *)ssid, (uint8_t *)password, 15000) >= 0 ? ESP_OK : ESP_ERR_TIMEOUT;
    TickType_t started = xTaskGetTickCount();
    while (!rc && !station_netif() && xTaskGetTickCount()-started < pdMS_TO_TICKS(20000))
        vTaskDelay(pdMS_TO_TICKS(100));
    if (!rc && !station_netif()) rc = ESP_ERR_TIMEOUT;
    if (rc) { s_state = WIFI_STATE_FAILED; bridge_post_event(WIFI_EVT_CONNECT_FAIL, rc); }
    else publish_network();
    xSemaphoreGive(s_lock);
    return rc;
}
typedef struct { char ssid[33], password[65]; } connect_args_t;
static void connect_worker(void *arg)
{
    connect_args_t *a = arg;
    HAL_WiFi_Connect(a->ssid, a->password);
    memset(a->password, 0, sizeof(a->password)); free(a);
    s_connecting = false;
    vTaskDelete(NULL);
}
esp_err_t HAL_WiFi_ConnectAsync(const char *ssid, const char *password)
{
    if (!ssid || !password || !ssid[0] || strlen(ssid) > 32 || strlen(password) > 64) return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL();
    bool busy = s_connecting || s_scanning;
    if (!busy) s_connecting = true;
    taskEXIT_CRITICAL();
    if (busy) return ESP_ERR_INVALID_STATE;
    connect_args_t *a = calloc(1, sizeof(*a));
    if (!a) { s_connecting = false; return ESP_ERR_NO_MEM; }
    snprintf(a->ssid, sizeof(a->ssid), "%s", ssid);
    snprintf(a->password, sizeof(a->password), "%s", password);
    if (xTaskCreate(connect_worker, "wifi_connect", 6144, a, 4, NULL) != pdPASS) {
        free(a); s_connecting = false; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void HAL_WiFi_Disconnect(void)
{
    if (s_init > 0) wlan_disconnect_sta(0);
    s_state = WIFI_STATE_OFF;
    bridge_set_network(false, -127, "", s_ssid);
}
wifi_state_t HAL_WiFi_GetState(void) { return s_state; }
bool HAL_WiFi_IsConnected(void) { return station_netif() != NULL; }
esp_err_t HAL_WiFi_SaveCreds(const char *ssid, const char *password)
{
    if (!ssid || !password || strlen(ssid) > 32 || strlen(password) > 64) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h; esp_err_t rc = nvs_open("wifi", NVS_READWRITE, &h);
    if (rc) return rc;
    rc = nvs_set_str(h, "ssid", ssid);
    if (!rc) rc = nvs_set_str(h, "pass", password);
    if (!rc) rc = nvs_commit(h);
    nvs_close(h); return rc;
}
bool HAL_WiFi_LoadCreds(char *ssid, size_t slen, char *password, size_t plen)
{
    if (!ssid || !password || !slen || !plen) return false;
    ssid[0] = password[0] = 0;
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h)) return false;
    esp_err_t rc = nvs_get_str(h, "ssid", ssid, &slen);
    if (!rc) rc = nvs_get_str(h, "pass", password, &plen);
    nvs_close(h);
    return !rc && ssid[0];
}
void HAL_WiFi_ClearCreds(void)
{
    nvs_handle_t h;
    if (!nvs_open("wifi", NVS_READWRITE, &h)) { nvs_erase_all(h); nvs_commit(h); nvs_close(h); }
}
static void scan_worker(void *arg)
{
    wifi_scan_cb_t callback = (wifi_scan_cb_t)arg;
    wifi_ap_t results[WIFI_SCAN_MAX];
    wifi_ap_list_t list = {0};
    unsigned count = 0;
    esp_err_t rc = HAL_WiFi_Init();
    if (!rc) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        wlan_if_scan(); wlan_if_getscan(&list);
        count = list.ap_count > WIFI_SCAN_MAX ? WIFI_SCAN_MAX : list.ap_count;
        for (unsigned i = 0; i < count; ++i) {
            memcpy(results[i].ssid, list.ap_info[i].ssid, 32); results[i].ssid[32] = 0;
            results[i].rssi = (int8_t)list.ap_info[i].rssi;
            results[i].authmode = 3;
        }
        xSemaphoreGive(s_lock);
    }
    s_scanning = false;
    if (callback) callback(results, count, rc);
    vTaskDelete(NULL);
}
esp_err_t HAL_WiFi_StartScan(wifi_scan_cb_t callback)
{
    taskENTER_CRITICAL(); bool busy = s_scanning || s_connecting;
    if (!busy) s_scanning = true;
    taskEXIT_CRITICAL();
    if (busy) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(scan_worker, "wifi_scan", 4096, (void *)callback, 4, NULL) != pdPASS) { s_scanning = false; return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
bool HAL_WiFi_IsScanning(void) { return s_scanning; }
