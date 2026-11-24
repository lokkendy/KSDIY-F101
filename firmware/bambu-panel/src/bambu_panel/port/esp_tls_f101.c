/* F101 native TLS; all contexts are connection-local. No simulator backend. */
#include "esp_tls.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

extern const char mi_http_ca_pem[];
extern const size_t mi_http_ca_pem_len;
static const unsigned char printer_ca_pem[] =
#include "bambu_printer_ca.inc"
;

esp_tls_t *esp_tls_init(void)
{
    esp_tls_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->config);
    mbedtls_net_init(&t->net);
    mbedtls_ctr_drbg_init(&t->rng);
    mbedtls_entropy_init(&t->entropy);
    mbedtls_x509_crt_init(&t->ca);
    return t;
}

static int tcp_connect(esp_tls_t *t, const char *host, int port, int timeout_ms)
{
    struct addrinfo hints = {0}, *addresses = NULL, *a;
    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%d", port);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int resolved=lwip_getaddrinfo(host,port_text,&hints,&addresses);
    if(resolved){ESP_LOGW("tls","resolve failed rc=%d",resolved);return -1;}
    int result=-1;
    for(a=addresses;a;a=a->ai_next){
        int fd=lwip_socket(a->ai_family,a->ai_socktype,a->ai_protocol);
        if(fd<0){ESP_LOGW("tls","socket failed errno=%d",errno);continue;}
        int nonblocking=1;
        if(ioctlsocket(fd,FIONBIO,&nonblocking)!=0){
            ESP_LOGW("tls","socket nonblocking failed errno=%d",errno);lwip_close(fd);continue;
        }
        int rc=lwip_connect(fd,a->ai_addr,a->ai_addrlen);
        int connect_errno=errno;
        if(rc!=0){
            if(connect_errno!=EINPROGRESS && connect_errno!=115){ESP_LOGW("tls","connect pending rc=%d errno=%d expected=%d",rc,connect_errno,EINPROGRESS);}
            fd_set writable,exception;FD_ZERO(&writable);FD_ZERO(&exception);FD_SET(fd,&writable);FD_SET(fd,&exception);
            struct timeval tv={timeout_ms/1000,(timeout_ms%1000)*1000};
            int selected=lwip_select(fd+1,NULL,&writable,&exception,&tv);
            if(selected>0){
                int error=0;socklen_t n=sizeof(error);
                rc=lwip_getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&n);
                if(rc!=0 || error){ESP_LOGW("tls","connect completion rc=%d socket_error=%d errno=%d",rc,error,errno);rc=-1;}
            }else{ESP_LOGW("tls","connect select rc=%d errno=%d",selected,errno);rc=-1;}
        }
        if(rc==0){
            nonblocking=0;
            if(ioctlsocket(fd,FIONBIO,&nonblocking)!=0){lwip_close(fd);continue;}
            t->net.fd=fd;result=0;break;
        }
        lwip_close(fd);
    }
    lwip_freeaddrinfo(addresses);
    return result;
}

