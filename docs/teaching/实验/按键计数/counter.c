#include "counter.h"

uint32_t counter_update(counter_t *counter, bool pressed, bool clear, uint32_t limit)
{
    if (clear) {
        counter->value = 0;
    } else if (pressed && !counter->down && counter->value < limit) {
        counter->value++;
    }
    counter->down = pressed;
    return counter->value;
}
