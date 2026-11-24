/* F101 Cedar JPEG hardware decode -> direct MB32 RGB565 preview sampling.
 * Avoid a full-resolution RGB surface and a large G2D command array on 16MB S3. */
#include "app_jpeg_dec.h"
#include "PicDecoder.h"
#include "bambu_jpeg_pixels.h"
#include "hal_mem.h"
#include "hal_cache.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
size_t rgb_width,rgb_height;
static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;
static bool s_hw_ready;
static uint8_t *s_yuv,*s_input;
static uint16_t *s_output;
static uint32_t s_width,s_height;
static size_t s_yuv_bytes,s_input_bytes,s_output_bytes;
static unsigned s_attempts,s_decoded,s_ready,s_errors;
/* Confirmed on this F101S3 board: convert_format=0 returns MB32.
 * The generic SDK output-format register comment alone is not a layout
 * contract; YV12 sampling scrambled motion and MB32 restored the picture. */
static volatile int s_layout=0;
void app_jpeg_dec_layout(int layout){if(layout>=0&&layout<=3)s_layout=layout;}
static bool jpeg_size(const uint8_t *b, size_t n, uint32_t *w, uint32_t *h)
{
    if (!b || n < 4 || b[0] != 255 || b[1] != 216) return false;
    size_t pos = 2;
    while (pos + 3 < n) {
        if (b[pos++] != 255) return false;
        while (pos < n && b[pos] == 255) ++pos;
        if (pos >= n) return false;
        unsigned marker = b[pos++];
        if (marker == 0xda || marker == 0xd9) return false;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (pos + 2 > n) return false;
        size_t length = ((unsigned)b[pos] << 8) | b[pos + 1];
        if (length < 2 || length > n - pos) return false;
        if (marker == 0xc2) return false;
        if (marker == 0xc0) {
            if (length < 17 || b[pos+2]!=8 || b[pos+7]!=3 || b[pos+9]!=0x22 || b[pos+12]!=0x11 || b[pos+15]!=0x11) return false;
            *h = ((unsigned)b[pos + 3] << 8) | b[pos + 4];
            *w = ((unsigned)b[pos + 5] << 8) | b[pos + 6];
            return *w && *h && *w <= 1920 && *h <= 1080;
        }
        pos += length;
    }
    return false;
}

