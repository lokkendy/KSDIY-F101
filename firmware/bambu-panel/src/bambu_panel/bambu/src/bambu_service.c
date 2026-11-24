/*
 * Bambu Lab LAN MQTT monitor + control (Developer Mode).
 * Status merge via bambu_status_stream (derived from KlipperScreen-esp core).
 */
#include "bambu_service.h"
#include "bambu_studio_api.h"

#include "bambu_ftp.h"   /* BAMBU_FTP_NAME_MAX */

#include "bambu_model.h"
#include "bambu_status_stream.h"

#include "bambu_cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "bambu_svc";

static SemaphoreHandle_t s_lock;
/* Publish borrows the client under this short lock; only worker destroys TLS. */
static SemaphoreHandle_t s_transport_lock;
static bambu_lan_config_t s_requested_cfg;
static bool s_requested_running;
static uint32_t s_auth_failed_gen;
static uint32_t s_active_gen;
static void worker_task(void *arg);
static bambu_svc_snapshot_t s_snap;
static bambu_lan_config_t s_cfg;
static esp_mqtt_client_handle_t s_mqtt;
static TaskHandle_t s_worker;
static volatile uint32_t s_gen;
static volatile bool s_mqtt_teardown;
static uint32_t s_seq;
static volatile uint32_t s_last_seq;
static bambu_status_stream_t s_stream;
static bool s_stream_active;
static int64_t s_last_pushall_us;
static int64_t s_mqtt_connected_us;
static int64_t s_last_report_rx_us;
static bool s_warned_no_report;
static bool s_subscribe_done;
static bool s_sn_mismatch_logged;
static char s_asm_buf[CONFIG_BAMBU_MQTT_BUFFER_SIZE+1];
static int s_asm_total;
static int s_asm_len;

/* ── 命令应答环形记录（最近 8 条 / 90s，供 ack_query / wait_ack / 回调） ── */
enum { BAMBU_ACK_RING = 8 };
typedef struct {
    uint32_t seq;
    bool ok;
    bool pending;
    uint32_t generation;
    TaskHandle_t owner;
    char section[12],command[40];
    char result[24];
    int64_t stamp_us;
} ack_slot_t;
static ack_slot_t s_acks[BAMBU_ACK_RING];
static uint8_t s_ack_wr;
static bambu_ack_cb_t s_ack_cb;
static void *s_ack_cb_ud;

#define BAMBU_ACK_TTL_US (90LL * 1000 * 1000)

static void copy_msg(const char *msg)
{
    if (!msg) {
        msg = "";
    }
    size_t n = strlen(msg);
    if (n >= sizeof(s_snap.message)) {
        n = sizeof(s_snap.message) - 1;
    }
    memcpy(s_snap.message, msg, n);
    s_snap.message[n] = 0;
}

static void set_state(bambu_svc_state_t st, bool connected, const char *msg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_snap.state = st;
    s_snap.connected = connected;
    copy_msg(msg);
    xSemaphoreGive(s_lock);
}

/* ── 命令应答：环形记录 + 查询 + 回调（MQTT 任务写，任意任务读） ── */

static void ack_begin(uint32_t seq,const char *section,const char *command)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    ack_slot_t *slot=&s_acks[s_ack_wr];memset(slot,0,sizeof(*slot));
    slot->seq=seq;slot->pending=true;slot->generation=s_gen;
    slot->owner=xTaskGetCurrentTaskHandle();slot->stamp_us=esp_timer_get_time();
    snprintf(slot->section,sizeof(slot->section),"%s",section);
    snprintf(slot->command,sizeof(slot->command),"%s",command);
    s_ack_wr=(s_ack_wr+1)%BAMBU_ACK_RING;
    xSemaphoreGive(s_lock);
}

static void ack_record(uint32_t seq,const char *section,const char *command,bool ok,const char *result)
{
    if(!seq)return;
    bambu_ack_cb_t cb=NULL;void *ud=NULL;char safe[24];bool matched=false;
    snprintf(safe,sizeof(safe),"%s",result?result:"");
    xSemaphoreTake(s_lock,portMAX_DELAY);
    for(unsigned i=0;i<BAMBU_ACK_RING;++i){
        ack_slot_t *slot=&s_acks[i];
        if(slot->seq!=seq || slot->generation!=s_gen || !slot->pending ||
           strcmp(slot->section,section) || strcmp(slot->command,command))continue;
        slot->pending=false;slot->ok=ok;strlcpy(slot->result,safe,sizeof(slot->result));
        cb=s_ack_cb;ud=s_ack_cb_ud;matched=true;break;
    }
    xSemaphoreGive(s_lock);
    if(matched && cb)cb(seq,ok,safe,ud);
}

static void ack_scan_message(const char *buf,size_t len)
{
    if(!buf || len<16 || !strstr(buf,"\"result\""))return;
    cJSON *root=cJSON_ParseWithLength(buf,len);if(!root)return;
    static const char *const sections[]={"print","info","system","camera","pushing"};
    for(unsigned i=0;i<sizeof(sections)/sizeof(sections[0]);++i){
        cJSON *body=cJSON_GetObjectItemCaseSensitive(root,sections[i]);
        cJSON *js=cJSON_GetObjectItemCaseSensitive(body,"sequence_id");
        cJSON *jr=cJSON_GetObjectItemCaseSensitive(body,"result");
        cJSON *jc=cJSON_GetObjectItemCaseSensitive(body,"command");
        if(!cJSON_IsString(jr) || !jr->valuestring || !cJSON_IsString(jc) || !jc->valuestring)continue;
        uint32_t seq=0;
        if(cJSON_IsString(js) && js->valuestring && js->valuestring[0]){
            char *end=NULL;unsigned long n=strtoul(js->valuestring,&end,10);
            if(!end || *end || !n || n>0xffffffffUL)continue;
            seq=(uint32_t)n;
        }else if(cJSON_IsNumber(js) && js->valuedouble>=1 && js->valuedouble<=4294967295.0 &&
                 js->valuedouble==(double)(uint32_t)js->valuedouble)seq=(uint32_t)js->valuedouble;
        if(seq)ack_record(seq,sections[i],jc->valuestring,strcasecmp(jr->valuestring,"success")==0,jr->valuestring);
    }
    cJSON_Delete(root);
}

