#pragma once
#include "bambu_service.h"
#include <stdint.h>
#define BAMBU_LOCAL_HISTORY_MAX 8
typedef struct {char name[96];uint32_t started,ended;uint8_t state,progress;uint8_t reserved[2];} bambu_history_entry_t;
void bambu_history_init(void);
void bambu_history_observe(const bambu_svc_snapshot_t *snapshot,uint32_t now);
unsigned bambu_history_copy(bambu_history_entry_t out[BAMBU_LOCAL_HISTORY_MAX],uint32_t *revision);