static void drop_decode_buffers(void)
{
    if(s_yuv)hal_free_align(s_yuv);
    if(s_input)hal_free_align(s_input);
    free(s_output);s_yuv=s_input=NULL;s_output=NULL;
    s_width=s_height=0;s_yuv_bytes=s_input_bytes=s_output_bytes=0;
}
esp_err_t app_jpeg_dec_init(void)
{
    taskENTER_CRITICAL();
    if(!s_mutex)s_mutex=xSemaphoreCreateMutexStatic(&s_mutex_storage);
    taskEXIT_CRITICAL();
    return s_mutex?ESP_OK:ESP_ERR_NO_MEM;
}
esp_err_t app_jpeg_dec_deinit(void)
{
    if(!s_mutex)return ESP_OK;
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    if(s_hw_ready){Pic_Deinit();Dec_Deinit();s_hw_ready=false;}
    drop_decode_buffers();xSemaphoreGive(s_mutex);return ESP_OK;
}
void app_jpeg_dec_debug_status(void)
{
    printf("[bambu jpeg] attempts=%u decoded=%u ready=%u errors=%u source=%lux%lu output=%lux%lu workspace=%lu hardware=%u layout=%d\n",
        s_attempts,s_decoded,s_ready,s_errors,(unsigned long)s_width,(unsigned long)s_height,
        (unsigned long)rgb_width,(unsigned long)rgb_height,
        (unsigned long)(s_yuv_bytes+s_input_bytes+s_output_bytes),s_hw_ready,s_layout);
}
esp_err_t app_jpeg_dec_process_to(const uint8_t *input,size_t len,uint32_t max_w,uint32_t max_h,
                                  uint8_t **out,uint32_t *out_w,uint32_t *out_h)
{
    if(!out||!out_w||!out_h||!max_w||!max_h||len>262144)return ESP_ERR_INVALID_ARG;
    *out=NULL;uint32_t w,h;if(!jpeg_size(input,len,&w,&h))return ESP_ERR_NOT_SUPPORTED;
    if(app_jpeg_dec_init()!=ESP_OK)return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_mutex,portMAX_DELAY);s_attempts++;
    esp_err_t error=ESP_FAIL;
    if(!s_hw_ready){
        if(Dec_init()!=0){Dec_Deinit();error=ESP_ERR_NO_MEM;goto done;}
        if(Pic_Init()!=0){Pic_Deinit();Dec_Deinit();goto done;}
        s_hw_ready=true;
    }
    uint32_t aw=(w+31)&~31u,ah=(h+31)&~31u;
    size_t ysize=(size_t)aw*ah;
    /* Chroma tiles are 32 rows high, i.e. 64 source luma rows. */
    size_t bytes=ysize+(size_t)aw*((h+63)&~63u)/2+1024;
    if(aw!=s_width||ah!=s_height){
        if(s_yuv)hal_free_align(s_yuv);
        s_yuv=hal_malloc_align(bytes,4096);s_yuv_bytes=0;s_width=s_height=0;
        if(!s_yuv){error=ESP_ERR_NO_MEM;goto done;}
        s_yuv_bytes=bytes;s_width=aw;s_height=ah;
    }
    size_t vbv=(len+2047)&~1023u;
    if(vbv>s_input_bytes){
        if(s_input)hal_free_align(s_input);
        s_input=hal_malloc_align(vbv,1024);s_input_bytes=0;
        if(!s_input){error=ESP_ERR_NO_MEM;goto done;}s_input_bytes=vbv;
    }
    memcpy(s_input,input,len);memset(s_input+len,0,vbv-len);
    /* Eliminate dirty CPU cache lines before VE owns the output surface. */
    hal_dcache_clean_invalidate((unsigned long)s_yuv,s_yuv_bytes);
    JpegDecCdx info={.jpegData=s_input,.jpegData_len=len,.pic_width=w,.pic_height=h,
        .yuv_buf=(unsigned long)s_yuv,.y_buf=(unsigned long)s_yuv,
        .u_buf=(unsigned long)s_yuv+ysize,.v_buf=(unsigned long)s_yuv+ysize+ysize/4,
        .yuv_buf_size=bytes,.convert_format=0};
    if(Jpeg_Decode(&info)!=0)goto done;
    s_decoded++;hal_dcache_invalidate((unsigned long)s_yuv,s_yuv_bytes);
    uint32_t dw=w,dh=h;
    if(dw>max_w){dh=(uint64_t)dh*max_w/dw;dw=max_w;}
    if(dh>max_h){dw=(uint64_t)dw*max_h/dh;dh=max_h;}
    if(!dw)dw=1;
    if(!dh)dh=1;
    size_t needed=(size_t)dw*dh*2;
    if(needed>s_output_bytes){
        uint16_t *next=realloc(s_output,needed);if(!next){error=ESP_ERR_NO_MEM;goto done;}
        s_output=next;s_output_bytes=needed;
    }
    jpeg_preview_scale(s_output,dw,dh,s_yuv,aw,ah,w,h,s_layout);
    *out=(uint8_t*)s_output;*out_w=dw;*out_h=dh;rgb_width=dw;rgb_height=dh;
    s_ready++;error=ESP_OK;
    if(s_ready==1)printf("[bambu jpeg] hardware initialized, first frame %lux%lu -> %lux%lu, workspace=%lu\n",
        (unsigned long)w,(unsigned long)h,(unsigned long)dw,(unsigned long)dh,
        (unsigned long)(s_yuv_bytes+s_input_bytes+s_output_bytes));
done:
    if(error!=ESP_OK){s_errors++;printf("[bambu jpeg] frame failed rc=%d\n",error);}
    xSemaphoreGive(s_mutex);return error;
}
esp_err_t app_jpeg_dec_process(const uint8_t *input,size_t len,uint8_t **out)
{
    uint32_t w,h;lv_display_t *d=lv_display_get_default();
    return app_jpeg_dec_process_to(input,len,d?lv_display_get_horizontal_resolution(d):800,
                                    d?lv_display_get_vertical_resolution(d):480,out,&w,&h);
}