static bool sd_print_ready(uint32_t generation)
{
    return generation == s_gen && s_snap.connected && s_snap.printer.has_data &&
           (s_snap.printer.state == BAMBU_PRINT_IDLE || s_snap.printer.state == BAMBU_PRINT_FINISHED ||
            s_snap.printer.state == BAMBU_PRINT_FAILED);
}

static int transport_publish(const char *json, bool require_synced,
                             const uint32_t *print_generation)
{
    if(!json || !s_transport_lock)return -1;
    xSemaphoreTake(s_transport_lock,portMAX_DELAY);
    xSemaphoreTake(s_lock,portMAX_DELAY);
    int id=-1;
    if(s_mqtt && s_cfg.serial[0] && s_requested_running && s_gen==s_active_gen &&
       (!require_synced || s_snap.connected) &&
       (!print_generation || sd_print_ready(*print_generation))){
        char topic[80];snprintf(topic,sizeof(topic),"device/%s/request",s_cfg.serial);
        id=esp_mqtt_client_publish(s_mqtt,topic,json,0,0,0);
    }
    xSemaphoreGive(s_lock);xSemaphoreGive(s_transport_lock);return id;
}
static int publish_request_json(const char *json){return transport_publish(json, false, NULL);}

static void trim_inplace(char *s)
{
    if (!s) {
        return;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = 0;
    }
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') {
        i++;
    }
    if (i > 0) {
        memmove(s, s + i, strlen(s + i) + 1);
    }
}

static void upper_inplace(char *s)
{
    if (!s) {
        return;
    }
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char)(*s - ('a' - 'A'));
        }
    }
}

static void normalize_lan_cfg(bambu_lan_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    trim_inplace(cfg->printer_ip);
    trim_inplace(cfg->serial);
    upper_inplace(cfg->serial);
    trim_inplace(cfg->access_code);
    trim_inplace(cfg->nickname);
}

static bool serial_eq_i(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - ('a' - 'A'));
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - ('a' - 'A'));
        }
        if (ca != cb) {
            return false;
        }
    }
    return *a == *b;
}

/** topic: device/{serial}/report → serial_out */
static bool report_topic_serial(const char *topic, char *serial_out, size_t cap)
{
    if (!topic || !serial_out || cap < 2) {
        return false;
    }
    static const char prefix[] = "device/";
    if (strncmp(topic, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }
    const char *p = topic + (sizeof(prefix) - 1);
    const char *slash = strchr(p, '/');
    if (!slash || strcmp(slash, "/report") != 0) {
        return false;
    }
    size_t n = (size_t)(slash - p);
    if (n == 0 || n >= cap) {
        return false;
    }
    memcpy(serial_out, p, n);
    serial_out[n] = 0;
    return true;
}

static void publish_pushall(void)
{
    int id = publish_request_json(
        "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}");
    if (id < 0) {
        ESP_LOGW(TAG, "pushall publish failed");
    } else {
        ESP_LOGI(TAG, "pushall published msg_id=%d", id);
    }
    s_last_pushall_us = esp_timer_get_time();
}

static uint32_t reserve_sequence(void)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    uint32_t id=++s_seq;if(!id)id=++s_seq;
    xSemaphoreGive(s_lock);return id;
}

esp_err_t bambu_mqtt_publish_root(cJSON *root)
{
    if(!root)return ESP_ERR_INVALID_ARG;
    static const char *const sections[]={"print","system","info","camera","pushing"};
    cJSON *body=NULL;const char *section=NULL;
    for(unsigned i=0;i<sizeof(sections)/sizeof(sections[0]);++i){
        cJSON *item=cJSON_GetObjectItemCaseSensitive(root,sections[i]);
        if(!item)continue;
        if(body || !cJSON_IsObject(item)){cJSON_Delete(root);return ESP_ERR_INVALID_ARG;}
        body=item;section=sections[i];
    }
    if(!body){cJSON_Delete(root);return ESP_ERR_INVALID_ARG;}
    body=cJSON_DetachItemFromObjectCaseSensitive(root,section);cJSON_Delete(root);
    return bambu_mqtt_publish_section(section,body);
}

