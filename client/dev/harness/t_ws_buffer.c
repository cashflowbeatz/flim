#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int fail_alloc;
static void *test_realloc(void *p, size_t n) {
    return fail_alloc ? NULL : realloc(p, n);
}
#define FLIM_WS_REALLOC test_realloc
#include "../../common/flim_ws_buffer.h"

int main(void) {
    unsigned char *buf = NULL, *before;
    size_t cap = 0, old_cap, len;
    assert(flim_ws_reserve(&buf, &cap, 0));
    assert(cap >= 65537 && cap <= FLIM_WS_MAX_MESSAGE + 2u);
    buf[0] = 42;
    before = buf;
    old_cap = cap;
    fail_alloc = 1;
    assert(!flim_ws_reserve(&buf, &cap, cap - 1));
    assert(buf == before && cap == old_cap && buf[0] == 42);
    fail_alloc = 0;
    for (len = 0; len < FLIM_WS_MAX_MESSAGE; len += 65536) {
        assert(flim_ws_reserve(&buf, &cap, len));
        assert(cap > len + 1 && cap <= FLIM_WS_MAX_MESSAGE + 2u);
    }
    assert(flim_ws_reserve(&buf, &cap, FLIM_WS_MAX_MESSAGE));
    assert(cap == FLIM_WS_MAX_MESSAGE + 2u);
    assert(flim_ws_reserve(&buf, &cap, 64u * 1024u * 1024u + 512u));
    assert(cap > 64u * 1024u * 1024u + 512u); // maximum accepted input plus relay metadata
    assert(!flim_ws_reserve(&buf, &cap, FLIM_WS_MAX_MESSAGE + 1u));
    assert(!flim_ws_reserve(&buf, &cap, (size_t)-1));
    free(buf);
    puts("WebSocket buffer: OK");
    return 0;
}
