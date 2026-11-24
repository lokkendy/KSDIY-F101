/* Bambu China cloud session and passive printer telemetry. */
#include "bambu_cloud.h"
#include "bambu_https.h"
#include "bambu_cJSON.h"
#include "bambu_status_stream.h"
#include "esp_netif_sntp.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "HAL_WiFi.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TOKEN_MAX 4096
#define LOGIN_PATH "/v1/user-service/user/login"
#define EMAIL_PATH "/v1/user-service/user/sendemail/code"
#define SMS_PATH "/v1/user-service/user/sendsmscode"
#define DEVICE_PATH "/v1/iot-service/api/user/bind"
#define UID_PATH "/v1/design-user-service/my/preference"
#define SESSION_KEY "auth_cn_v1"
#define US_PER_SECOND 1000000LL
#define SNAPSHOT_INTERVAL (300 * US_PER_SECOND)
#define SNAPSHOT_PACING US_PER_SECOND
#define STATUS_FRESH_TIME (90 * US_PER_SECOND)
#define SUBSCRIBE_TIMEOUT (12 * US_PER_SECOND)

typedef struct {
    unsigned version;
    char selected[41];
    char token[TOKEN_MAX];
} session_t;

typedef enum { SEND_CODE, VERIFY_CODE, REFRESH, SELECT, SIGN_OUT } command_kind_t;
typedef struct {
    command_kind_t kind;
    unsigned index, revision;
    char account[BAMBU_CLOUD_ACCOUNT_MAX];
    char code[BAMBU_CLOUD_CODE_MAX];
} command_t;

static SemaphoreHandle_t lock;
static QueueHandle_t commands;
static bambu_cloud_view_t view;
static bambu_cloud_device_t devices[BAMBU_CLOUD_DEVICE_MAX];
static session_t session;
static char media_codes[BAMBU_CLOUD_DEVICE_MAX][9];
static bambu_lan_config_t media_profile;
static char username[32];
static char challenge_account[BAMBU_CLOUD_ACCOUNT_MAX];
static int64_t resend_at, challenge_until;
static esp_mqtt_client_handle_t mqtt;
static bool mqtt_ready, mqtt_ended, auth_refused;
static int64_t next_snapshot, subscribe_deadline;
static int subscribe_id;
typedef enum { SUB_NEW, SUB_PENDING, SUB_ACTIVE, SUB_DENIED } subscribe_state_t;

typedef struct {
    char serial[sizeof(devices[0].serial)];
    bambu_status_t status;
    int64_t last_report;
    uint32_t media_ip;
    subscribe_state_t subscription;
    bool fresh_session;
    bool snapshot_sent;
} printer_report_t;

static printer_report_t reports[BAMBU_CLOUD_DEVICE_MAX];
static printer_report_t *pending_subscription;
static bool time_task_started;
static struct {
    char serial[sizeof(devices[0].serial)];
    int64_t next_allowed;
} snapshot_requests[BAMBU_CLOUD_DEVICE_MAX];

static void wipe(void *ptr, size_t len)
{
    volatile unsigned char *p = ptr;
    while (len--) *p++ = 0;
}

static bool token_valid(const char *token)
{
    if (!token || !*token || strlen(token) >= TOKEN_MAX) return false;
    for (const unsigned char *p = (const unsigned char *)token; *p; ++p) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || strchr("._~+/=-", *p)) continue;
        return false;
    }
    return true;
}

static bool digits(const char *s, size_t min, size_t max)
{
    size_t len = strlen(s);
    return len >= min && len <= max && strspn(s, "0123456789") == len;
}

static bool serial_valid(const char *s)
{
    static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    return *s && strlen(s) < sizeof(devices[0].serial) && strspn(s, chars) == strlen(s);
}

static bool account_valid(const char *s)
{
    if (!s || !*s || strlen(s) >= BAMBU_CLOUD_ACCOUNT_MAX) return false;
    const char *at = strchr(s, '@');
    if (!at) {
        const char *phone = !strncmp(s, "+86", 3) ? s + 3 : s;
        return phone[0] == '1' && digits(phone, 11, 11);
    }
    if (at == s || !at[1] || strchr(at + 1, '@')) return false;
    for (; *s; ++s)
        if ((unsigned char)*s <= 32 || (unsigned char)*s >= 127) return false;
    return true;
}

static void message_locked(const char *text)
{
    snprintf(view.message, sizeof(view.message), "%s", text);
}

static void message(const char *text)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    message_locked(text);
    xSemaphoreGive(lock);
}

