#ifndef FLIM_PLUGIN_H
#define FLIM_PLUGIN_H
#include <stdlib.h>
#include "flim_flp.h"

typedef struct {
    unsigned char id[16];
    int format; /* 2 = VST2 unique ID, 3 = VST3 class ID. */
    char name[256], path[4096];
    size_t eventAt, eventEnd, payloadAt, payloadSize, pathAt, pathSize;
    int wrappers, states, names, invalid;
} FlimPlugin;

static int fp_string(char *out, size_t cap, const unsigned char *p, size_t n) {
    if (n && !p[n - 1]) n--;
    if (n >= cap || memchr(p, 0, n)) return 0;
    memcpy(out, p, n); out[n] = 0;
    return 1;
}

static int fp_wrapper(const unsigned char *p, size_t n) {
    static const char name[] = "Fruity Wrapper";
    size_t i;
    if (n != sizeof(name) * 2) return 0;
    for (i = 0; i < sizeof(name); i++) if (p[i * 2] != (unsigned char)name[i] || p[i * 2 + 1]) return 0;
    return 1;
}

static int fp_event(void *ctx, uint32_t id, const unsigned char *p, size_t n, size_t at, size_t end) {
    FlimPlugin *s = (FlimPlugin *)ctx;
    if (id == 201) { if (fp_wrapper(p, n)) s->wrappers++; else s->invalid = 1; }
    if (id == 213) {
        size_t o = 4;
        s->states++; s->eventAt = at; s->eventEnd = end;
        s->payloadAt = end - n; s->payloadSize = n;
        if (n < 4 || flp_u32(p) != 8) { s->invalid = 1; return 1; }
        while (o < n) {
            uint32_t tag; uint64_t len;
            size_t start = o;
            if (n - o < 12) { s->invalid = 1; break; }
            tag = flp_u32(p + o);
            len = (uint64_t)flp_u32(p + o + 4) | (uint64_t)flp_u32(p + o + 8) << 32;
            o += 12;
            if (len > n - o) { s->invalid = 1; break; }
            if (tag == 51 || tag == 52) {
                size_t need = tag == 51 ? 4 : 16;
                if (s->format || len != need) s->invalid = 1;
                else {
                    size_t j; unsigned nonzero = 0;
                    s->format = tag == 51 ? 2 : 3; memcpy(s->id, p + o, need);
                    for (j = 0; j < need; j++) nonzero |= s->id[j];
                    if (!nonzero) s->invalid = 1;
                }
            } else if (tag == 54) {
                if (s->names++ || !fp_string(s->name, sizeof(s->name), p + o, (size_t)len)) s->invalid = 1;
            } else if (tag == 55) {
                if (s->pathAt || !fp_string(s->path, sizeof(s->path), p + o, (size_t)len)) s->invalid = 1;
                s->pathAt = start; s->pathSize = 12 + (size_t)len;
            }
            o += (size_t)len;
        }
    }
    return 1;
}

/* Only a single, fully understood wrapper is eligible for path adaptation. */
static int flim_plugin_read(const unsigned char *d, size_t n, FlimPlugin *s) {
    memset(s, 0, sizeof(*s));
    return flp_walk(d, n, fp_event, s) && !s->invalid && s->wrappers == 1 && s->states == 1 && s->format;
}

static int flim_plugin_same(const FlimPlugin *a, const FlimPlugin *b) {
    return a->format && a->format == b->format && !memcmp(a->id, b->id, a->format == 2 ? 4 : 16);
}

static void fp_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static size_t fp_var(unsigned char *p, size_t n) {
    size_t o = 0;
    do { p[o++] = (unsigned char)((n & 127) | (n > 127 ? 128 : 0)); n >>= 7; } while (n);
    return o;
}

/* Change tag 55 only. The class ID, wrapper flags and opaque sound chunk stay byte-identical. */
static unsigned char *flim_plugin_path(const unsigned char *d, size_t n, const char *path, size_t limit, size_t *outn) {
    FlimPlugin s;
    unsigned char var[8], *out, *p;
    size_t plen = strlen(path), payload, vn, size, start, end, pos;
    *outn = 0;
    if (!plen || plen >= sizeof(s.path) || !flim_plugin_read(d, n, &s) || !flp_data_bounds(d, n, &start, &end)) return NULL;
    payload = s.payloadSize - s.pathSize + 12 + plen;
    if (payload > UINT32_MAX) return NULL;
    vn = fp_var(var, payload);
    size = n - (s.eventEnd - s.eventAt) + 1 + vn + payload;
    if (size > limit || end - start + size - n > UINT32_MAX) return NULL;
    out = (unsigned char *)malloc(size);
    if (!out) return NULL;
    memcpy(out, d, s.eventAt); p = out + s.eventAt;
    *p++ = 213; memcpy(p, var, vn); p += vn;
    pos = s.pathAt ? s.pathAt : s.payloadSize;
    memcpy(p, d + s.payloadAt, pos); p += pos;
    fp_u32(p, 55); fp_u32(p + 4, (uint32_t)plen); fp_u32(p + 8, 0); p += 12;
    memcpy(p, path, plen); p += plen;
    memcpy(p, d + s.payloadAt + pos + s.pathSize, s.payloadSize - pos - s.pathSize);
    p += s.payloadSize - pos - s.pathSize;
    memcpy(p, d + s.eventEnd, n - s.eventEnd);
    fp_u32(out + start - 4, (uint32_t)(end - start + size - n));
    *outn = size;
    return out;
}
#endif
