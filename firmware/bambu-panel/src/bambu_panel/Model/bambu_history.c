/* Records only tasks actually observed by this screen; no cloud history is fabricated. */
#include "bambu_history.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
typedef struct {uint32_t version,count; bambu_history_entry_t entries[BAMBU_LOCAL_HISTORY_MAX];} history_store_t;
static history_store_t store;
static SemaphoreHandle_t lock;
static uint32_t revision;
static bool tracking,dirty;
static bambu_history_entry_t current;
void bambu_history_init(void)
{
    if(lock)return;
    lock=xSemaphoreCreateMutex();if(!lock)return;
    nvs_handle_t h;size_t n=sizeof(store);
    if(nvs_open("bambu_history",NVS_READONLY,&h)==ESP_OK){
        if(nvs_get_blob(h,"recent",&store,&n)!=ESP_OK||n!=sizeof(store)||store.version!=1||store.count>BAMBU_LOCAL_HISTORY_MAX)memset(&store,0,sizeof(store));
        nvs_close(h);
    }
    store.version=1;
    for(unsigned i=0;i<store.count;++i)store.entries[i].name[95]=0;
    revision=1;
}
void bambu_history_observe(const bambu_svc_snapshot_t *s,uint32_t now)
{
    if(!lock||!s)return;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(s->connected&&s->printer.has_data){
        const bambu_status_t *p=&s->printer;
        bool active=p->state==BAMBU_PRINT_RUNNING||p->state==BAMBU_PRINT_PREPARE||p->state==BAMBU_PRINT_PAUSED;
        if(active&&p->task_name[0]){
            if(!tracking||strcmp(current.name,p->task_name)){
                memset(&current,0,sizeof(current));snprintf(current.name,sizeof(current.name),"%s",p->task_name);current.started=now;tracking=true;
            }
            current.progress=p->progress_percent<0?0:p->progress_percent>100?100:p->progress_percent;
        }else if(tracking&&(p->state==BAMBU_PRINT_FINISHED||p->state==BAMBU_PRINT_FAILED||p->state==BAMBU_PRINT_IDLE)){
            if(p->task_name[0]&&strcmp(current.name,p->task_name)){
                tracking=false;
                xSemaphoreGive(lock);
                return;
            }
            current.ended=now;current.state=p->state;
            if(p->state==BAMBU_PRINT_FINISHED)current.progress=100;
            memmove(store.entries+1,store.entries,(BAMBU_LOCAL_HISTORY_MAX-1)*sizeof(current));store.entries[0]=current;
            if(store.count<BAMBU_LOCAL_HISTORY_MAX)store.count++;
            tracking=false;dirty=true;revision++;
        }
    }
    /* Background poll only: flash I/O never blocks the LVGL render boundary. */
    if(dirty){nvs_handle_t h;if(nvs_open("bambu_history",NVS_READWRITE,&h)==ESP_OK){
        if(nvs_set_blob(h,"recent",&store,sizeof(store))==ESP_OK&&nvs_commit(h)==ESP_OK)dirty=false;
        nvs_close(h);
    }}
    xSemaphoreGive(lock);
}
unsigned bambu_history_copy(bambu_history_entry_t out[BAMBU_LOCAL_HISTORY_MAX],uint32_t *rev)
{
    if(!lock)return 0;
    if(xSemaphoreTake(lock,0)!=pdTRUE)return BAMBU_LOCAL_HISTORY_MAX+1;
    unsigned n=store.count;
    if(out)memcpy(out,store.entries,n*sizeof(*out));
    if(rev)*rev=revision;
    xSemaphoreGive(lock);return n;
}