static const char *string_field(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static void display_text(char *out, size_t capacity, const char *s)
{
    size_t n = strlen(s);
    if (n >= capacity) {
        n = capacity - 1;
        while (n && ((unsigned char)s[n] & 0xc0) == 0x80) --n;
    }
    for (size_t i = 0; i < n; ++i)
        out[i] = (unsigned char)s[i] < 32 || s[i] == 127 ? ' ' : s[i];
    out[n] = 0;
}

static int cloud_json(const char *path, cJSON *input, const char *token, cJSON **out)
{
    *out = NULL;
    char *body = input ? cJSON_PrintUnformatted(input) : NULL;
    if (input && !body) return 0;
    bambu_https_response_t response = {0};
    esp_err_t rc = bambu_https_cn(path, body, token, &response);
    if (body) { wipe(body, strlen(body)); cJSON_free(body); }
    if (rc == ESP_OK && response.body && *response.body)
        *out = cJSON_ParseWithOpts(response.body, NULL, true);
    if (response.body) { wipe(response.body, strlen(response.body)); free(response.body); }
    return rc == ESP_OK ? response.status : 0;
}

static bool json_error(const cJSON *root)
{
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    return (error && !cJSON_IsNull(error) && !cJSON_IsFalse(error) &&
            !(cJSON_IsString(error) && !error->valuestring[0])) ||
           (code && !cJSON_IsNull(code) && !(cJSON_IsNumber(code) && code->valuedouble == 0));
}

static void http_error(int status, bool verifying, const cJSON *root)
{
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (verifying && status == 400 && cJSON_IsNumber(code)) {
        if (code->valueint == 1) { message("验证码已过期，请重新获取"); return; }
        if (code->valueint == 2) { message("验证码错误，请重新输入"); return; }
    }
    switch (status) {
    case 400: message(verifying ? "验证码登录未通过，请核对账号和验证码" : "云端拒绝请求，请核对账号"); break;
    case 401: message("云端授权失效，请重新验证码登录"); break;
    case 403: message("中国区云端拒绝此请求（403）"); break;
    case 429: message("请求过于频繁，请稍后再试"); break;
    default: message("拓竹云暂不可用，请稍后重试"); break;
    }
}

static bool commit_session(const session_t *next)
{
    nvs_handle_t handle;
    if (nvs_open("bambu_cloud", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t rc = nvs_set_blob(handle, SESSION_KEY, next, sizeof(*next));
    if (rc == ESP_OK) {
        rc = nvs_commit(handle);
        if (rc != ESP_OK) {
            /* Undo the in-memory edit too. Another module may commit later. */
            if (session.token[0]) nvs_set_blob(handle, SESSION_KEY, &session, sizeof(session));
            else nvs_erase_key(handle, SESSION_KEY);
        }
    }
    nvs_close(handle);
    return rc == ESP_OK;
}

static void load_session(void)
{
    nvs_handle_t handle;
    size_t len = sizeof(session);
    if (nvs_open("bambu_cloud", NVS_READONLY, &handle) != ESP_OK) return;
    esp_err_t rc = nvs_get_blob(handle, SESSION_KEY, &session, &len);
    nvs_close(handle);
    if (rc != ESP_OK || len != sizeof(session) || session.version != 1 ||
        !memchr(session.token, 0, sizeof(session.token)) ||
        !memchr(session.selected, 0, sizeof(session.selected)) ||
        !token_valid(session.token))
        wipe(&session, sizeof(session));
}

static void reset_status(bambu_status_t *status)
{
    bambu_status_reset(status);
    status->nozzle_temp = status->nozzle_target = NAN;
    status->bed_temp = status->bed_target = NAN;
    status->chamber_temp = NAN;
    status->progress_percent = -1;
}

static printer_report_t *report_for(const char *serial)
{
    for (unsigned i = 0; i < BAMBU_CLOUD_DEVICE_MAX; ++i) {
        if (!strcmp(serial, reports[i].serial)) return &reports[i];
    }
    return NULL;
}

static printer_report_t *selected_report(void)
{
    return view.selected < view.count ? report_for(devices[view.selected].serial) : NULL;
}

/* Slot ownership follows serial numbers even when the cloud reorders its catalog. */
static void sync_reports_locked(const bambu_cloud_device_t *catalog, unsigned count, bool new_session)
{
    if (new_session) memset(reports, 0, sizeof(reports));
    for (unsigned slot = 0; slot < BAMBU_CLOUD_DEVICE_MAX; ++slot) {
        printer_report_t *report = &reports[slot];
        if (!report->serial[0]) continue;
        unsigned index = 0;
        while (index < count && strcmp(report->serial, catalog[index].serial)) ++index;
        if (index == count) memset(report, 0, sizeof(*report));
    }
    for (unsigned i = 0; i < count; ++i) {
        if (report_for(catalog[i].serial)) continue;
        printer_report_t *report = report_for("");
        if (!report) break;
        strlcpy(report->serial, catalog[i].serial, sizeof(report->serial));
        reset_status(&report->status);
    }
}

static bool report_is_live(const printer_report_t *report, int64_t now)
{
    return mqtt_ready && report->subscription == SUB_ACTIVE && report->fresh_session &&
           now - report->last_report <= STATUS_FRESH_TIME;
}

static void stop_mqtt(void)
{
    if (mqtt) esp_mqtt_client_destroy(mqtt);
    mqtt = NULL;
    xSemaphoreTake(lock, portMAX_DELAY);
    mqtt_ready = mqtt_ended = auth_refused = false;
    pending_subscription = NULL;
    for (unsigned i = 0; i < BAMBU_CLOUD_DEVICE_MAX; ++i) {
        reports[i].subscription = SUB_NEW;
        reports[i].snapshot_sent = false;
        reports[i].fresh_session = false;
    }
    xSemaphoreGive(lock);
}

static bool request_snapshot_locked(const char *serial, int64_t now)
{
    unsigned index = BAMBU_CLOUD_DEVICE_MAX;
    for (unsigned i = 0; i < BAMBU_CLOUD_DEVICE_MAX; ++i) {
        if (!strcmp(snapshot_requests[i].serial, serial)) {
            if (now < snapshot_requests[i].next_allowed) return false;
            index = i;
            break;
        }
        if (index == BAMBU_CLOUD_DEVICE_MAX && now >= snapshot_requests[i].next_allowed)
            index = i;
    }
    if (index == BAMBU_CLOUD_DEVICE_MAX ||
        f101_mqtt_request_bambu_snapshot(mqtt, serial) < 0) return false;
    strcpy(snapshot_requests[index].serial, serial);
    snapshot_requests[index].next_allowed = now + SNAPSHOT_INTERVAL;
    return true;
}

/* One SUBACK at a time; printer queries share one paced queue and TLS session. */
static void service_reports_locked(int64_t now)
{
    if (!mqtt_ready || !view.count) return;
    if (pending_subscription && now >= subscribe_deadline) {
        mqtt_ready = false;
        mqtt_ended = true;
        return;
    }
    for (unsigned offset = 0; offset < view.count; ++offset) {
        const char *serial = devices[(view.selected + offset) % view.count].serial;
        printer_report_t *report = report_for(serial);
        if (!report) continue;
        if (!pending_subscription && report->subscription == SUB_NEW) {
            char topic[64];
            snprintf(topic, sizeof(topic), "device/%s/report", serial);
            int id = esp_mqtt_client_subscribe(mqtt, topic, 0);
            if (id > 0) {
                pending_subscription = report;
                report->subscription = SUB_PENDING;
                subscribe_id = id;
                subscribe_deadline = now + SUBSCRIBE_TIMEOUT;
            }
        }
        if (report->subscription == SUB_ACTIVE && !report->snapshot_sent &&
            now >= next_snapshot && request_snapshot_locked(serial, now)) {
            report->snapshot_sent = true;
            next_snapshot = now + SNAPSHOT_PACING;
        }
    }
}

static printer_report_t *report_from_topic(const char *topic, int length)
{
    static const char prefix[] = "device/";
    static const char suffix[] = "/report";
    enum { prefix_len = sizeof(prefix) - 1, suffix_len = sizeof(suffix) - 1 };
    if (!topic || length <= prefix_len + suffix_len ||
        memcmp(topic, prefix, prefix_len) ||
        memcmp(topic + length - suffix_len, suffix, suffix_len)) return NULL;
    const size_t serial_len = (size_t)(length - prefix_len - suffix_len);
    if (serial_len >= sizeof(reports[0].serial)) return NULL;
    for (unsigned i = 0; i < BAMBU_CLOUD_DEVICE_MAX; ++i) {
        if (strlen(reports[i].serial) == serial_len &&
            !memcmp(reports[i].serial, topic + prefix_len, serial_len)) return &reports[i];
    }
    return NULL;
}

/* Printer network information arrives in a full report on its own topic. */
static void update_media_ip_locked(printer_report_t *report, const char *body, size_t length)
{
    static const char net_key[] = "\"net\"";
    const size_t key_len = sizeof(net_key) - 1;
    size_t offset = 0;
    while (offset + key_len <= length && memcmp(body + offset, net_key, key_len)) ++offset;
    if (offset + key_len > length) return;

    esp_netif_ip_info_t station;
    esp_netif_t *iface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!iface || esp_netif_get_ip_info(iface, &station) != ESP_OK || !station.netmask.addr)
        return;

    cJSON *root = cJSON_ParseWithLength(body, length);
    const cJSON *print = cJSON_GetObjectItemCaseSensitive(root, "print");
    const cJSON *net = cJSON_GetObjectItemCaseSensitive(print, "net");
    const cJSON *info = cJSON_GetObjectItemCaseSensitive(net, "info");
    if (cJSON_IsArray(info)) {
        report->media_ip = 0;
        const cJSON *entry;
        cJSON_ArrayForEach(entry, info) {
            const cJSON *ip = cJSON_GetObjectItemCaseSensitive(entry, "ip");
            if (!cJSON_IsNumber(ip) || ip->valuedouble < 1.0 || ip->valuedouble > 4294967295.0)
                continue;
            uint32_t value = (uint32_t)ip->valuedouble;
            if ((double)value != ip->valuedouble || value == station.ip.addr ||
                (value & 0xff) >= 224 || !(value & 0xff) ||
                ((value ^ station.ip.addr) & station.netmask.addr)) continue;
            report->media_ip = value;
            break;
        }
    }
    cJSON_Delete(root);
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    const esp_mqtt_event_t *event = data;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (id == MQTT_EVENT_CONNECTED) {
        mqtt_ready = true;
        next_snapshot = esp_timer_get_time();
        message_locked("已连接，正在订阅公网状态");
    } else if (id == MQTT_EVENT_SUBSCRIBED && pending_subscription &&
               event->msg_id == subscribe_id) {
        pending_subscription->subscription = event->subscribe_denied ? SUB_DENIED : SUB_ACTIVE;
        pending_subscription = NULL;
    } else if (id == MQTT_EVENT_ERROR || id == MQTT_EVENT_DISCONNECTED) {
        mqtt_ready = false;
        mqtt_ended = true;
        if (event->error_handle &&
            event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED &&
            event->error_handle->connect_return_code >= 4) auth_refused = true;
    } else if (id == MQTT_EVENT_DATA && mqtt_ready && event->data_len > 0) {
        printer_report_t *report = report_from_topic(event->topic, event->topic_len);
        if (report && (report->subscription == SUB_ACTIVE ||
                       report->subscription == SUB_PENDING)) {
            bambu_status_stream_t stream;
            bambu_status_stream_begin(&stream, (size_t)event->data_len);
            if (bambu_status_stream_feed(&stream, event->data, (size_t)event->data_len) &&
                bambu_status_stream_finish(&stream, &report->status)) {
                update_media_ip_locked(report, event->data, (size_t)event->data_len);
                report->last_report = esp_timer_get_time();
                report->fresh_session = true;
                if (report == selected_report()) message_locked("公网监控 · 已收到状态");
            }
        }
    }
    xSemaphoreGive(lock);
}

static bool start_mqtt(void)
{
    if (!view.count || !username[0]) return false;
    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = "mqtts://cn.mqtt.bambulab.com:8883",
        .credentials = {.username = username, .authentication.password = session.token},
        .session = {.keepalive = 45, .protocol_ver = MQTT_PROTOCOL_V_3_1_1},
        .network.timeout_ms = 12000,
        .buffer.size = BAMBUSTREAM_MAX_TOTAL,
        .read_only = true,
    };
    mqtt = esp_mqtt_client_init(&cfg);
    if (mqtt &&
        esp_mqtt_client_register_event(mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL) == ESP_OK &&
        esp_mqtt_client_start(mqtt) == ESP_OK) return true;
    stop_mqtt();
    return false;
}

static int fetch_devices(const char *token, bambu_cloud_device_t *out,
                         char codes[BAMBU_CLOUD_DEVICE_MAX][9], unsigned *used, bool *truncated)
{
    cJSON *root;
    int status = cloud_json(DEVICE_PATH, NULL, token, &root);
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "devices");
    *used = 0;
    *truncated = false;
    if (status != 200 || !cJSON_IsArray(list) || json_error(root)) {
        cJSON_Delete(root);
        return status == 200 ? 0 : status;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        const char *serial = string_field(item, "dev_id");
        if (!serial_valid(serial)) continue;
        unsigned index = 0;
        while (index < *used && strcmp(serial, out[index].serial)) ++index;
        if (index == BAMBU_CLOUD_DEVICE_MAX) { *truncated = true; continue; }
        if (index == *used) ++*used;
        bambu_cloud_device_t *d = &out[index];
        strcpy(d->serial, serial);
        display_text(d->name, sizeof(d->name), string_field(item, "name"));
        const char *model = string_field(item, "dev_product_name");
        if (!*model) model = string_field(item, "dev_model_name");
        display_text(d->model, sizeof(d->model), model);
        const char *code = string_field(item, "dev_access_code");
        size_t length = strspn(code, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        if (length == 8 && code[length + strspn(code + length, " \r\n\t")] == 0)
            memcpy(codes[index], code, length);
        if (*code) wipe((char *)code, strlen(code));
        display_text(d->state, sizeof(d->state), string_field(item, "print_status"));
        const cJSON *online = cJSON_GetObjectItemCaseSensitive(item, "online");
        d->online_known = cJSON_IsBool(online);
        d->online = cJSON_IsTrue(online);
    }
    cJSON_Delete(root);
    return 200;
}

static int fetch_username(const char *token, char *out)
{
    cJSON *root;
    int status = cloud_json(UID_PATH, NULL, token, &root);
    const cJSON *uid = cJSON_GetObjectItemCaseSensitive(root, "uid");
    char decimal[24] = "";
    const char *id = decimal;
    if (cJSON_IsString(uid)) id = uid->valuestring;
    else if (cJSON_IsNumber(uid) && uid->valuedouble >= 1 && uid->valuedouble <= 9007199254740991.0 &&
             (double)(uint64_t)uid->valuedouble == uid->valuedouble)
        snprintf(decimal, sizeof(decimal), "%llu", (unsigned long long)uid->valuedouble);
    if (status == 200 && !json_error(root) && digits(id, 1, 23) && id[0] != '0')
        snprintf(out, sizeof(username), "u_%s", id);
    else if (status == 200) status = 0;
    cJSON_Delete(root);
    return status;
}

static int connect_catalog(const char *token, session_t *candidate)
{
    bambu_cloud_device_t found[BAMBU_CLOUD_DEVICE_MAX] = {0};
    unsigned used;
    bool truncated;
    char user[sizeof(username)] = "";
    char found_codes[BAMBU_CLOUD_DEVICE_MAX][9] = {0};
    int status = fetch_devices(token, found, found_codes, &used, &truncated);
    if (status != 200) return status;
    if (used && (status = fetch_username(token, user)) != 200) return status;
    if (candidate) {
        if (!commit_session(candidate)) { message("授权保存失败，请重试登录"); return -1; }
        memcpy(&session, candidate, sizeof(session));
    }
    stop_mqtt();
    strcpy(username, user);
    unsigned index = 0;
    for (unsigned i = 0; i < used; ++i)
        if (!strcmp(found[i].serial, session.selected)) index = i;
    xSemaphoreTake(lock, portMAX_DELAY);
    sync_reports_locked(found, used, candidate != NULL);
    if (!used || strcmp(found[index].serial, media_profile.serial) ||
        strcmp(found_codes[index], media_profile.access_code))
        wipe(&media_profile, sizeof(media_profile));
    memcpy(media_codes, found_codes, sizeof(media_codes));
    wipe(found_codes, sizeof(found_codes));
    memcpy(devices, found, sizeof(devices));
    view.selected = index;
    view.count = used;
    view.signed_in = true;
    ++view.catalog_revision;
    message_locked(!used ? "已登录，账号下暂无绑定打印机" :
                   truncated ? "已登录，仅显示前 16 台打印机" : "已登录，正在连接公网状态");
    xSemaphoreGive(lock);
    return 200;
}

static void send_code(const char *account)
{
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(lock, portMAX_DELAY);
    bool allowed = now >= resend_at;
    if (allowed) resend_at = now + 60 * US_PER_SECOND;
    xSemaphoreGive(lock);
    if (!allowed) { message("请等待倒计时后重新发送"); return; }
    bool sms = strchr(account, '@') == NULL;
    cJSON *input = cJSON_CreateObject();
    if (!input || !cJSON_AddStringToObject(input, sms ? "phone" : "email", account) ||
        !cJSON_AddStringToObject(input, "type", "codeLogin")) {
        cJSON_Delete(input);
        message("内存不足，验证码未发送");
        return;
    }
    message("正在请求验证码…");
    cJSON *reply;
    int status = cloud_json(sms ? SMS_PATH : EMAIL_PATH, input, NULL, &reply);
    cJSON_Delete(input);
    if ((status == 200 || status == 204) && !json_error(reply)) {
        strcpy(challenge_account, account);
        challenge_until = esp_timer_get_time() + 600 * US_PER_SECOND;
        message(sms ? "请输入手机收到的短信验证码" : "请输入邮箱收到的验证码");
    } else {
        http_error(status, false, reply);
    }
    cJSON_Delete(reply);
}

static int verify_code(const command_t *command)
{
    if (strcmp(challenge_account, command->account) || esp_timer_get_time() > challenge_until) {
        message("请先为此账号获取验证码");
        return -1;
    }
    cJSON *input = cJSON_CreateObject();
    if (!input || !cJSON_AddStringToObject(input, "account", command->account) ||
        !cJSON_AddStringToObject(input, "code", command->code)) {
        cJSON_Delete(input);
        message("内存不足，请重试");
        return -1;
    }
    message("正在验证登录…");
    cJSON *reply;
    int status = cloud_json(LOGIN_PATH, input, NULL, &reply);
    cJSON *code = cJSON_GetObjectItemCaseSensitive(input, "code");
    wipe(code->valuestring, strlen(code->valuestring));
    cJSON_Delete(input);
    const char *token = string_field(reply, "accessToken");
    if (status != 200 || !token_valid(token) || json_error(reply)) {
        http_error(status, true, reply);
        if (*token) wipe((char *)token, strlen(token));
        cJSON_Delete(reply);
        return -1;
    }
    session_t *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) {
        wipe((char *)token, strlen(token));
        cJSON_Delete(reply);
        message("内存不足，授权未保存");
        return -1;
    }
    candidate->version = 1;
    strcpy(candidate->token, token);
    wipe((char *)token, strlen(token));
    cJSON_Delete(reply);
    /* Verify the token against the account catalog before persisting it. */
    status = connect_catalog(candidate->token, candidate);
    wipe(candidate, sizeof(*candidate));
    free(candidate);
    if (status >= 0 && status != 200) http_error(status, false, NULL);
    if (status == 200) wipe(challenge_account, sizeof(challenge_account));
    return status;
}

