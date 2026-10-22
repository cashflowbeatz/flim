#ifndef FLIM_UNIT_BURSTS_H
#define FLIM_UNIT_BURSTS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t epoch;
    uint32_t count;
} FlimUnitBurstCount;

static uint32_t flim_unit_bursts_begin(FlimUnitBurstCount *counts, size_t n, uint32_t *epoch) {
    if (++*epoch == 0) {
        memset(counts, 0, n * sizeof(*counts));
        *epoch = 1;
    }
    return *epoch;
}

static uint32_t flim_unit_bursts_hit(FlimUnitBurstCount *count, uint32_t epoch) {
    if (count->epoch != epoch) { count->epoch = epoch; count->count = 0; }
    return ++count->count;
}

#endif
