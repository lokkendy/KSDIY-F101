/* Native task + TLS transport; preserves real MQTT connection/error/ACK events. */
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "MQTTPacket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int f101_mbedtls_entropy(unsigned char *output, size_t length);

static void wipe_bytes(void *ptr, size_t length)
{
    volatile unsigned char *p = ptr;
    while (length--) *p++ = 0;
}

typedef struct { unsigned char *bytes; int length; } mqtt_tx_t;
struct f101_mqtt_client {
    char host[64], username[32];
    char *password;
    bool read_only;
    int port, timeout_ms, keepalive, capacity;
    bool insecure;
    volatile bool stop, connected;
    TaskHandle_t task;
    SemaphoreHandle_t done, mutex;
    QueueHandle_t tx;
    esp_mqtt_event_cb_t callback;
    void *callback_arg;
    esp_tls_t *tls;
    unsigned short next_id;
};

static void notify(esp_mqtt_client_handle_t c, int id, esp_mqtt_event_t *event)
{
    esp_mqtt_event_t empty = {0};
    if (c->callback) c->callback(c->callback_arg, "mqtt", id, event ? event : &empty);
}

/* Read exactly one MQTT packet, retaining the variable-length header. An idle
 * timeout returns zero; truncated/oversized packets are connection failures. */
static int read_exact(esp_mqtt_client_handle_t c, unsigned char *buf, int count, bool idle)
{
    int done = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)c->timeout_ms * 1000;
    while (done < count && !c->stop) {
        int n = esp_tls_conn_read(c->tls, buf + done, count - done);
        if (n > 0) { done += n; continue; }
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (idle && done == 0) return 0;
            if (esp_timer_get_time() < deadline) continue;
        }
        return -1;
    }
    return done == count ? done : -1;
}
static int read_packet(esp_mqtt_client_handle_t c, unsigned char *buf)
{
    int n = read_exact(c, buf, 1, true);
    if (n <= 0) return n;
    unsigned remaining = 0, multiplier = 1;
    int header = 1;
    for (int i = 0; i < 4; ++i) {
        if (read_exact(c, buf + header, 1, false) != 1) return -1;
        unsigned char v = buf[header++];
        remaining += (v & 127u) * multiplier;
        if (!(v & 128u)) {
            if (remaining > (unsigned)(c->capacity - header)) return -1;
            if (remaining && read_exact(c, buf + header, remaining, false) != (int)remaining) return -1;
            return header + remaining;
        }
        multiplier *= 128;
    }
    return -1;
}

/* The bundled Paho deserializers ignore buflen. Validate all variable fields
 * before calling them, including the two-byte topic and packet-id lengths. */
static int packet_body(const unsigned char *packet, int size)
{
    if (size < 2) return -1;
    unsigned remain = 0, multiplier = 1;
    for (int i = 1; i < size && i <= 4; ++i) {
        unsigned byte = packet[i];
        remain += (byte & 127u) * multiplier;
        if (!(byte & 128u)) return remain == (unsigned)(size - i - 1) ? i + 1 : -1;
        multiplier *= 128;
    }
    return -1;
}
static bool valid_publish(const unsigned char *packet, int size)
{
    int body = packet_body(packet, size);
    if (body < 0 || size - body < 2) return false;
    unsigned qos = (packet[0] >> 1) & 3;
    unsigned length = ((unsigned)packet[body] << 8) | packet[body + 1];
    if (qos > 1 || !length || length >= 96 || length > (unsigned)(size - body - 2)) return false;
    if (memchr(packet + body + 2, 0, length)) return false;
    body += 2 + length;
    if (qos == 1 && (size - body < 2 || (packet[body] == 0 && packet[body + 1] == 0))) return false;
    return true;
}

