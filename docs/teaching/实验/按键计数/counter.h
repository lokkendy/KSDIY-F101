#ifndef F101_TEACHING_COUNTER_H
#define F101_TEACHING_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t value;
    bool down;
} counter_t;

uint32_t counter_update(counter_t *counter, bool pressed, bool clear, uint32_t limit);

#endif
