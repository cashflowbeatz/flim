/* Exercise the real preset-load and queue paths against local mock objects. */
#include <assert.h>
#include "../../engine/flim_engine.c"

static uint32_t test_value = 10;
static int writes, saves, fail_load;
static unsigned char image[64], mixer[128], object;
static int tracks = 2;
static uint32_t id;

static uint32_t test_rec(uint32_t recid, uint64_t value, uint32_t flags) {
    assert(recid == id);
    if (flags != REC_GetValue) { test_value = (uint32_t)value; writes++; }
    return test_value;
}

static char test_has_plugin(void *obj) { assert(obj == &object); return 1; }
static void *test_get_plugin(void *obj, void **out) { assert(obj == &object); *out = &object; return *out; }
static void test_clear(void **p) { *p = NULL; }
static int test_save(const wchar_t *file, int mask, void *obj, int flags) {
    (void)file; (void)mask; (void)obj; (void)flags; saves++; return 1;
}

static int test_load(const wchar_t *file, int mask, void *obj, int flags) {
    Unit *u = &g_fxU[1][0];
    QItem snap = {0};
    (void)file; (void)mask; (void)flags; assert(obj == &object);
    assert(u->applyGen & 1);
    /* A long-running load must remain guarded even beyond the cooldown length. */
    assert(unit_applying(u, GetTickCount() + 60000));
    assert(!unit_poll_current(u, u->applyGen, GetTickCount()));
    snap.unit = U_FX; snap.a = 1; snap.mode = SNAP_FORCE;
    do_snap(&snap);
    assert(saves == 0);
    /* Reproduce a poll decision queued just before/during preset loading. */
    enqueue_local_revert(id, 10, u->applyGen - 1);
    enqueue_local_revert(id, 10, u->applyGen);
    test_value = 42;
    if (fail_load) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, NULL);
    return 0;
}

int main(void) {
    QItem load = {0};
    Unit *u = &g_fxU[1][0];
    Slot *s;
    InitializeCriticalSection(&g_qLock);
    InitializeCriticalSection(&g_slotLock);
    g_flBase = (char *)image;
    g_fl.MIXARR = IMG_BASE + 8; g_fl.MIXCNT = IMG_BASE + 16;
    MIX_STRIDE = 64; MIX_SLOTS_OFF = 16;
    *(void **)(image + 8) = mixer; *(void **)(image + 16) = &tracks;
    *(void **)(mixer + 64 + 16) = &object;
    g_rec = test_rec; g_loadPreset = test_load; g_savePreset = test_save;
    g_hasPlugin = test_has_plugin; g_getPlugin = test_get_plugin; g_intfClear = test_clear;
    id = FX_ID(1, 0, 1);
    assert(add_slot(id));
    s = slot_by_id(id); assert(s); s->hasUndo = 1;
    load.unit = U_FX; load.a = 1; load.path = "flim-missing-test-input.fst";
    do_load(&load);
    assert(u->applyGen == 2 && s->last == 42 && !s->hasUndo);
    assert(unit_applying(u, GetTickCount()));
    u->suppressUntil = 0; /* Advance the simulated cooldown without sleeping. */
    assert(!unit_poll_current(u, 0, GetTickCount()));
    assert(unit_poll_current(u, 2, GetTickCount()));
    drain_queue_mainthread();
    assert(writes == 0 && test_value == 42);
    /* Genuine inbound values and current local lock reversions remain enabled. */
    enqueue_apply(id, 43); drain_queue_mainthread();
    assert(writes == 1 && test_value == 43);
    enqueue_local_revert(id, 42, u->applyGen); drain_queue_mainthread();
    assert(writes == 2 && test_value == 42);
    fail_load = 1;
    do_load(&load);
    assert(u->applyGen == 4); /* An FL fault must not leave the unit permanently busy. */
    u->suppressUntil = 0;
    drain_queue_mainthread();
    assert(writes == 2 && test_value == 42);
    u->suppressUntil = 15;
    assert(unit_applying(u, 0xfffffff0u));
    assert(!unit_applying(u, 15));
    puts("Plugin apply guard, stale queue rejection, cooldown wrap, and fault cleanup: OK");
    return 0;
}
