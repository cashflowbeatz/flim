#ifndef FLIM_FLP_H
#define FLIM_FLP_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int (*flpev_t)(void *ctx, uint32_t id, const unsigned char *p, size_t n, size_t at, size_t end);

static uint32_t flp_u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int flp_data_bounds(const unsigned char *d, size_t n, size_t *start, size_t *end) {
    size_t h, len;
    if (!d || n < 22 || memcmp(d, "FLhd", 4)) return 0;
    h = flp_u32(d + 4);
    if (h < 6 || h > n - 16) return 0;
    h += 8;
    if (memcmp(d + h, "FLdt", 4)) return 0;
    len = flp_u32(d + h + 4);
    *start = h + 8;
    if (len > n - *start) return 0;
    *end = *start + len;
    return 1;
}

static int flp_events_walk(const unsigned char *d, size_t o, size_t end, flpev_t cb, void *ctx) {
    while (o < end) {
        size_t at = o, len;
        uint32_t id = d[o++], kind;
        /* 0xac escapes a full event ID; its high nibble selects the payload type.
           Verified in FL 25.2.3 / 26.1.6 readers and writers; see compatibility evidence. */
        if (id == 0xac) {
            if (end - o < 4) return 0;
            id = flp_u32(d + o);
            o += 4;
        }
        kind = id <= 0xff ? id >> 6 : id >> 28;
        if (id > 0xff && kind != 0 && kind != 4 && kind != 8 && kind != 12) return 0;
        if (id > 0xff) kind >>= 2;
        if (kind < 3) len = (size_t)1 << kind;
        else {
            unsigned s = 0;
            uint32_t v = 0;
            for (;;) {
                unsigned char b;
                if (o == end || s > 28) return 0;
                b = d[o++];
                if (s == 28 && (b & 0xf0)) return 0;
                v |= (uint32_t)(b & 0x7f) << s;
                if (!(b & 0x80)) break;
                s += 7;
            }
            len = v;
        }
        if (len > end - o) return 0;
        if (cb && !cb(ctx, id, d + o, len, at, o + len)) return 1;
        o += len;
    }
    return 1;
}

/* Validate the entire FLdt before callbacks can collect or rewrite partial state. */
static int flp_walk(const unsigned char *d, size_t n, flpev_t cb, void *ctx) {
    size_t start, end;
    if (!flp_data_bounds(d, n, &start, &end) || !flp_events_walk(d, start, end, NULL, NULL)) return 0;
    return !cb || flp_events_walk(d, start, end, cb, ctx);
}

#endif
