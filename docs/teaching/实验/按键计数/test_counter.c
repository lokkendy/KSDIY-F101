#include <assert.h>
#include <stdio.h>

#include "counter.h"

int main(void)
{
    counter_t counter = {0};
    assert(counter_update(&counter, false, false, 2) == 0);
    assert(counter_update(&counter, true, false, 2) == 1);
    assert(counter_update(&counter, true, false, 2) == 1);
    assert(counter_update(&counter, false, false, 2) == 1);
    assert(counter_update(&counter, true, false, 2) == 2);
    assert(counter_update(&counter, false, false, 2) == 2);
    assert(counter_update(&counter, true, false, 2) == 2);
    assert(counter_update(&counter, true, true, 2) == 0);
    assert(counter_update(&counter, true, false, 2) == 0);
    assert(counter_update(&counter, false, false, 2) == 0);
    assert(counter_update(&counter, true, false, 2) == 1);
    puts("PASS");
    return 0;
}