static esp_err_t publish_section(const char *section, cJSON *body,
                                 const uint32_t *print_generation)
{
    if (!body) {
        return ESP_ERR_INVALID_ARG;
    }
    if(!section || (strcmp(section,"print") && strcmp(section,"system") && strcmp(section,"camera") && strcmp(section,"info") && strcmp(section,"pushing"))){cJSON_Delete(body);return ESP_ERR_INVALID_ARG;}
    if (!s_mqtt || !s_snap.connected) {
        cJSON_Delete(body);
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t id = reserve_sequence();
    char seq[16];
    snprintf(seq, sizeof(seq), "%lu", (unsigned long)id);
    cJSON_AddStringToObject(body, "sequence_id", seq);
    cJSON *jcommand=cJSON_GetObjectItemCaseSensitive(body,"command");
    if(!cJSON_IsString(jcommand) || !jcommand->valuestring || strlen(jcommand->valuestring)>=40){cJSON_Delete(body);return ESP_ERR_INVALID_ARG;}
    char command[40];snprintf(command,sizeof(command),"%s",jcommand->valuestring);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(body);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(root, section, body);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    ack_begin(id,section,command);
    esp_err_t err = ESP_OK;
    if (transport_publish(json, true, print_generation) < 0) {
        err = ESP_FAIL;
        ack_record(id,section,command,false,"not_sent");
    } else {
        s_last_seq = id;
        ESP_LOGI(TAG,"command seq=%lu section=%s op=%s queued",(unsigned long)id,section,command);
    }
    cJSON_free(json);
    return err;
}

esp_err_t bambu_mqtt_publish_section(const char *section, cJSON *body)
{
    return publish_section(section, body, NULL);
}

static esp_err_t publish_print_json_cjson(cJSON *print_obj)
{
    return bambu_mqtt_publish_section("print", print_obj);
}

static esp_err_t publish_print_cmd(const char *command, const char *param)
{
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", command);
    /* Studio：pause/resume/stop 均带 param:"" */
    if (param) {
        cJSON_AddStringToObject(print, "param", param);
    } else if (strcmp(command, "pause") == 0 || strcmp(command, "resume") == 0 ||
               strcmp(command, "stop") == 0) {
        cJSON_AddStringToObject(print, "param", "");
    }
    return publish_print_json_cjson(print);
}

static bool model_is_core_xy(bambu_model_t model)
{
    switch (model) {
    case BAMBU_MODEL_P1P:
    case BAMBU_MODEL_P1S:
    case BAMBU_MODEL_X1:
    case BAMBU_MODEL_X1C:
    case BAMBU_MODEL_X1E:
    case BAMBU_MODEL_H2S:
    case BAMBU_MODEL_H2C:
    case BAMBU_MODEL_H2D:
        return true;
    default:
        return false;
    }
}

static void read_motion_caps(bambu_status_t *st, bool *mqtt_axis, bool *mqtt_home, bool *mqtt_bed)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (st) {
        *st = s_snap.printer;
    }
    xSemaphoreGive(s_lock);
    *mqtt_axis = st->cap_mqtt_axis;
    *mqtt_home = st->cap_mqtt_homing;
    *mqtt_bed = st->cap_mqtt_bed_temp;
}

static bool name_is_project_archive(const char *name)
{
    size_t n = strlen(name);
    if (n > 11 && strcasecmp(name + n - 11, ".gcode.3mf") == 0) {
        return true;
    }
    if (n > 4 && strcasecmp(name + n - 4, ".3mf") == 0) {
        return true;
    }
    return false;
}

int bambu_service_project_plate(const char *filename)
{
    int plate = 0;
    const char *p = strstr(filename, "_plate_");
    if (p && sscanf(p, "_plate_%d", &plate) == 1 && plate >= 1 && plate <= 8)
        return plate;
    return 1;
}

static void merge_report_payload(const char *data, int len, int total_in, int offset)
{
    if (!data || len <= 0) {
        return;
    }
    uint32_t report_generation=s_active_gen;
    /* ESP-MQTT：未分片时 total_data_len 常为 0，须用 data_len */
    int total = total_in;
    if (total <= 0) {
        total = len;
        offset = 0;
    }
    if(total<=0 || total>CONFIG_BAMBU_MQTT_BUFFER_SIZE || offset<0 || len>total || offset>total-len){
        s_stream_active=false;s_asm_len=s_asm_total=0;return;
    }
    if(offset==0){
        bambu_status_stream_begin(&s_stream,(size_t)total);s_stream_active=true;
        s_asm_total=total;s_asm_len=0;
    }
    if(total!=s_asm_total || offset!=s_asm_len){
        s_stream_active=false;s_asm_len=s_asm_total=0;return;
    }
    memcpy(s_asm_buf+offset,data,(size_t)len);s_asm_len+=len;s_asm_buf[s_asm_len]=0;
    /* 完整报文到达即做命令应答关联（先于状态流；纯 ack 报文流解析不改变结论，
     * 流解析失败也不影响应答门控） */
    if (offset + len >= total && s_asm_len == s_asm_total) {
        ack_scan_message(s_asm_buf, (size_t)s_asm_len);
    }
    if (s_stream_active && !bambu_status_stream_feed(&s_stream, data, (size_t)len)) {
        s_stream_active = false;
    }
    if (offset + len < total) {
        return;
    }
    if (s_asm_len != s_asm_total) {
        ESP_LOGW(TAG, "report truncated (%d/%d)", s_asm_len, s_asm_total);
        s_stream_active = false;
        s_asm_len = 0;
        return;
    }
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bambu_status_t merged = s_snap.printer;
    xSemaphoreGive(s_lock);
    bool stream_ok = s_stream_active && bambu_status_stream_finish(&s_stream, &merged);
    bool json_ok = bambu_status_apply_report_json(&merged, s_asm_buf, (size_t)s_asm_len);
    if (!json_ok) {
        json_ok = bambu_status_apply_json(&merged, s_asm_buf, (size_t)s_asm_len);
    }
    if (stream_ok || json_ok) {
        static bool s_logged_sync;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if(report_generation!=s_gen || report_generation!=s_active_gen){xSemaphoreGive(s_lock);s_stream_active=false;s_asm_len=0;return;}
        s_snap.printer = merged;
        if(merged.has_data){s_snap.connected=true;s_snap.state=BAMBU_SVC_CONNECTED;copy_msg("实时状态已更新");}
        xSemaphoreGive(s_lock);
        if (!s_logged_sync) {
            s_logged_sync = true;
            ESP_LOGI(TAG, "status synced (gcode_state=%d progress=%d%%)",
                     (int)merged.state, merged.progress_percent);
        }
    } else {
        ESP_LOGW(TAG, "report parse miss (%d bytes)", s_asm_len);
    }
    s_stream_active = false;
    s_asm_len = 0;
}

