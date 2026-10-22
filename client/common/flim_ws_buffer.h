#pragma once
#include <stddef.h>
#include <stdlib.h>

// The relay accepts 64 MiB, then adds seq/from fields. Allow its envelope plus one overflow byte.
#define FLIM_WS_MAX_MESSAGE (64u * 1024u * 1024u + 1024u)
#ifndef FLIM_WS_REALLOC
#define FLIM_WS_REALLOC realloc
#endif

static int flim_ws_reserve(unsigned char **buffer, size_t *capacity, size_t length) {
    const size_t limit = FLIM_WS_MAX_MESSAGE + 2u;
    size_t wanted, next;
    unsigned char *grown;
    if (length > FLIM_WS_MAX_MESSAGE) return 0;
    wanted = length + 65537u;
    if (wanted > limit) wanted = limit;
    if (*capacity >= wanted) return 1;
    next = *capacity ? *capacity : 262144u;
    while (next < wanted) next = next > limit / 2 ? limit : next * 2;
    if (next > limit) next = limit;
    grown = (unsigned char *)FLIM_WS_REALLOC(*buffer, next);
    if (!grown) return 0;
    *buffer = grown;
    *capacity = next;
    return 1;
}
