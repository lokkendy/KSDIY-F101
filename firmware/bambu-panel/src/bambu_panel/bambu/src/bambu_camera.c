/*

 * Bambu LAN camera — TCP/TLS JPEG stream (OpenBambuAPI video.md, port 6000).

 * P1 / A1 系列；X1/H 等为 RTSP:322，本模块暂不支持。

 */

#include "bambu_camera.h"



#include "bambu_model.h"



#include "esp_heap_caps.h"

#include "esp_log.h"

#include "esp_tls.h"

#include "freertos/FreeRTOS.h"

#include "freertos/semphr.h"

#include "freertos/task.h"

#include "sdkconfig.h"



#include <string.h>



static const char *TAG = "bambu_cam";



#define CAM_TLS_TIMEOUT_MS    5000

/** 等 worker 自行结束 TLS（含 conn_new_sync 最长阻塞）；勿跨线程 esp_tls_conn_destroy */

#define CAM_STOP_WAIT_MS      (CAM_TLS_TIMEOUT_MS + 1500)



static StaticSemaphore_t s_lock_buf;

static SemaphoreHandle_t s_lock;

static TaskHandle_t s_worker;

static volatile uint32_t s_gen;

static bambu_lan_config_t s_cfg;

static bambu_cam_state_t s_state;

static char s_msg[96];



static uint8_t *s_jpeg_a;

static uint8_t *s_jpeg_b;

static uint8_t *s_jpeg_pub;

static size_t s_jpeg_len;

static uint32_t s_frame_seq;



typedef struct {

    char printer_ip[48];

    char access_code[64];
    char expected_serial[32];

    uint32_t gen;

} cam_sess_t;

/** 单次仅一个 worker；在 xTaskCreate 前写入，worker 入口即复制 */
static cam_sess_t s_worker_sess;



static void set_status(bambu_cam_state_t st, const char *msg)

{

    if (s_lock) {

        xSemaphoreTake(s_lock, portMAX_DELAY);

    }

    s_state = st;

    if (msg) {

        strncpy(s_msg, msg, sizeof(s_msg) - 1);

        s_msg[sizeof(s_msg) - 1] = 0;

    }

    if (s_lock) {

        xSemaphoreGive(s_lock);

    }

}



static void build_auth_packet(uint8_t out[80], const char *access_code)

{

    memset(out, 0, 80);

    const uint32_t payload = 0x40;

    const uint32_t type = 0x3000;

    memcpy(out + 0, &payload, 4);

    memcpy(out + 4, &type, 4);

    strncpy((char *)out + 16, "bblp", 31);

    if (access_code) {

        strncpy((char *)out + 48, access_code, 31);

    }

}



static int tls_read_all(esp_tls_t *tls, uint8_t *buf, size_t need, uint32_t gen)

{

    size_t got = 0;

    while (got < need) {

        if (gen != s_gen) {

            return -1;

        }

        ssize_t n = esp_tls_conn_read(tls, buf + got, need - got);

        if (n <= 0) {

            return -1;

        }

        got += (size_t)n;

    }

    return 0;

}



static bool publish_jpeg(const uint8_t *data, size_t len)

{

    if (len < 4 || data[0] != 0xff || data[1] != 0xd8) {

        return false;

    }

    if (len > (size_t)CONFIG_BAMBU_CAMERA_FRAME_MAX) {

        ESP_LOGW(TAG, "frame too large: %u", (unsigned)len);

        return false;

    }

    uint8_t *dst = (s_jpeg_pub == s_jpeg_a) ? s_jpeg_b : s_jpeg_a;

    memcpy(dst, data, len);

    if (s_lock) {

        xSemaphoreTake(s_lock, portMAX_DELAY);

    }

    s_jpeg_pub = dst;

    s_jpeg_len = len;

    s_frame_seq++;

    if (s_lock) {

        xSemaphoreGive(s_lock);

    }

    return true;

}



static esp_err_t stream_session(const cam_sess_t *sess)