static void mqtt_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    uint32_t gen=(uint32_t)(uintptr_t)handler_args;
    /* Delayed callbacks from an old connection cannot overwrite a new profile. */
    if(gen!=s_gen || gen!=s_active_gen)return;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to %s (SN %s)", s_cfg.printer_ip, s_cfg.serial);
        s_mqtt_connected_us = esp_timer_get_time();
        s_last_report_rx_us = 0;
        s_warned_no_report = false;
        s_subscribe_done = false;
        s_sn_mismatch_logged = false;
        set_state(BAMBU_SVC_CONNECTING, false, "已认证，正在同步状态…");
        /* 拓竹 LAN broker 拒绝 device/+/report 通配符，会立刻 EOF 断连 */
        {
            char report[80];
            snprintf(report, sizeof(report), "device/%s/report", s_cfg.serial);
            int sub_id = esp_mqtt_client_subscribe(s_mqtt, report, 0);
            ESP_LOGI(TAG, "subscribe %s (msg_id=%d)", report, sub_id);
        }
        break;
    case MQTT_EVENT_SUBSCRIBED:
        if (!s_subscribe_done) {
            s_subscribe_done = true;
            ESP_LOGI(TAG, "MQTT subscription active (msg_id=%d)", event->msg_id);
            publish_pushall();
            publish_request_json("{\"info\":{\"sequence_id\":\"0\",\"command\":\"get_version\"}}");
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        if(s_auth_failed_gen!=gen)set_state(BAMBU_SVC_NETWORK_ERROR,false,"MQTT 已断开");
        s_mqtt_teardown = true;
        break;
    case MQTT_EVENT_ERROR: {
        int et = event->error_handle ? (int)event->error_handle->error_type : -1;
        ESP_LOGE(TAG, "MQTT error type=%d", et);
        if (et == MQTT_ERROR_TYPE_CONNECTION_REFUSED && event->error_handle &&
            (event->error_handle->connect_return_code==4 || event->error_handle->connect_return_code==5)) {
            s_auth_failed_gen=gen;
            set_state(BAMBU_SVC_AUTH_ERROR,false,"认证失败，请核对当前 LAN 访问码与开发者模式");
        } else {
            set_state(BAMBU_SVC_NETWORK_ERROR, false, "MQTT 连接中断，正在重试…");
        }
        s_mqtt_teardown = true;
        break;
    }
    case MQTT_EVENT_DATA: {
        char topic[96];
        int tlen = event->topic_len;
        if (tlen <= 0 || tlen >= (int)sizeof(topic)) {
            break;
        }
        memcpy(topic, event->topic, (size_t)tlen);
        topic[tlen] = 0;
        char topic_sn[32];
        if (!report_topic_serial(topic, topic_sn, sizeof(topic_sn))) {
            ESP_LOGD(TAG, "ignore topic %s", topic);
            break;
        }
        if (!serial_eq_i(topic_sn,s_cfg.serial)) {
            if(!s_sn_mismatch_logged){s_sn_mismatch_logged=true;ESP_LOGW(TAG,"discard report: serial does not match active printer");}
            break;
        }
        s_last_report_rx_us = esp_timer_get_time();
        if (event->current_data_offset == 0) {
            ESP_LOGI(TAG, "report rx [%s] chunk=%d total=%d", topic_sn, event->data_len,
                     event->total_data_len);
        }
        merge_report_payload(event->data, event->data_len,
                             event->total_data_len, event->current_data_offset);
        break;
    }
    default:
        break;
    }
}

static void mqtt_client_destroy(void)
{
    xSemaphoreTake(s_transport_lock,portMAX_DELAY);
    esp_mqtt_client_handle_t c=s_mqtt;s_mqtt=NULL;
    xSemaphoreGive(s_transport_lock);
    if(c){esp_mqtt_client_stop(c);esp_mqtt_client_destroy(c);}
}

static esp_err_t mqtt_client_start(void)
{
    if (!s_cfg.printer_ip[0] || !s_cfg.serial[0] || !s_cfg.access_code[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    char uri[96];
    snprintf(uri, sizeof(uri), "mqtts://%s:%d", s_cfg.printer_ip, CONFIG_BAMBU_MQTT_PORT);

    esp_mqtt_client_config_t cfg = {
        .broker = {
            .address.uri = uri,
#if CONFIG_BAMBU_LAN_TLS_INSECURE
            .verification.skip_cert_common_name_check = true,
#endif
        },
        .credentials = {
            .username = "bblp",
            .authentication.password = s_cfg.access_code,
        },
        .session = {
            .keepalive = 60,
            .protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        },
        .network = {
            .timeout_ms = 15000,
            .refresh_connection_after_ms = 0,
        },
        .buffer = {
            .size = CONFIG_BAMBU_MQTT_BUFFER_SIZE,
        },
    };

    s_mqtt = esp_mqtt_client_init(&cfg);
    if (!s_mqtt) {
        return ESP_ERR_NO_MEM;
    }
    esp_mqtt_client_register_event(s_mqtt,ESP_EVENT_ANY_ID,mqtt_event,(void*)(uintptr_t)s_active_gen);
    esp_err_t err = esp_mqtt_client_start(s_mqtt);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_mqtt);
        s_mqtt = NULL;
    }
    return err;
}