static bool start_time(void)
{
    if (time_task_started) return true;
    const esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    time_task_started = esp_netif_sntp_init(&cfg) == ESP_OK;
    return time_task_started;
}

static void select_device(const command_t *command)
{
    if (command->revision != view.catalog_revision || command->index >= view.count) return;
    if (command->index == view.selected) return;
    session_t *candidate = malloc(sizeof(*candidate));
    if (!candidate) { message("内存不足，请重试"); return; }
    memcpy(candidate, &session, sizeof(*candidate));
    strcpy(candidate->selected, devices[command->index].serial);
    bool saved = commit_session(candidate);
    if (saved) memcpy(&session, candidate, sizeof(session));
    wipe(candidate, sizeof(*candidate));
    free(candidate);
    if (!saved) { message("打印机选择未能保存"); return; }
    xSemaphoreTake(lock, portMAX_DELAY);
    wipe(&media_profile, sizeof(media_profile));
    view.selected = command->index;
    message_locked("已切换打印机");
    xSemaphoreGive(lock);
}

static bool sign_out(void)
{
    session_t *empty = calloc(1, sizeof(*empty));
    if (!empty) { message("内存不足，退出失败"); return false; }
    bool saved = commit_session(empty);
    free(empty);
    if (!saved) { message("退出未保存，请重试"); return false; }
    stop_mqtt();
    wipe(&session, sizeof(session));
    wipe(challenge_account, sizeof(challenge_account));
    username[0] = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    memset(reports, 0, sizeof(reports));
    wipe(&media_profile, sizeof(media_profile));
    wipe(media_codes, sizeof(media_codes));
    view.count = view.selected = 0;
    view.signed_in = false;
    ++view.catalog_revision;
    message_locked("已退出拓竹云");
    xSemaphoreGive(lock);
    return true;
}

