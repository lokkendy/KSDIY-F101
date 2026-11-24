#include "bambu_command_feedback.h"
#include <stdio.h>
#include <string.h>

#define CONFIRM_TIMEOUT_US (10LL * 1000 * 1000)

void bambu_feedback_begin(bambu_feedback_t *t, uint32_t seq, const char *action,
                          int64_t now, bambu_feedback_notify_t notify)
{
    if (!t || !seq || !action) return;
    unsigned oldest = 0, slot = BAMBU_FEEDBACK_CAPACITY;
    for (unsigned i = 0; i < BAMBU_FEEDBACK_CAPACITY; ++i) {
        if (t->entries[i].sequence == seq) return;
        if (!t->entries[i].sequence && slot == BAMBU_FEEDBACK_CAPACITY) slot = i;
        if (t->entries[i].started_us < t->entries[oldest].started_us) oldest = i;
    }
    if (slot == BAMBU_FEEDBACK_CAPACITY) {
        slot = oldest;
        if (notify) notify(t->entries[slot].action, BCF_UNKNOWN, "overwritten");
    }
    t->entries[slot].sequence = seq;
    t->entries[slot].started_us = now;
    snprintf(t->entries[slot].action, sizeof(t->entries[slot].action), "%s", action);
    if (notify) notify(t->entries[slot].action, BCF_PENDING, "");
}

void bambu_feedback_poll(bambu_feedback_t *t, int64_t now, bool connected,
                         bambu_feedback_query_t query, bambu_feedback_notify_t notify)
{
    if (!t || !query) return;
    for (unsigned i = 0; i < BAMBU_FEEDBACK_CAPACITY; ++i) {
        bambu_feedback_entry_t *e = &t->entries[i];
        if (!e->sequence) continue;
        char reason[64] = "";
        bambu_feedback_state_t state = connected ? query(e->sequence, reason, sizeof(reason)) : BCF_UNKNOWN;
        if (!connected) snprintf(reason, sizeof(reason), "disconnected");
        if (state == BCF_PENDING && now - e->started_us >= CONFIRM_TIMEOUT_US) {
            state = BCF_UNKNOWN;
            snprintf(reason, sizeof(reason), "timeout");
        }
        if (state == BCF_PENDING) continue;
        /* Remove first: a notification may start another command. */
        char action[sizeof(e->action)];
        memcpy(action, e->action, sizeof(action));
        e->sequence = 0;
        if (notify) notify(action, state, reason);
    }
}
