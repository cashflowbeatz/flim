#include <assert.h>
#include <stdio.h>
#include "../../common/flim_flp.h"

static unsigned char image[2048];
static const unsigned char stream[] = {
    1, 42, 64, 1, 2, 128, 1, 2, 3, 4,
    0xac, 1, 1, 0, 0, 17,
    0xac, 2, 1, 0, 0x40, 18, 19,
    0xac, 3, 1, 0, 0x80, 20, 21, 22, 23,
    0xac, 1, 1, 0, 0xc0, 3, 'a', 'b', 0,
    196, 4, 'X', 0, 0, 0
};
static const uint32_t ids[] = {1, 64, 128, 0x101, 0x40000102, 0x80000103, 0xc0000101, 196};
static const size_t sizes[] = {1, 2, 4, 1, 2, 4, 3, 4};
static size_t seen;

static void u32(unsigned char *p, uint32_t n) {
    p[0] = (unsigned char)n; p[1] = (unsigned char)(n >> 8);
    p[2] = (unsigned char)(n >> 16); p[3] = (unsigned char)(n >> 24);
}

static size_t fixture(const unsigned char *p, size_t n) {
    memcpy(image, "FLhd", 4); u32(image + 4, 6); memset(image + 8, 0, 6);
    memcpy(image + 14, "FLdt", 4); u32(image + 18, (uint32_t)n);
    memcpy(image + 22, p, n);
    return n + 22;
}

static int check(void *ctx, uint32_t id, const unsigned char *p, size_t n, size_t at, size_t end) {
    (void)ctx;
    assert(seen < sizeof(ids) / sizeof(ids[0]));
    assert(id == ids[seen] && n == sizes[seen]);
    assert(end == (size_t)(p - image) + n && at < (size_t)(p - image));
    seen++;
    return 1;
}

static int count(void *ctx, uint32_t id, const unsigned char *p, size_t n, size_t at, size_t end) {
    (void)id; (void)p; (void)n; (void)at; (void)end;
    seen++;
    return ctx == NULL;
}

static void invalid(const unsigned char *p, size_t n) {
    size_t total = fixture(p, n);
    seen = 0;
    assert(!flp_walk(image, total, count, (void *)1));
    assert(seen == 0); /* Even early-stop callbacks cannot observe invalid input. */
}

int main(void) {
    size_t n = fixture(stream, sizeof(stream)), i;
    assert(flp_walk(image, n, check, NULL) && seen == 8);
    seen = 0;
    assert(flp_walk(image, n, count, (void *)1) && seen == 1);
    for (i = 0; i < n; i++) assert(!flp_walk(image, i, NULL, NULL));
    memcpy(image + n, "TAIL", 4);
    seen = 0;
    assert(flp_walk(image, n + 4, check, NULL) && seen == 8);
    u32(image + 4, UINT32_MAX);
    assert(!flp_walk(image, n, NULL, NULL));
    n = fixture(stream, sizeof(stream));
    u32(image + 18, UINT32_MAX);
    assert(!flp_walk(image, n, NULL, NULL));
    invalid((const unsigned char *)"\x01\x00\xac", 3);
    invalid((const unsigned char *)"\xac\x01\x01\x00\x10\x00", 6);
    invalid((const unsigned char *)"\x80\x01\x02\x03", 4);
    invalid((const unsigned char *)"\xc4\x04\x00", 3);
    invalid((const unsigned char *)"\xc4\x80", 2);
    invalid((const unsigned char *)"\xc4\x80\x80\x80\x80\x10", 6);
    invalid((const unsigned char *)"\xc4\x80\x80\x80\x80\x80\x00", 7);
    n = fixture((const unsigned char *)"\xc4\x00", 2);
    assert(flp_walk(image, n, NULL, NULL));
    puts("FLP legacy/extended events, bounds, callback atomicity: OK");
    return 0;
}