static int64_t retry_at(unsigned *attempt)
{
    static const int seconds[] = {5, 15, 30, 60};
    unsigned index = *attempt;
    if (*attempt < 3) ++*attempt;
    return esp_timer_get_time() + seconds[index] * US_PER_SECOND;
}

static bool wait_for_time(void)
{
    if (esp_netif_sntp_synced()) return true;
    message("正在自动校时…");
    if (start_time() && esp_netif_sntp_sync_wait(12000) == ESP_OK) return true;
    message("自动校时未完成，请稍后重试");
    return false;
}

static void worker(void *arg)
{
    (void)arg;
    bool catalog_due = session.token[0] != 0;
    bool blocked = false;
    unsigned attempt = 0;
    int64_t next_try = 0;
    for (;;) {
        if (!time_task_started && HAL_WiFi_IsConnected()) start_time();
        command_t command = {0};
        if (xQueueReceive(commands, &command, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (command.kind == SIGN_OUT) {
                if (sign_out()) { catalog_due = blocked = false; }
            } else if (command.kind == SELECT) {
                select_device(&command);
                if (!mqtt) next_try = 0;
            } else if (command.kind == REFRESH && session.token[0]) {
                catalog_due = true;
                blocked = false;
                attempt = 0;
                next_try = 0;
            } else if (!HAL_WiFi_IsConnected()) {
                message("请先在设置中连接 Wi-Fi");
            } else if (wait_for_time()) {
                if (command.kind == SEND_CODE) send_code(command.account);
                if (command.kind == VERIFY_CODE && verify_code(&command) == 200) {
                    catalog_due = blocked = false;
                    attempt = 0;
                    next_try = 0;
                }
            }
            wipe(&command, sizeof(command));
            xSemaphoreTake(lock, portMAX_DELAY);
            view.busy = false;
            xSemaphoreGive(lock);
        }

        if (!session.token[0]) continue;
        if (!HAL_WiFi_IsConnected()) {
            if (mqtt) stop_mqtt();
            if (!blocked) { catalog_due = true; attempt = 0; next_try = 0; }
            message("账号已保存，等待 Wi-Fi");
            continue;
        }
        if (!start_time() || !esp_netif_sntp_synced()) {
            message("账号已保存，正在自动校时");
            continue;
        }
        int64_t now = esp_timer_get_time();
        xSemaphoreTake(lock, portMAX_DELAY);
        bool ended = mqtt_ended;
        bool rejected = auth_refused;
        if (mqtt_ready) attempt = 0;
        if (!blocked && !catalog_due) service_reports_locked(now);
        xSemaphoreGive(lock);
        if (ended) {
            stop_mqtt();
            next_try = retry_at(&attempt);
            if (rejected) {
                blocked = true;
                xSemaphoreTake(lock, portMAX_DELAY);
                view.signed_in = false;
                message_locked("MQTT 授权未通过，请重新验证码登录");
                xSemaphoreGive(lock);
            } else message("公网状态连接中断，稍后自动重试");
        }
        if (blocked || now < next_try) continue;
        if (catalog_due) {
            message("正在读取已绑定打印机…");
            int status = connect_catalog(session.token, NULL);
            if (status != 200) {
                if (status == 401 || status == 403) {
                    blocked = true;
                    stop_mqtt();
                    xSemaphoreTake(lock, portMAX_DELAY);
                    /* Only a 401 indicates that credentials are invalid. */
                    if (status == 401) view.signed_in = false;
                    xSemaphoreGive(lock);
                    http_error(status, false, NULL);
                } else {
                    next_try = retry_at(&attempt);
                    message("设备列表读取失败，稍后自动重试");
                }
                continue;
            }
            catalog_due = false;
            attempt = 0;
        }
        if (!mqtt && view.count && !start_mqtt()) {
            next_try = retry_at(&attempt);
            message("公网状态连接失败，稍后自动重试");
        }
    }
}

void bambu_cloud_start(void)
{
    lock = xSemaphoreCreateMutex();
    commands = xQueueCreate(1, sizeof(command_t));
    if (!lock || !commands) return;
    load_session();
    view.signed_in = session.token[0] != 0;
    message_locked(view.signed_in ? "账号已保存，等待联网恢复" : "使用中国区拓竹账号登录");
    if (xTaskCreate(worker, "bambu_cloud", 12288, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(commands);
        commands = NULL;
        message_locked("云服务启动失败");
    }
}

static bool submit(command_t *command)
{
    if (!lock || !commands) { wipe(command, sizeof(*command)); return false; }
    xSemaphoreTake(lock, portMAX_DELAY);
    bool queued = !view.busy && xQueueSend(commands, command, 0) == pdTRUE;
    if (queued) view.busy = true;
    xSemaphoreGive(lock);
    wipe(command, sizeof(*command));
    return queued;
}

bool bambu_cloud_send_code(const char *account)
{
    if (!account_valid(account)) return false;
    command_t command = {.kind = SEND_CODE};
    strcpy(command.account, account);
    return submit(&command);
}

bool bambu_cloud_verify(const char *account, const char *code)
{
    if (!account_valid(account) || !code || !digits(code, 4, 10)) return false;
    command_t command = {.kind = VERIFY_CODE};
    strcpy(command.account, account);
    strcpy(command.code, code);
    return submit(&command);
}

bool bambu_cloud_refresh(void)
{
    command_t command = {.kind = REFRESH};
    return submit(&command);
}

bool bambu_cloud_select(unsigned index, unsigned catalog_revision)
{
    command_t command = {.kind = SELECT, .index = index, .revision = catalog_revision};
    return submit(&command);
}

bool bambu_cloud_sign_out(void)
{
    command_t command = {.kind = SIGN_OUT};
    return submit(&command);
}

bool bambu_cloud_media_credentials(const char *serial, bambu_lan_config_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!lock || !serial || !*serial) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    unsigned i = view.selected;
    bool ok = view.signed_in && i < view.count && media_codes[i][0] &&
              !strcmp(serial, devices[i].serial) && strlen(serial) < sizeof(out->serial);
    if (ok) {
        strlcpy(out->serial, serial, sizeof(out->serial));
        strlcpy(out->nickname, devices[i].name, sizeof(out->nickname));
        strlcpy(out->access_code, media_codes[i], sizeof(out->access_code));
        out->model = bambu_model_from_devmodel(devices[i].model);
        const printer_report_t *report = selected_report();
        uint32_t media_ip = report ? report->media_ip : 0;
        if (media_ip)
            snprintf(out->printer_ip, sizeof(out->printer_ip), "%u.%u.%u.%u",
                     media_ip & 255u, (media_ip >> 8) & 255u, (media_ip >> 16) & 255u, media_ip >> 24);
        out->verify_printer = true;
    }
    xSemaphoreGive(lock);
    return ok;
}

bool bambu_cloud_set_media(const bambu_lan_config_t *cfg)
{
    if (!lock) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = !cfg || (view.signed_in && view.selected < view.count && cfg->verify_printer &&
              !strcmp(cfg->serial, devices[view.selected].serial) &&
              !strcmp(cfg->access_code, media_codes[view.selected]));
    if (ok) {
        wipe(&media_profile, sizeof(media_profile));
        if (cfg) media_profile = *cfg;
    }
    xSemaphoreGive(lock);
    return ok;
}

bool bambu_cloud_copy_media(bambu_lan_config_t *out)
{
    if (!lock) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool valid = view.signed_in && media_profile.printer_ip[0];
    if (valid) *out = media_profile;
    xSemaphoreGive(lock);
    return valid;
}

static void copy_view_locked(bambu_cloud_view_t *out)
{
    memcpy(out, &view, sizeof(*out));
    int64_t now = esp_timer_get_time();
    int64_t remaining = resend_at - now;
    out->cooldown_seconds = remaining > 0 ? (unsigned)((remaining + US_PER_SECOND - 1) / US_PER_SECOND) : 0;
    const printer_report_t *report = selected_report();
    if (!report || !out->signed_in) {
        reset_status(&out->status);
        return;
    }
    out->status = report->status;
    out->live = report_is_live(report, now);
    if (report->subscription == SUB_DENIED && mqtt_ready)
        strlcpy(out->message, "该打印机暂未授权公网状态", sizeof(out->message));
    else if (report->status.has_data && !out->live && mqtt_ready)
        strlcpy(out->message, "近期未收到更新，显示上次状态", sizeof(out->message));

    const bambu_status_t *status = &out->status;
    if (status->state != BAMBU_PRINT_UNKNOWN) out->metrics |= BAMBU_CLOUD_STATE;
    if (status->progress_percent >= 0) out->metrics |= BAMBU_CLOUD_PROGRESS;
    if (status->remaining_minutes >= 0) out->metrics |= BAMBU_CLOUD_REMAINING;
    if (isfinite(status->nozzle_temp)) out->metrics |= BAMBU_CLOUD_NOZZLE;
    if (isfinite(status->bed_temp)) out->metrics |= BAMBU_CLOUD_BED;
    if (isfinite(status->chamber_temp)) out->metrics |= BAMBU_CLOUD_CHAMBER;
}

void bambu_cloud_selected_snapshot(bambu_cloud_view_t *out, bambu_cloud_device_t *selected)
{
    memset(selected, 0, sizeof(*selected));
    if (!lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    copy_view_locked(out);
    if (view.selected < view.count) *selected = devices[view.selected];
    xSemaphoreGive(lock);
}

void bambu_cloud_snapshot(bambu_cloud_view_t *out,
                          bambu_cloud_device_t list[BAMBU_CLOUD_DEVICE_MAX])
{
    if (!lock) {
        memset(out, 0, sizeof(*out));
        memset(list, 0, sizeof(devices));
        snprintf(out->message, sizeof(out->message), "云服务尚未启动");
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    copy_view_locked(out);
    memcpy(list, devices, sizeof(devices));
    xSemaphoreGive(lock);
}

void bambu_cloud_fleet_snapshot(bambu_cloud_fleet_t *out)
{
    if (!lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    out->signed_in = view.signed_in;
    out->busy = view.busy;
    out->count = view.signed_in ? view.count : 0;
    out->selected = view.selected;
    out->catalog_revision = view.catalog_revision;
    const int64_t now = esp_timer_get_time();
    for (unsigned i = 0; i < out->count; ++i) {
        bambu_cloud_fleet_device_t *device = &out->devices[i];
        const printer_report_t *report = report_for(devices[i].serial);
        *device = (bambu_cloud_fleet_device_t){
            .device = devices[i],
            .state = report ? report->status.state : BAMBU_PRINT_UNKNOWN,
            .progress_percent = report ? report->status.progress_percent : -1,
            .remaining_minutes = report ? report->status.remaining_minutes : -1,
            .has_data = report && report->status.has_data,
            .live = report && report_is_live(report, now),
            .subscription_denied = report && mqtt_ready && report->subscription == SUB_DENIED,
        };
    }
    xSemaphoreGive(lock);
}