static void maybe_warn_no_report(void)
{
    if (s_warned_no_report || !s_mqtt || s_mqtt_connected_us <= 0) {
        return;
    }
    int64_t now = esp_timer_get_time();
    if (now - s_mqtt_connected_us < 15LL * 1000 * 1000) {
        return;
    }
    if (s_last_report_rx_us > s_mqtt_connected_us) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool has = s_snap.printer.has_data;
    xSemaphoreGive(s_lock);
    if (has) {
        return;
    }
    s_warned_no_report = true;
    ESP_LOGW(TAG, "15s 内无 report：需 LAN+开发者模式；序列号/访问码须与打印机一致");
    set_state(BAMBU_SVC_CONNECTING, false,
              "未收到状态：请开 LAN 与开发者模式，核对序列号与访问码");
}

static bool generation_sleep(uint32_t gen,unsigned milliseconds)
{
    for(unsigned elapsed=0;elapsed<milliseconds;elapsed+=100){
        if(gen!=s_gen)return false;
        vTaskDelay(pdMS_TO_TICKS(100)?pdMS_TO_TICKS(100):1);
    }
    return gen==s_gen;
}

static void worker_task(void *arg)
{
    (void)arg;
    for(;;){
        xSemaphoreTake(s_lock,portMAX_DELAY);
        bool requested=s_requested_running;uint32_t gen=s_gen;
        bambu_lan_config_t config=s_requested_cfg;
        xSemaphoreGive(s_lock);
        if(!requested){vTaskDelay(pdMS_TO_TICKS(100)?pdMS_TO_TICKS(100):1);continue;}
        s_cfg=config;s_active_gen=gen;
        unsigned backoff_ms=2000;
        while(gen==s_gen){
            if(s_auth_failed_gen==gen){generation_sleep(gen,500);continue;}
            set_state(BAMBU_SVC_CONNECTING,false,"正在连接打印机…");
            s_mqtt_teardown=false; /* Before starting, never erase an early ERROR callback. */
            esp_err_t err=mqtt_client_start();
            if(err==ESP_OK){
                int64_t last_push=esp_timer_get_time();
                while(gen==s_gen && s_mqtt && !s_mqtt_teardown){
                    if(!generation_sleep(gen,500))break;
                    maybe_warn_no_report();
                    uint32_t min_ms=bambu_model_pushall_min_ms(s_cfg.model);
                    if(min_ms<(uint32_t)CONFIG_BAMBU_PUSHALL_MIN_INTERVAL_MS)min_ms=CONFIG_BAMBU_PUSHALL_MIN_INTERVAL_MS;
                    int64_t now=esp_timer_get_time();
                    if(s_snap.connected && now-last_push>=(int64_t)min_ms*1000){publish_pushall();last_push=now;}
                }
            }
            mqtt_client_destroy();
            if(gen!=s_gen)break;
            if(s_auth_failed_gen==gen)continue;
            set_state(BAMBU_SVC_NETWORK_ERROR,false,"连接中断，正在重试…");
            generation_sleep(gen,backoff_ms);
            if(backoff_ms<15000)backoff_ms=backoff_ms*2>15000?15000:backoff_ms*2;
        }
        mqtt_client_destroy();
    }
}

void bambu_service_init(void)
{
    if(s_worker)return;
    if(!s_lock){s_lock=xSemaphoreCreateMutex();bambu_status_reset(&s_snap.printer);s_snap.state=BAMBU_SVC_STOPPED;}
    if(!s_transport_lock)s_transport_lock=xSemaphoreCreateMutex();
    if(!s_lock || !s_transport_lock)return;
    if(xTaskCreate(worker_task,"bambu_mqtt",8192,NULL,5,&s_worker)!=pdPASS)s_worker=NULL;
}

esp_err_t bambu_service_apply_lan(const bambu_lan_config_t *cfg)
{
    bambu_service_init();
    if(!cfg || !s_worker)return cfg?ESP_ERR_NO_MEM:ESP_ERR_INVALID_ARG;
    bambu_lan_config_t next=*cfg;
    if(!memchr(next.printer_ip,0,sizeof(next.printer_ip)) || !memchr(next.serial,0,sizeof(next.serial)) ||
       !memchr(next.access_code,0,sizeof(next.access_code)) || !memchr(next.nickname,0,sizeof(next.nickname)))return ESP_ERR_INVALID_ARG;
    normalize_lan_cfg(&next);
    struct in_addr ip;
    if(inet_pton(AF_INET,next.printer_ip,&ip)!=1 || !ip.s_addr || (ntohl(ip.s_addr)>>24)>=224 || strlen(next.serial)<8 || strlen(next.access_code)!=8)return ESP_ERR_INVALID_ARG;
    for(const char *p=next.serial;*p;++p)if(!isalnum((unsigned char)*p))return ESP_ERR_INVALID_ARG;
    for(const char *p=next.access_code;*p;++p)if((unsigned char)*p<33 || (unsigned char)*p>126)return ESP_ERR_INVALID_ARG;
    if(next.model<=BAMBU_MODEL_UNKNOWN || next.model>=BAMBU_MODEL_COUNT)next.model=BAMBU_MODEL_OTHER;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    s_requested_cfg=next;s_requested_running=true;++s_gen;
    memset(s_acks,0,sizeof(s_acks));bambu_status_reset(&s_snap.printer);
    s_snap.state=BAMBU_SVC_CONNECTING;s_snap.connected=false;copy_msg("正在连接打印机…");
    xSemaphoreGive(s_lock);
    return ESP_OK; /* UI returns immediately; old TLS is closed by worker. */
}