{

    const uint32_t gen = sess->gen;

    if (gen != s_gen || !sess->printer_ip[0]) {

        return ESP_ERR_INVALID_STATE;

    }



    esp_tls_cfg_t tls_cfg = { 0 };

#if CONFIG_BAMBU_LAN_TLS_INSECURE

    tls_cfg.skip_common_name = true;

#endif

    tls_cfg.timeout_ms = CAM_TLS_TIMEOUT_MS;
    if (sess->expected_serial[0]) tls_cfg.printer_serial = sess->expected_serial;



    esp_tls_t *tls = esp_tls_init();

    if (!tls) {

        return ESP_ERR_NO_MEM;

    }



    int ret = esp_tls_conn_new_sync(sess->printer_ip, strlen(sess->printer_ip),

                                    CONFIG_BAMBU_CAMERA_TCP_PORT, &tls_cfg, tls);

    if (gen != s_gen) {

        esp_tls_conn_destroy(tls);

        return ESP_ERR_INVALID_STATE;

    }

    if (ret != 1) {

        ESP_LOGE(TAG, "TLS connect failed");

        esp_tls_conn_destroy(tls);

        return ESP_FAIL;

    }



    uint8_t auth[80];

    build_auth_packet(auth, sess->access_code);

    ssize_t w = esp_tls_conn_write(tls, auth, sizeof(auth));

    if (gen != s_gen || w != (ssize_t)sizeof(auth)) {

        esp_tls_conn_destroy(tls);

        return ESP_FAIL;

    }



    set_status(BAMBU_CAM_STREAMING, "实时画面");



    uint8_t hdr[16];

    while (gen == s_gen) {

        if (tls_read_all(tls, hdr, sizeof(hdr), gen) != 0) {

            break;

        }

        uint32_t payload = 0;

        memcpy(&payload, hdr, 4);

        if (payload == 0 || payload > (uint32_t)CONFIG_BAMBU_CAMERA_FRAME_MAX) {

            ESP_LOGW(TAG, "bad payload size %lu", (unsigned long)payload);

            break;

        }

        uint8_t *frame = (s_jpeg_pub == s_jpeg_a) ? s_jpeg_b : s_jpeg_a;

        if (tls_read_all(tls, frame, payload, gen) != 0) {

            break;

        }

        (void)publish_jpeg(frame, payload);

    }



    esp_tls_conn_destroy(tls);

    return ESP_OK;

}



static void worker_task(void *arg)

{

    cam_sess_t sess = *(const cam_sess_t *)arg;

    unsigned backoff_ms = 2000;



    while (sess.gen == s_gen) {

        set_status(BAMBU_CAM_CONNECTING, "正在连接摄像头…");

        esp_err_t err = stream_session(&sess);

        if (sess.gen != s_gen) {

            break;

        }

        if (err != ESP_OK) {

            set_status(BAMBU_CAM_ERROR, "摄像头连接失败");

        } else {

            set_status(BAMBU_CAM_ERROR, "摄像头流中断");

        }

        vTaskDelay(pdMS_TO_TICKS(backoff_ms));

        if (backoff_ms < 8000) {

            backoff_ms += 1000;

        }

    }

    s_worker = NULL;

    vTaskDelete(NULL);

}



static void wait_worker_exit(void)

{

    const int loops = CAM_STOP_WAIT_MS / 50;

    for (int i = 0; i < loops && s_worker; i++) {

        vTaskDelay(pdMS_TO_TICKS(50));

    }

    if (s_worker) {

        ESP_LOGW(TAG, "worker still running after %dms", CAM_STOP_WAIT_MS);

    }

}



void bambu_camera_init(void)

{

    if (s_lock) {

        return;

    }

    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);

    s_state = BAMBU_CAM_STOPPED;

    s_msg[0] = 0;

}



esp_err_t bambu_camera_start(const bambu_lan_config_t *cfg)

