/* ESP TLS subset backed by the F101 SDK mbedTLS transport. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include "esp_err.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_crt.h"
typedef struct {
    bool skip_common_name;
    int timeout_ms;
    const char *printer_serial;
} esp_tls_cfg_t;
typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_net_context net;
    mbedtls_ctr_drbg_context rng;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt ca;
    int last_error;
    int timeout_ms;
    char lan_serial[32];
    char lan_host[48];
} esp_tls_t;
esp_tls_t *esp_tls_init(void);
int esp_tls_conn_new_sync(const char *host, int host_len, int port,
                          const esp_tls_cfg_t *cfg, esp_tls_t *tls);
ssize_t esp_tls_conn_read(esp_tls_t *tls, void *data, size_t length);
ssize_t esp_tls_conn_write(esp_tls_t *tls, const void *data, size_t length);
void esp_tls_conn_destroy(esp_tls_t *tls);
void f101_tls_read_timeout(esp_tls_t *tls, unsigned ms);

/* Orderly TLS EOF for an FTPS upload data stream. */
int f101_tls_close_notify(esp_tls_t *tls);