static void mqtt_task(void *arg)
{
    esp_mqtt_client_handle_t c = arg;
    unsigned char *rx = malloc(c->capacity);
    bool error_reported = false;
    esp_mqtt_error_codes_t error = {0};
    esp_mqtt_event_t event = {.error_handle = &error};
    c->tls = esp_tls_init();
    if (!rx || !c->tls) goto error;
    esp_tls_cfg_t tls_cfg = {.skip_common_name = c->insecure, .timeout_ms = c->timeout_ms};
    if (esp_tls_conn_new_sync(c->host, strlen(c->host), c->port, &tls_cfg, c->tls) != 1) goto error;
    f101_tls_read_timeout(c->tls, 500);
    MQTTPacket_connectData conn = MQTTPacket_connectData_initializer;
    char client_id[40];
    if (c->read_only) {
        unsigned char nonce[6];
        if (f101_mbedtls_entropy(nonce, sizeof(nonce))) goto error;
        snprintf(client_id, sizeof(client_id), "KSDIY-%02x%02x%02x%02x%02x%02x",
                 nonce[0], nonce[1], nonce[2], nonce[3], nonce[4], nonce[5]);
    } else {
        snprintf(client_id, sizeof(client_id), "F101S3-%08lx", (unsigned long)(uint32_t)esp_timer_get_time());
    }
    conn.MQTTVersion = 4;
    conn.clientID.cstring = client_id;
    conn.username.cstring = c->username;
    conn.password.cstring = c->password;
    conn.keepAliveInterval = c->keepalive;
    conn.cleansession = 1;
    int size = MQTTSerialize_connect(rx, c->capacity, &conn);
    if (size <= 0) goto error;
    ssize_t sent = esp_tls_conn_write(c->tls, rx, size);
    wipe_bytes(rx, (size_t)size);
    if (sent != size) goto error;
    int64_t connect_deadline = esp_timer_get_time() + (int64_t)c->timeout_ms * 1000;
    do { size = read_packet(c, rx); } while (!size && !c->stop && esp_timer_get_time() < connect_deadline);
    unsigned char session = 0, result = 0;
    if (size != 4 || rx[0] != 0x20 || rx[1] != 2 || rx[2] > 1 || rx[3] > 5 || !MQTTDeserialize_connack(&session, &result, rx, size)) goto error;
    if (result) { error.error_type = MQTT_ERROR_TYPE_CONNECTION_REFUSED; error.connect_return_code=result; goto error; }
    c->connected = true;
    notify(c, MQTT_EVENT_CONNECTED, NULL);
    int64_t last_tx = esp_timer_get_time(), last_rx = last_tx;
    while (!c->stop) {
        mqtt_tx_t tx;
        while (xQueueReceive(c->tx, &tx, 0) == pdTRUE) {
            int written = esp_tls_conn_write(c->tls, tx.bytes, tx.length);
            free(tx.bytes);
            if (written != tx.length) goto error;
            last_tx = esp_timer_get_time();
        }
        int64_t now = esp_timer_get_time();
        if (now - last_tx > (int64_t)c->keepalive * 500000) {
            int n = MQTTSerialize_pingreq(rx, c->capacity);
            if (esp_tls_conn_write(c->tls, rx, n) != n) goto error;
            last_tx = now;
        }
        if (now - last_rx > (int64_t)c->keepalive * 1500000) goto error;
        size = read_packet(c, rx);
        if (size < 0) goto error;
        if (!size) continue;
        last_rx = esp_timer_get_time();
        unsigned kind = rx[0] >> 4;
        if (kind == PUBLISH) {
            if (!valid_publish(rx, size)) goto error;
            unsigned char dup, retained;
            int qos, payload_len;
            unsigned short packet_id;
            unsigned char *payload;
            MQTTString topic = MQTTString_initializer;
            if (!MQTTDeserialize_publish(&dup, &qos, &retained, &packet_id,
                                          &topic, &payload, &payload_len, rx, size)) goto error;
            esp_mqtt_event_t data = {.topic = topic.lenstring.data,
                .topic_len = topic.lenstring.len, .data = (char *)payload,
                .data_len = payload_len, .total_data_len = payload_len, .current_data_offset = 0};
            notify(c, MQTT_EVENT_DATA, &data);
            if (qos == 1) {
                unsigned char ack[8]; int count = MQTTSerialize_ack(ack, sizeof(ack), PUBACK, 0, packet_id);
                if (esp_tls_conn_write(c->tls, ack, count) != count) goto error;
            } else if (qos > 1) goto error;
        } else if (kind == SUBACK) {
            int body = packet_body(rx, size);
            if (rx[0] != 0x90 || body < 0 || size - body != 3) goto error;
            unsigned short packet_id;
            int count = 0, granted = 0;
            if (!MQTTDeserialize_suback(&packet_id, 1, &count, &granted, rx, size) ||
                count != 1 || (granted > 2 && granted != 0x80) ||
                (granted == 0x80 && !c->read_only)) goto error;
            esp_mqtt_event_t ack = {.msg_id = packet_id, .subscribe_denied = granted == 0x80};
            notify(c, MQTT_EVENT_SUBSCRIBED, &ack);
        }
    }
    goto cleanup;
error:
    if (!c->stop) { notify(c, MQTT_EVENT_ERROR, &event); error_reported = true; }
cleanup:
    c->connected = false;
    if (!c->stop || error_reported) notify(c, MQTT_EVENT_DISCONNECTED, NULL);
    esp_tls_conn_destroy(c->tls); c->tls = NULL;
    free(rx);
    xSemaphoreGive(c->done);
    vTaskDelete(NULL);
}

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *cfg)
{
    if (!cfg || !cfg->broker.address.uri || cfg->session.protocol_ver != MQTT_PROTOCOL_V_3_1_1) return NULL;
    esp_mqtt_client_handle_t c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (sscanf(cfg->broker.address.uri, "mqtts://%63[^:]:%d", c->host, &c->port) != 2 ||
        c->port < 1 || c->port > 65535) { free(c); return NULL; }
    const char *user = cfg->credentials.username ? cfg->credentials.username : "";
    const char *password = cfg->credentials.authentication.password ? cfg->credentials.authentication.password : "";
    c->read_only = cfg->read_only;
    size_t password_len = strlen(password);
    if (strlen(user) >= sizeof(c->username) || password_len >= 4096 ||
        (c->read_only && cfg->broker.verification.skip_cert_common_name_check)) {
        free(c);
        return NULL;
    }
    strcpy(c->username, user);
    c->password = malloc(password_len + 1);
    if (!c->password) { free(c); return NULL; }
    memcpy(c->password, password, password_len + 1);
    c->insecure = cfg->broker.verification.skip_cert_common_name_check;
    c->timeout_ms = cfg->network.timeout_ms > 0 ? cfg->network.timeout_ms : 15000;
    c->keepalive = cfg->session.keepalive > 0 ? cfg->session.keepalive : 60;
    c->capacity = (cfg->buffer.size > 1024 ? cfg->buffer.size : 4096) + 128;
    if (c->capacity > 65664) { esp_mqtt_client_destroy(c); return NULL; }
    c->done = xSemaphoreCreateBinary(); c->mutex = xSemaphoreCreateMutex();
    c->tx = xQueueCreate(16, sizeof(mqtt_tx_t));
    if (!c->done || !c->mutex || !c->tx) { esp_mqtt_client_destroy(c); return NULL; }
    return c;
}
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t c, int id, esp_mqtt_event_cb_t cb, void *arg)
{
    (void)id;
    if (!c || c->task) return ESP_ERR_INVALID_STATE;
    c->callback = cb; c->callback_arg = arg; return ESP_OK;
}
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t c)
{
    if (!c || c->task) return ESP_ERR_INVALID_STATE;
    return xTaskCreate(mqtt_task, "mqtt_tls", 6144, c, 5, &c->task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t c)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    if (c->task) { c->stop = true; xSemaphoreTake(c->done, portMAX_DELAY); c->task = NULL; }
    return ESP_OK;
}
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t c)
{
    if (!c) return ESP_OK;
    esp_mqtt_client_stop(c);
    mqtt_tx_t tx;
    if (c->tx) { while (xQueueReceive(c->tx, &tx, 0) == pdTRUE) free(tx.bytes); vQueueDelete(c->tx); }
    if (c->done) vSemaphoreDelete(c->done);
    if (c->mutex) vSemaphoreDelete(c->mutex);
    if (c->password) { wipe_bytes(c->password, strlen(c->password)); free(c->password); }
    free(c); return ESP_OK;
}
static int send_request(esp_mqtt_client_handle_t c, const char *topic, const char *data, int len, int qos, int retain, bool subscribe)
{
    if (!c || !c->connected || !topic || qos != 0) return -1;
    if (len < 0 || (len && !data)) return -1;
    if (!subscribe && !len && data) len = strlen(data);
    size_t needed = strlen(topic) + (size_t)len + 16;
    if (needed > 65536) return -1;
    unsigned char *packet = malloc(needed);
    if (!packet) return -1;
    MQTTString name = MQTTString_initializer; name.cstring = (char *)topic;
    xSemaphoreTake(c->mutex, portMAX_DELAY);
    unsigned short id = ++c->next_id;
    if (!id) id = ++c->next_id;
    int size = subscribe ? MQTTSerialize_subscribe(packet, needed, 0, id, 1, &name, &qos)
                         : MQTTSerialize_publish(packet, needed, 0, 0, retain, 0, name, (unsigned char *)data, len);
    mqtt_tx_t tx = {packet, size};
    bool ok = size > 0 && xQueueSend(c->tx, &tx, 0) == pdTRUE;
    xSemaphoreGive(c->mutex);
    if (!ok) { free(packet); return -1; }
    return id;
}
int esp_mqtt_client_publish(esp_mqtt_client_handle_t c, const char *topic, const char *data, int len, int qos, int retain)
{
    if (!c || c->read_only) return -1;
    return send_request(c, topic, data, len, qos, retain, false);
}

int f101_mqtt_request_bambu_snapshot(esp_mqtt_client_handle_t c, const char *serial)
{
    static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static const char query[] =
        "{\"pushing\":{\"sequence_id\":\"1\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}";
    if (!c || !c->read_only || strcmp(c->host, "cn.mqtt.bambulab.com") || c->port != 8883 ||
        !serial || !*serial || strlen(serial) > 40 || strspn(serial, alphabet) != strlen(serial)) return -1;
    char topic[56];
    snprintf(topic, sizeof(topic), "device/%s/request", serial);
    return send_request(c, topic, query, sizeof(query) - 1, 0, 0, false);
}
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t c, const char *topic, int qos)
{ return send_request(c, topic, NULL, 0, qos, 0, true); }