{

    bambu_camera_init();

    if (!s_lock) {

        return ESP_ERR_INVALID_STATE;

    }

    if (!cfg || !cfg->printer_ip[0] || !cfg->access_code[0]) {

        return ESP_ERR_INVALID_ARG;

    }

    if (!bambu_model_camera_tcp_jpeg(cfg->model)) {

        set_status(BAMBU_CAM_UNSUPPORTED,

                   "该机型的 RTSP 摄像头暂未支持（P1/A1 可用 LAN 6000）");

        return ESP_ERR_NOT_SUPPORTED;

    }



    bambu_camera_stop();



    if (s_worker) {

        ESP_LOGW(TAG, "start blocked: waiting for previous worker");

        wait_worker_exit();

    }

    if (s_worker) {

        ESP_LOGE(TAG, "start failed: worker did not exit");

        return ESP_ERR_TIMEOUT;

    }



    if (!s_jpeg_a) {

        size_t cap = (size_t)CONFIG_BAMBU_CAMERA_FRAME_MAX;

        s_jpeg_a = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

        s_jpeg_b = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

        if (!s_jpeg_a || !s_jpeg_b) {

            heap_caps_free(s_jpeg_a);

            heap_caps_free(s_jpeg_b);

            s_jpeg_a = s_jpeg_b = NULL;

            return ESP_ERR_NO_MEM;

        }

    }



    xSemaphoreTake(s_lock, portMAX_DELAY);

    s_cfg = *cfg;

    s_jpeg_pub = NULL;

    s_jpeg_len = 0;

    xSemaphoreGive(s_lock);



    s_gen++;

    memset(&s_worker_sess, 0, sizeof(s_worker_sess));

    strncpy(s_worker_sess.printer_ip, cfg->printer_ip, sizeof(s_worker_sess.printer_ip) - 1);

    strncpy(s_worker_sess.access_code, cfg->access_code, sizeof(s_worker_sess.access_code) - 1);

    if (cfg->verify_printer) strlcpy(s_worker_sess.expected_serial, cfg->serial, sizeof(s_worker_sess.expected_serial));
    s_worker_sess.gen = s_gen;



    BaseType_t ok = xTaskCreate(worker_task, "bambu_cam", 8192,

                                &s_worker_sess, 4, &s_worker);

    return ok == pdPASS ? ESP_OK : ESP_FAIL;

}



void bambu_camera_stop(void)

{

    if (!s_lock) {

        return;

    }

    s_gen++;



    wait_worker_exit();



    xSemaphoreTake(s_lock, portMAX_DELAY);

    s_jpeg_pub = NULL;

    s_jpeg_len = 0;

    xSemaphoreGive(s_lock);



    if (!s_worker && s_jpeg_a) {

        heap_caps_free(s_jpeg_a);

        heap_caps_free(s_jpeg_b);

        s_jpeg_a = s_jpeg_b = NULL;

    }

    set_status(BAMBU_CAM_STOPPED, "未开启");

}



void bambu_camera_get_status(bambu_cam_state_t *state, char *msg, size_t msg_len)

{

    bambu_camera_init();

    if (!s_lock) {

        if (state) {

            *state = BAMBU_CAM_STOPPED;

        }

        if (msg && msg_len > 0) {

            strncpy(msg, "相机未就绪", msg_len - 1);

            msg[msg_len - 1] = 0;

        }

        return;

    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (state) {

        *state = s_state;

    }

    if (msg && msg_len > 0) {

        strncpy(msg, s_msg, msg_len - 1);

        msg[msg_len - 1] = 0;

    }

    xSemaphoreGive(s_lock);

}



bool bambu_camera_copy_frame(const uint8_t **jpeg, size_t *len, uint32_t *seq)

{

    if (!jpeg || !len || !seq) {

        return false;

    }

    bambu_camera_init();

    if (!s_lock) {

        return false;

    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    bool ok = s_jpeg_pub != NULL && s_jpeg_len > 0;

    if (ok) {

        *jpeg = s_jpeg_pub;

        *len = s_jpeg_len;

        *seq = s_frame_seq;

    }

    xSemaphoreGive(s_lock);

    return ok;

}



bool bambu_camera_read_frame(uint8_t *jpeg,size_t capacity,size_t *len,uint32_t *seq)
{
    if(!jpeg||!len||!seq||!s_lock)return false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool ok=s_jpeg_pub&&s_jpeg_len&&s_jpeg_len<=capacity&&s_frame_seq!=*seq;
    if(ok){memcpy(jpeg,s_jpeg_pub,s_jpeg_len);*len=s_jpeg_len;*seq=s_frame_seq;}
    xSemaphoreGive(s_lock);return ok;
}