int esp_tls_conn_new_sync(const char *host, int host_len, int port,
                          const esp_tls_cfg_t *cfg, esp_tls_t *t)
{
    if (!host || host_len < 1 || host_len >= 96 || port < 1 || port > 65535 || !t) return -1;
    char name[96]; memcpy(name, host, host_len); name[host_len] = 0;
    t->timeout_ms = cfg && cfg->timeout_ms > 0 ? cfg->timeout_ms : 15000;
    const char *serial = cfg ? cfg->printer_serial : NULL;
    if (serial && (!*serial || strlen(serial) >= sizeof(t->lan_serial) ||
                   (size_t)host_len >= sizeof(t->lan_host))) return -1;
    const bool verify = serial || !cfg || !cfg->skip_common_name;
    if (verify && time(NULL) < 1700000000L) return -1;
    static const unsigned char personalization[] = "F101S3 Bambu TLS";
    const char *stage="entropy";
    int rc = mbedtls_ctr_drbg_seed(&t->rng, mbedtls_entropy_func, &t->entropy,
                                  personalization, sizeof(personalization) - 1);
    if (rc) goto fail;
    stage="defaults";
    rc = mbedtls_ssl_config_defaults(&t->config, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc) goto fail;
    mbedtls_ssl_conf_min_version(&t->config, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    if (verify) {
        stage = "trust anchors";
        const unsigned char *roots = serial ? printer_ca_pem : (const unsigned char *)mi_http_ca_pem;
        size_t root_len = serial ? sizeof(printer_ca_pem) : mi_http_ca_pem_len;
        rc = mbedtls_x509_crt_parse(&t->ca, roots, root_len);
        if (rc < 0 || !t->ca.raw.p) goto fail;
        mbedtls_ssl_conf_ca_chain(&t->config, &t->ca, NULL);
        mbedtls_ssl_conf_authmode(&t->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        mbedtls_ssl_conf_authmode(&t->config, MBEDTLS_SSL_VERIFY_NONE);
    }
    mbedtls_ssl_conf_rng(&t->config, mbedtls_ctr_drbg_random, &t->rng);
    mbedtls_ssl_conf_read_timeout(&t->config, t->timeout_ms);
    stage="setup";
    if ((rc = mbedtls_ssl_setup(&t->ssl, &t->config))) goto fail;
    stage="hostname";
    if ((rc = mbedtls_ssl_set_hostname(&t->ssl, serial ? serial : name))) goto fail;
    stage="tcp";
    if ((rc = tcp_connect(t, name, port, t->timeout_ms))) goto fail;
    mbedtls_ssl_set_bio(&t->ssl, &t->net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
    stage="handshake";
    int64_t deadline = esp_timer_get_time() + (int64_t)t->timeout_ms * 1000;
    do {
        rc = mbedtls_ssl_handshake(&t->ssl);
        if (rc == 0) {
            if (serial) {
                strcpy(t->lan_serial, serial);
                strcpy(t->lan_host, name);
            }
            return 1;
        }
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (esp_timer_get_time() < deadline);
fail:
    t->last_error = rc;
    ESP_LOGW("tls", "connect %s:%d stage=%s failed: -0x%04x", name, port, stage, (unsigned)-rc);
    return -1;
}

void f101_tls_read_timeout(esp_tls_t *t, unsigned ms)
{
    if (t) mbedtls_ssl_conf_read_timeout(&t->config, ms);
}
ssize_t esp_tls_conn_read(esp_tls_t *t, void *data, size_t length)
{
    if (!t || !data) return -1;
    int rc = mbedtls_ssl_read(&t->ssl, data, length);
    if (rc == MBEDTLS_ERR_SSL_TIMEOUT) return MBEDTLS_ERR_SSL_WANT_READ;
    t->last_error = rc < 0 ? rc : 0;
    return rc;
}
ssize_t esp_tls_conn_write(esp_tls_t *t, const void *data, size_t length)
{
    if (!t || (!data && length)) return -1;
    size_t done = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)t->timeout_ms * 1000;
    while (done < length) {
        int rc = mbedtls_ssl_write(&t->ssl, (const unsigned char *)data + done, length - done);
        if (rc > 0) { done += rc; continue; }
        if ((rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) &&
            esp_timer_get_time() < deadline) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
        t->last_error = rc;
        return -1;
    }
    return (ssize_t)done;
}
void esp_tls_conn_destroy(esp_tls_t *t)
{
    if (!t) return;
    mbedtls_net_free(&t->net);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->config);
    mbedtls_ctr_drbg_free(&t->rng);
    mbedtls_entropy_free(&t->entropy);
    mbedtls_x509_crt_free(&t->ca);
    free(t);
}

int f101_tls_close_notify(esp_tls_t *t)
{
    if(!t)return -1;
    int rc;int64_t deadline=esp_timer_get_time()+2000000;
    do{
        rc=mbedtls_ssl_close_notify(&t->ssl);
        if(rc!=MBEDTLS_ERR_SSL_WANT_READ && rc!=MBEDTLS_ERR_SSL_WANT_WRITE)return rc;
        vTaskDelay(pdMS_TO_TICKS(1)?pdMS_TO_TICKS(1):1);
    }while(esp_timer_get_time()<deadline);
    return rc;
}
