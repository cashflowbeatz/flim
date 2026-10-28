#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FLIM_CLIP_FL24_SIZE 60u
#define FLIM_CLIP_WIRE_SIZE 80u

/* FL25's native clip initializer appends these fields to FL24's 60-byte record.
   Local static evidence: fl24-wire-static.json, FLEngine 25.2.5.5319 VA 0xf72000. */
static const unsigned char flim_clip_extension_default[20] = {
    0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0xf0, 0x3f,
    0, 0, 0, 0,
    0, 0, 0, 0
};

static int flim_clip_size_valid(size_t native_size) {
    return native_size == FLIM_CLIP_FL24_SIZE || native_size == FLIM_CLIP_WIRE_SIZE;
}

static int flim_clip_from_native(void *wire, const void *native, size_t native_size) {
    if (!wire || !native || !flim_clip_size_valid(native_size)) return 0;
    memmove(wire, native, native_size);
    if (native_size == FLIM_CLIP_FL24_SIZE)
        memcpy((unsigned char *)wire + FLIM_CLIP_FL24_SIZE, flim_clip_extension_default,
               sizeof(flim_clip_extension_default));
    return 1;
}

/* Check the complete list before purging or adding any native clips. */
static int flim_clips_compatible(const void *wire, size_t count, size_t native_size) {
    const unsigned char *record = (const unsigned char *)wire;
    size_t i;
    if (!flim_clip_size_valid(native_size) || count > SIZE_MAX / FLIM_CLIP_WIRE_SIZE ||
        (!wire && count)) return 0;
    if (native_size == FLIM_CLIP_WIRE_SIZE) return 1;
    for (i = 0; i < count; i++, record += FLIM_CLIP_WIRE_SIZE)
        if (memcmp(record + FLIM_CLIP_FL24_SIZE, flim_clip_extension_default,
                   sizeof(flim_clip_extension_default))) return 0;
    return 1;
}

static int flim_clip_to_native(void *native, const void *wire, size_t native_size) {
    if (!native || !wire || !flim_clips_compatible(wire, 1, native_size)) return 0;
    memmove(native, wire, native_size);
    return 1;
}
