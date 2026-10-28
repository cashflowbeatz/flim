#include <assert.h>
#include <stdio.h>
#include "../../common/flim_clip.h"

static unsigned checks;

static void check(int result) { assert(result); checks++; }

int main(void) {
    unsigned char native24[60], native25[80], wire[80], back24[60], back25[80];
    unsigned char list[3 * 80], untouched[80], overlap[100], expected[80];
    const size_t invalid_sizes[] = {0, 1, 59, 61, 79, 81, SIZE_MAX};
    size_t i;
    for (i = 0; i < sizeof(native24); i++) native24[i] = (unsigned char)(i * 37 + 11);
    for (i = 0; i < sizeof(native25); i++) native25[i] = (unsigned char)(i * 19 + 7);

    check(flim_clip_from_native(wire, native24, sizeof(native24)));
    check(!memcmp(wire, native24, sizeof(native24)));
    check(!memcmp(wire + 60, "\0\0\0\0\0\0\0\0\0\0\xf0\x3f\0\0\0\0\0\0\0\0", 20));
    check(flim_clip_to_native(back24, wire, sizeof(back24)));
    check(!memcmp(back24, native24, sizeof(native24)));
    check(flim_clip_from_native(back25, wire, sizeof(back25)));
    check(!memcmp(back25, wire, sizeof(wire)));
    memcpy(expected, wire, sizeof(wire));

    check(flim_clip_from_native(wire, native25, sizeof(native25)));
    check(!memcmp(wire, native25, sizeof(native25)));
    check(flim_clip_to_native(back25, wire, sizeof(back25)));
    check(!memcmp(back25, native25, sizeof(native25)));
    memset(untouched, 0xa5, sizeof(untouched));
    memcpy(back25, untouched, sizeof(back25));
    check(!flim_clip_to_native(back25, wire, sizeof(back24)));
    check(!memcmp(back25, untouched, sizeof(back25)));

    for (i = 60; i < 80; i++) {
        memcpy(wire, expected, sizeof(wire)); wire[i] ^= 1;
        memcpy(back25, untouched, sizeof(back25));
        check(!flim_clip_to_native(back25, wire, sizeof(back24)));
        check(!memcmp(back25, untouched, sizeof(back25)));
    }
    for (i = 0; i < sizeof(invalid_sizes) / sizeof(invalid_sizes[0]); i++) {
        memcpy(back25, untouched, sizeof(back25));
        check(!flim_clip_from_native(back25, native25, invalid_sizes[i]));
        check(!memcmp(back25, untouched, sizeof(back25)));
        check(!flim_clip_to_native(back25, expected, invalid_sizes[i]));
        check(!memcmp(back25, untouched, sizeof(back25)));
        check(!flim_clips_compatible(expected, 1, invalid_sizes[i]));
    }
    check(!flim_clip_from_native(NULL, native24, 60));
    check(!flim_clip_from_native(wire, NULL, 60));
    check(!flim_clip_to_native(NULL, expected, 60));
    check(!flim_clip_to_native(back24, NULL, 60));
    check(flim_clips_compatible(NULL, 0, 60));
    check(flim_clips_compatible(NULL, 0, 80));
    check(!flim_clips_compatible(NULL, 1, 60));
    check(!flim_clips_compatible(expected, SIZE_MAX / 80 + 1, 60));
    check(!flim_clips_compatible(expected, SIZE_MAX / 80 + 1, 80));

    for (i = 0; i < 3; i++) memcpy(list + i * 80, expected, 80);
    check(flim_clips_compatible(list, 3, 60));
    list[2 * 80 + 72] = 1;
    check(!flim_clips_compatible(list, 3, 60));
    check(flim_clips_compatible(list, 3, 80));

    memset(overlap, 0xa5, sizeof(overlap));
    memcpy(overlap + 10, native24, sizeof(native24));
    check(flim_clip_from_native(overlap, overlap + 10, 60));
    check(!memcmp(overlap, expected, 80));
    check(overlap[80] == 0xa5);
    memcpy(overlap, expected, 80);
    check(flim_clip_to_native(overlap + 7, overlap, 60));
    check(!memcmp(overlap + 7, native24, 60));
    memcpy(overlap + 7, native25, 80);
    check(flim_clip_from_native(overlap, overlap + 7, 80));
    check(!memcmp(overlap, native25, 80));

    printf("Canonical clip conversion: OK (%u checks; 60/80-byte roundtrips, exact defaults, loss refusal and preflight)\n", checks);
    return 0;
}
