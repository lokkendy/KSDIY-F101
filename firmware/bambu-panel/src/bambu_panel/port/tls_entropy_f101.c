/* Serialize access to the SDK CE PRNG and fill every requested entropy byte. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hal_cache.h"
#include "sunxi_hal_ce.h"
static StaticSemaphore_t s_entropy_mutex_storage;
static SemaphoreHandle_t s_entropy_mutex;
int f101_mbedtls_entropy(unsigned char *output, size_t length)
{
    if (!output && length) return -1;
    taskENTER_CRITICAL();
    if (!s_entropy_mutex) s_entropy_mutex = xSemaphoreCreateMutexStatic(&s_entropy_mutex_storage);
    taskEXIT_CRITICAL();
    if (!s_entropy_mutex || xSemaphoreTake(s_entropy_mutex, portMAX_DELAY) != pdTRUE) return -1;
    int rc = sunxi_ce_init();
    while (!rc && length) {
        /* A 16-byte request is smaller than the hardware PRNG output block.
         * Avoid the vendor multi-block API's incomplete remainder handling. */
        uint8_t block[16] __attribute__((aligned(64))) = {0};
        crypto_rng_req_ctx_t req = {.rng_buf = block, .rng_len = sizeof(block)};
        rc = do_rng_gen(&req);
        if (!rc) {
            size_t n = length < sizeof(block) ? length : sizeof(block);
            memcpy(output, block, n); output += n; length -= n;
        }
        memset(block, 0, sizeof(block));
    }
    xSemaphoreGive(s_entropy_mutex);
    return rc;
}
