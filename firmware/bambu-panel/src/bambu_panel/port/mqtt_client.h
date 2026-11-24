/* ESP MQTT subset implemented using the SDK's Eclipse Paho packet codec. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef const char *esp_event_base_t;
typedef enum { MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_SUBSCRIBED,
               MQTT_EVENT_DATA, MQTT_EVENT_ERROR } esp_mqtt_event_id_t;
#define ESP_EVENT_ANY_ID (-1)
#define MQTT_PROTOCOL_V_3_1_1 4
#define MQTT_ERROR_TYPE_CONNECTION_REFUSED 2
typedef struct f101_mqtt_client *esp_mqtt_client_handle_t;
typedef struct { int error_type; int connect_return_code; } esp_mqtt_error_codes_t;
typedef struct {
    int msg_id;
    bool subscribe_denied;
    char *topic;
    int topic_len;
    char *data;
    int data_len, total_data_len, current_data_offset;
    esp_mqtt_error_codes_t *error_handle;
} esp_mqtt_event_t;
typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;
typedef struct {
    struct { struct { const char *uri; } address;
             struct { bool skip_cert_common_name_check; } verification; } broker;
    struct { const char *username;
             struct { const char *password; } authentication; } credentials;
    struct { int keepalive, protocol_ver; } session;
    struct { int timeout_ms, refresh_connection_after_ms; } network;
    struct { int size; } buffer;
    bool read_only;
} esp_mqtt_client_config_t;
typedef void (*esp_mqtt_event_cb_t)(void *, esp_event_base_t, int32_t, void *);
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, int id,
                                       esp_mqtt_event_cb_t cb, void *arg);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic,
                            const char *data, int len, int qos, int retain);
/* Fixed, non-retained read-only status query for the Bambu China broker. */
int f101_mqtt_request_bambu_snapshot(esp_mqtt_client_handle_t client, const char *serial);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos);
