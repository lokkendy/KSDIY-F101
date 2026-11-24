#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { BAMBU_FEEDBACK_CAPACITY = 8 };
typedef enum { BCF_PENDING, BCF_ACCEPTED, BCF_REJECTED, BCF_UNKNOWN } bambu_feedback_state_t;
typedef struct {
    uint32_t sequence;
    int64_t started_us;
    char action[64];
} bambu_feedback_entry_t;
typedef struct { bambu_feedback_entry_t entries[BAMBU_FEEDBACK_CAPACITY]; } bambu_feedback_t;
typedef bambu_feedback_state_t (*bambu_feedback_query_t)(uint32_t, char *, size_t);
typedef void (*bambu_feedback_notify_t)(const char *, bambu_feedback_state_t, const char *);

/* UI-thread owned. Acknowledgement means accepted, never physical completion. */
void bambu_feedback_begin(bambu_feedback_t *, uint32_t, const char *, int64_t,
                          bambu_feedback_notify_t);
void bambu_feedback_poll(bambu_feedback_t *, int64_t, bool,
                         bambu_feedback_query_t, bambu_feedback_notify_t);