void bambu_service_stop(void)
{
    if(!s_lock)return;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    s_requested_running=false;++s_gen;s_snap.connected=false;s_snap.state=BAMBU_SVC_STOPPED;
    bambu_status_reset(&s_snap.printer);copy_msg("未连接");
    xSemaphoreGive(s_lock);
}

void bambu_service_snapshot(bambu_svc_snapshot_t *out)
{
    if (!out) {
        return;
    }
    bambu_service_init();
    if(!s_lock){memset(out,0,sizeof(*out));return;}
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_snap;
    xSemaphoreGive(s_lock);
}

esp_err_t bambu_service_request_pushall(void)
{
    if (!s_snap.connected) {
        return ESP_ERR_INVALID_STATE;
    }
    int64_t now = esp_timer_get_time();
    uint32_t min_ms = bambu_model_pushall_min_ms(s_cfg.model);
    if (min_ms < (uint32_t)CONFIG_BAMBU_PUSHALL_MIN_INTERVAL_MS) {
        min_ms = (uint32_t)CONFIG_BAMBU_PUSHALL_MIN_INTERVAL_MS;
    }
    int64_t min_us = (int64_t)min_ms * 1000;
    if (s_last_pushall_us > 0 && (now - s_last_pushall_us) < min_us) {
        return ESP_ERR_INVALID_STATE;
    }
    publish_pushall();
    return ESP_OK;
}

esp_err_t bambu_service_pause(void)
{
    return publish_print_cmd("pause", NULL);
}

esp_err_t bambu_service_resume(void)
{
    return publish_print_cmd("resume", NULL);
}

esp_err_t bambu_service_stop_print(void)
{
    return publish_print_cmd("stop", NULL);
}

esp_err_t bambu_service_set_speed_level(int level_1_to_4)
{
    if (level_1_to_4 < 1 || level_1_to_4 > 4) {
        return ESP_ERR_INVALID_ARG;
    }
    char param[4];
    snprintf(param, sizeof(param), "%d", level_1_to_4);
    return publish_print_cmd("print_speed", param);
}

static esp_err_t publish_system_led(const char *node,bool on)
{
    cJSON *body=cJSON_CreateObject();if(!body)return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(body,"command","ledctrl");cJSON_AddStringToObject(body,"led_node",node);
    cJSON_AddStringToObject(body,"led_mode",on?"on":"off");
    cJSON_AddNumberToObject(body,"led_on_time",0);cJSON_AddNumberToObject(body,"led_off_time",0);
    cJSON_AddNumberToObject(body,"loop_times",0);cJSON_AddNumberToObject(body,"interval_time",0);
    return bambu_mqtt_publish_section("system",body);
}

esp_err_t bambu_service_set_chamber_light(bool on)
{
    return publish_system_led("chamber_light", on);
}

esp_err_t bambu_service_set_work_light(bool on)
{
    return publish_system_led("work_light", on);
}

static esp_err_t publish_gcode_param(const char *gcode_multiline)
{
    if (!gcode_multiline || !gcode_multiline[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "gcode_line");
    cJSON_AddStringToObject(print, "param", gcode_multiline);
    return publish_print_json_cjson(print);
}

esp_err_t bambu_service_send_gcode_line(const char *gcode)
{
    return publish_gcode_param(gcode);
}

esp_err_t bambu_service_set_nozzle_target_c(int celsius)
{
    int max_temp=(s_cfg.model==BAMBU_MODEL_P1P || s_cfg.model==BAMBU_MODEL_P1S || s_cfg.model==BAMBU_MODEL_A1 || s_cfg.model==BAMBU_MODEL_A1_MINI)?300:320;
    if (celsius < 0 || celsius > max_temp) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mqtt || !s_snap.connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if(s_cfg.model==BAMBU_MODEL_P1P || s_cfg.model==BAMBU_MODEL_P1S || s_cfg.model==BAMBU_MODEL_A1 || s_cfg.model==BAMBU_MODEL_A1_MINI){
        char line[32];snprintf(line,sizeof(line),"M104 S%d\n",celsius);return publish_gcode_param(line);
    }
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "set_nozzle_temp");
    cJSON_AddNumberToObject(print, "extruder_index", 0);
    cJSON_AddNumberToObject(print, "target_temp", celsius);
    esp_err_t err = publish_print_json_cjson(print);
    if (err != ESP_OK) {
        char line[32];
        snprintf(line, sizeof(line), "M104 S%d\n", celsius);
        return publish_gcode_param(line);
    }
    return err;
}

esp_err_t bambu_service_set_bed_target_c(int celsius)
{
    int max_temp=(s_cfg.model==BAMBU_MODEL_P1P || s_cfg.model==BAMBU_MODEL_P1S || s_cfg.model==BAMBU_MODEL_A1 || s_cfg.model==BAMBU_MODEL_A1_MINI)?100:120;
    if (celsius < 0 || celsius > max_temp) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mqtt || !s_snap.connected) {
        return ESP_ERR_INVALID_STATE;
    }
    bambu_status_t st;
    bool mqtt_axis, mqtt_home, mqtt_bed;
    read_motion_caps(&st, &mqtt_axis, &mqtt_home, &mqtt_bed);
    (void)mqtt_axis;
    (void)mqtt_home;
    if (mqtt_bed) {
        cJSON *print = cJSON_CreateObject();
        if (!print) {
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(print, "command", "set_bed_temp");
        cJSON_AddNumberToObject(print, "temp", celsius);
        return publish_print_json_cjson(print);
    }
    char line[32];
    snprintf(line, sizeof(line), "M140 S%d\n", celsius);
    return publish_gcode_param(line);
}

static bool manual_motion_allowed(bool extrude)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool allowed=s_snap.connected && s_snap.printer.has_data &&
        (s_snap.printer.state==BAMBU_PRINT_IDLE || s_snap.printer.state==BAMBU_PRINT_FINISHED || s_snap.printer.state==BAMBU_PRINT_FAILED);
    if(extrude && s_snap.printer.nozzle_temp<170.0f)allowed=false;
    xSemaphoreGive(s_lock);return allowed;
}

esp_err_t bambu_service_jog_axis(char axis, float delta_mm, int feedrate_mm_min)
{
    if (!isfinite(delta_mm) || delta_mm==0.f || fabsf(delta_mm)>10.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    axis = (char)toupper((unsigned char)axis);
    if (axis != 'X' && axis != 'Y' && axis != 'Z') {
        return ESP_ERR_INVALID_ARG;
    }
    if(!manual_motion_allowed(false))return ESP_ERR_INVALID_STATE;
    if(feedrate_mm_min<=0)feedrate_mm_min=3000;
    if(feedrate_mm_min>6000)return ESP_ERR_INVALID_ARG;
    bambu_status_t st;
    bool mqtt_axis, mqtt_home, mqtt_bed;
    read_motion_caps(&st, &mqtt_axis, &mqtt_home, &mqtt_bed);
    (void)mqtt_home;
    (void)mqtt_bed;

    if (mqtt_axis && (fabsf(delta_mm)==1.f || fabsf(delta_mm)==10.f)) {
        int dir = (delta_mm > 0) ? 1 : -1;
        if (!model_is_core_xy(s_cfg.model)) {
            if (axis == 'Y' || axis == 'Z') {
                dir = -dir;
            }
        }
        char ax[2] = { axis, 0 };
        cJSON *print = cJSON_CreateObject();
        if (!print) {
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(print, "command", "xyz_ctrl");
        cJSON_AddStringToObject(print, "axis", ax);
        cJSON_AddNumberToObject(print, "dir", dir);
        cJSON_AddNumberToObject(print, "mode", fabsf(delta_mm) >= 10.f ? 1 : 0);
        return publish_print_json_cjson(print);
    }

    float value = delta_mm;
    if (!model_is_core_xy(s_cfg.model)) {
        if (axis == 'Y' || axis == 'Z') {
            value = -value;
        }
    }
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "M211 S \nM211 X1 Y1 Z1\nM1002 push_ref_mode\nG91 \nG1 %c%0.1f F%d\nM1002 pop_ref_mode\nM211 R\n",
             axis, (double)value, feedrate_mm_min);
    return publish_gcode_param(cmd);
}

esp_err_t bambu_service_home_axes(void)
{
    if(!manual_motion_allowed(false))return ESP_ERR_INVALID_STATE;
    bambu_status_t st;
    bool mqtt_axis, mqtt_home, mqtt_bed;
    read_motion_caps(&st, &mqtt_axis, &mqtt_home, &mqtt_bed);
    (void)mqtt_axis;
    (void)mqtt_bed;

    if (mqtt_home) {
        cJSON *print = cJSON_CreateObject();
        if (!print) {
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(print, "command", "back_to_center");
        return publish_print_json_cjson(print);
    }
    bool printing = (st.state == BAMBU_PRINT_RUNNING || st.state == BAMBU_PRINT_PAUSED ||
                     st.state == BAMBU_PRINT_PREPARE);
    if (printing) {
        return publish_gcode_param("G28 X\n");
    }
    return publish_gcode_param("G28 \n");
}

esp_err_t bambu_service_extrude_mm(float length_mm)
{
    if (!isfinite(length_mm) || length_mm==0.f || length_mm>50.f || length_mm< -50.f) {
        return ESP_ERR_INVALID_ARG;
    }
    if(!manual_motion_allowed(true))return ESP_ERR_INVALID_STATE;
    if(s_cfg.model==BAMBU_MODEL_P1P || s_cfg.model==BAMBU_MODEL_P1S || s_cfg.model==BAMBU_MODEL_A1 || s_cfg.model==BAMBU_MODEL_A1_MINI){
        char command[64];snprintf(command,sizeof(command),"M83\nG0 E%.1f F300\n",(double)length_mm);return publish_gcode_param(command);
    }
    cJSON *print = cJSON_CreateObject();
    if (!print) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(print, "command", "set_extrusion_length");
    cJSON_AddNumberToObject(print, "extruder_index", 0);
    cJSON_AddNumberToObject(print, "length", (int)length_mm);
    esp_err_t err = publish_print_json_cjson(print);
    if (err == ESP_OK) {
        return err;
    }
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "M83 \nG0 E%0.1f F300\n", (double)length_mm);
    return publish_gcode_param(cmd);
}

esp_err_t bambu_service_copy_lan_config(bambu_lan_config_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_requested_cfg.printer_ip[0] || !s_requested_cfg.access_code[0]) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    *out = s_requested_cfg;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

static esp_err_t start_print_file_impl(const char *filename_on_printer,
                                       bambu_print_storage_t storage,
                                       const bambu_project_print_opts_t *opts,
                                       const uint32_t *print_generation)
{
    if (!filename_on_printer || !filename_on_printer[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!bambu_ftp_is_printable_path(filename_on_printer)) return ESP_ERR_INVALID_ARG;
    char normalized[BAMBU_FTP_NAME_MAX+8];
    snprintf(normalized,sizeof(normalized),"%s%s",storage==BAMBU_PRINT_STORAGE_CACHE&&!strchr(filename_on_printer,'/')?"cache/":"",filename_on_printer);
    const char *path = normalized;
    const char *base = path;
    const char *slash = strrchr(path, '/');
    if (slash) {
        base = slash + 1;
    }

    if (name_is_project_archive(base)) {
        /* pybamb 定案（_service_call_print_project_file）：
         *   H2C/H2S/H2D → "ftp:///<FTP相对路径>"
         *   其余(P1/A1/X1) → "file:///sdcard/<FTP相对路径>"
         * 曾发 "ftp:///cache/..."（空主机、缺 /sdcard 挂载点）——P1 ack success
         * 但完全无动作；A1 上同形式还报 0500_C010 SD 错误 */
        char url[256];   /* "file:///sdcard/"(14) + cache/ + BAMBU_FTP_NAME_MAX 名 */
        snprintf(url, sizeof(url), "file:///sdcard/%s", path);
        char plate[48];
        snprintf(plate, sizeof(plate), "Metadata/plate_%d.gcode", bambu_service_project_plate(base));
        cJSON *print = bambu_studio_project_payload(plate, base, url, opts);
        return print ? publish_section("print", print, print_generation) : ESP_ERR_NO_MEM;
    }

    cJSON *print = bambu_studio_gcode_payload(path);
    return print ? publish_section("print", print, print_generation) : ESP_ERR_NO_MEM;
}

esp_err_t bambu_service_prepare_sd_print(const char *serial, uint32_t *generation,
                                         char *printer_name, size_t name_capacity)
{
    if (!serial || !serial[0] || !generation || !printer_name || !name_capacity)
        return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (s_requested_running && s_gen == s_active_gen && s_snap.connected &&
        serial_eq_i(serial, s_requested_cfg.serial)) {
        err = ESP_ERR_INVALID_STATE;
        if (sd_print_ready(s_gen)) {
            *generation = s_gen;
            strlcpy(printer_name, s_requested_cfg.nickname[0] ? s_requested_cfg.nickname :
                    s_requested_cfg.serial, name_capacity);
            err = ESP_OK;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t bambu_service_start_sd_print(const char *path, uint32_t generation)
{
    return start_print_file_impl(path, BAMBU_PRINT_STORAGE_ROOT, NULL, &generation);
}

esp_err_t bambu_service_start_print_file(const char *filename_on_printer,
                                         bambu_print_storage_t storage)
{
    return start_print_file_impl(filename_on_printer, storage, NULL, NULL);
}

esp_err_t bambu_service_start_print_file_ex(const char *filename_on_printer,
                                            bambu_print_storage_t storage,
                                            const bambu_project_print_opts_t *opts)
{
    return start_print_file_impl(filename_on_printer, storage, opts, NULL);
}

/* ── 命令应答查询（阶段 C：sequence_id/result 关联） ── */

uint32_t bambu_service_last_seq(void)
{
    if(!s_lock)return 0;
    TaskHandle_t owner=xTaskGetCurrentTaskHandle();uint32_t seq=0;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    for(unsigned k=0;k<BAMBU_ACK_RING;++k){
        const ack_slot_t *slot=&s_acks[(s_ack_wr+BAMBU_ACK_RING-1-k)%BAMBU_ACK_RING];
        if(slot->seq && slot->generation==s_gen && slot->owner==owner){seq=slot->seq;break;}
    }
    xSemaphoreGive(s_lock);return seq;
}

bambu_ack_state_t bambu_service_ack_query(uint32_t seq, char *result_out, size_t cap)
{
    if (result_out && cap) {
        result_out[0] = 0;
    }
    if (!seq || !s_lock) {
        return BAMBU_ACK_EXPIRED;
    }
    int64_t now = esp_timer_get_time();
    bambu_ack_state_t st = BAMBU_ACK_EXPIRED;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < BAMBU_ACK_RING; i++) {
        const ack_slot_t *slot = &s_acks[i];
        if (slot->seq != seq || slot->generation!=s_gen || slot->stamp_us == 0) {
            continue;
        }
        if (now - slot->stamp_us > BAMBU_ACK_TTL_US) {
            break;
        }
        st = slot->pending ? BAMBU_ACK_PENDING : (slot->ok ? BAMBU_ACK_OK : BAMBU_ACK_FAIL);
        if (result_out && cap) {
            strlcpy(result_out, slot->result, cap);
        }
        break;
    }
    xSemaphoreGive(s_lock);
    return st;
}

bambu_ack_state_t bambu_service_wait_ack(uint32_t seq, uint32_t timeout_ms,
                                         char *result_out, size_t cap)
{
    uint32_t waited = 0;
    uint32_t step = 50;
    for (;;) {
        bambu_ack_state_t st = bambu_service_ack_query(seq, result_out, cap);
        if (st != BAMBU_ACK_PENDING && st != BAMBU_ACK_EXPIRED) {
            return st;
        }
        if (waited >= timeout_ms) {
            return BAMBU_ACK_PENDING;
        }
        vTaskDelay(pdMS_TO_TICKS(step));
        waited += step;
    }
}

void bambu_service_set_ack_cb(bambu_ack_cb_t cb, void *user_data)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_ack_cb = cb;
    s_ack_cb_ud = user_data;
    xSemaphoreGive(s_lock);
}

