/* Exercise the real preset-load and queue paths against local mock objects. */
#include <assert.h>
#include "../../engine/flim_engine.c"

static uint32_t test_value = 10;
static int writes, saves, fail_load, plugin_present = 1, clear_mode;
static unsigned char image[64], mixer[128], object;
static int tracks = 2;
static uint32_t id;

static uint32_t test_rec(uint32_t recid, uint64_t value, uint32_t flags) {
    assert(recid == id);
    if (flags != REC_GetValue) { test_value = (uint32_t)value; writes++; }
    return test_value;
}

static char test_has_plugin(void *obj) { assert(obj == &object); return (char)plugin_present; }
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
    if (clear_mode) {
        if (clear_mode == 1) plugin_present = 0;
        if (clear_mode == 4) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, NULL);
        return clear_mode == 2 ? 1 : 0;
    }
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
    if (fail_load == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, NULL);
    return fail_load == 2 ? 1 : 0;
}

static void preset_file(const char *path, const char *data) {
    FILE *file = NULL; assert(!fopen_s(&file, path, "wb") && file);
    assert(fwrite(data, 1, strlen(data), file) == strlen(data)); assert(!fclose(file));
}

static void wrapper_file(const char *path) {
    unsigned char data[256] = {0}, state[64] = {0}; size_t at = 22, n; FILE *file = NULL;
    memcpy(data,"FLhd",4); fp_u32(data+4,6); memcpy(data+14,"FLdt",4);
    data[at++]=201; n=sizeof(L"Fruity Wrapper"); at+=fp_var(data+at,n); memcpy(data+at,L"Fruity Wrapper",n); at+=n;
    fp_u32(state,8); fp_u32(state+4,52); fp_u32(state+8,16); state[16]=7;
    data[at++]=213; at+=fp_var(data+at,32); memcpy(data+at,state,32); at+=32; fp_u32(data+18,(unsigned)(at-22));
    assert(!fopen_s(&file,path,"wb") && file); assert(fwrite(data,1,at,file)==at); assert(!fclose(file));
}

int main(void) {
    QItem load = {0};
    Unit *u = &g_fxU[1][0];
    Slot *s;
    char temp[MAX_PATH], path[MAX_PATH];
    int mode;
    InitializeCriticalSection(&g_lifecycleLock);
    InitializeCriticalSection(&g_qLock);
    InitializeCriticalSection(&g_heldLock);
    InitializeCriticalSection(&g_netTextLock);
    InitializeCriticalSection(&g_ulockLock);
    InitializeCriticalSection(&g_slotLock);
    InitializeCriticalSection(&g_sendLock);
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
    GetTempPathA(MAX_PATH, temp); assert(GetTempFileNameA(temp, "fpa", 0, path));
    preset_file(path, "preset");
    load.unit = U_FX; load.a = 1; load.path = path;
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
    /* A reconnect invalidates both queued writes and work still owned by an old worker. */
    enqueue_apply(id, 99);
    InterlockedIncrement(&g_netGen);
    drain_queue_mainthread();
    assert(writes == 2 && test_value == 42);
    g_workBound = 1; g_workGen = g_netGen - 1;
    enqueue_apply(id, 100);
    g_workBound = 0;
    drain_queue_mainthread();
    assert(writes == 2 && test_value == 42);
    pend_put(id, 101);
    InterlockedIncrement(&g_netGen);
    retry_pending(); drain_queue_mainthread();
    assert(g_npend == 0 && writes == 2 && test_value == 42);
    pend_put(id, 44);
    retry_pending(); drain_queue_mainthread();
    assert(g_npend == 0 && writes == 3 && test_value == 44);
    test_value = 42; writes = 2;
    preset_file(path, "rejected");
    u->hash = 1234; u->baselineAt = 456; s->last = 77; s->hasUndo = 1;
    fail_load = 1;
    do_load(&load);
    assert(u->applyGen == 4); /* An FL fault must not leave the unit permanently busy. */
    assert(g_compatFault && u->hash == 1234 && u->baselineAt == 456 && s->last == 77 && s->hasUndo);
    assert(g_plugLastFx[1][0].n == 6 && !memcmp(g_plugLastFx[1][0].d, "preset", 6));
    u->suppressUntil = 0;
    drain_queue_mainthread();
    assert(writes == 2 && test_value == 42);
    g_compatFault = 0; g_compatReady = 1;
    fail_load = 2; do_load(&load);
    assert(!g_compatFault && g_compatReady && u->applyGen == 6 && u->unavailable && g_pluginMissing == 1);
    assert(g_plugLastFx[1][0].n == 8 && !memcmp(g_plugLastFx[1][0].d, "rejected", 8));
    assert(plug_param_blocked(id) && !plug_param_blocked(MIX_ID(1, 0x1fc0)));
    enqueue_apply(id, 99); assert(route_inbound(id, 100, 0)); retry_pending(); drain_queue_mainthread();
    assert(g_npend == 1 && g_pend[0].v == 100 && writes == 2 && test_value == 42);
    { int count = saves; QItem snap = load; snap.mode = SNAP_FORCE; do_snap(&snap); assert(saves == count); }
    do_load(&load); assert(u->applyGen == 6); /* Do not retry a rejected chunk in a loop. */
    /* A deliberate retry restores sharing and fresh parameter baselines. */
    fail_load = 0; load.mode = 2; do_load(&load); load.mode = 0;
    assert(!u->unavailable && !g_pluginMissing && u->applyGen == 8 && !s->hasUndo);
    retry_pending(); drain_queue_mainthread();
    assert(!g_npend && writes == 3 && test_value == 100);
    u->hash = 1234; s->last = 77;
    drain_queue_mainthread();
    g_compatFault = 0; g_compatReady = 1;
    assert(DeleteFileA(path)); do_load(&load);
    assert(g_compatFault && u->applyGen == 8 && u->hash == 1234 && s->last == 77);
    for (mode = 2; mode <= 4; mode++) {
        LONG generation = u->applyGen;
        g_compatFault = 0; g_compatReady = 1; clear_mode = mode;
        do_fxclear(&load);
        assert(g_compatFault && u->applyGen == generation + 2 && !(u->applyGen & 1));
        assert(u->hash == 1234 && g_plugLastFx[1][0].n == 8 && plugin_present);
    }
    g_compatFault = 0; g_compatReady = 1; clear_mode = 1;
    { LONG generation = u->applyGen; do_fxclear(&load);
      assert(!g_compatFault && u->applyGen == generation + 2 && !plugin_present && !u->hash && !g_plugLastFx[1][0].known); }
    u->suppressUntil = 15;
    assert(unit_applying(u, 0xfffffff0u));
    assert(!unit_applying(u, 15));
    s->hasUndo = 1; s->undo = 123; s->lockedByMe = s->lockedByPeer = 1;
    net_reset();
    assert(!s->hasUndo && !s->undo && !s->lockedByMe && !s->lockedByPeer);
    /* Channel deletion/reorder must not consume unrelated local metadata edits. */
    g_mHash[M_PAT][4] = 10; g_mSeen[M_PAT][4] = 11; g_mAt[M_PAT][4] = 123;
    g_mHash[M_MIX][1] = 20; g_mSeen[M_MIX][1] = 21; g_mAt[M_MIX][1] = 124;
    meta_rebaseline(1);
    assert(g_mHash[M_PAT][4] == 10 && g_mSeen[M_PAT][4] == 11 && g_mAt[M_PAT][4] == 123);
    assert(g_mHash[M_MIX][1] == 20 && g_mSeen[M_MIX][1] == 21 && g_mAt[M_MIX][1] == 124);
    g_chU[1].unavailable = 1; plug_agreed_set(U_CH, 1, 0, (unsigned char *)"missing", 7);
    g_chU[2].unavailable = 1; plug_agreed_set(U_CH, 2, 0, (unsigned char *)"other", 5);
    pend_put((1u << 16) | REC_PLUG_FIRST, 300); pend_put((2u << 16) | REC_PLUG_FIRST, 400);
    { int from[] = {2, 0, 1}; units_permute(from, 3); }
    assert(g_chU[0].unavailable && g_chU[2].unavailable && g_pluginMissing == 2);
    assert(!memcmp(g_plugLastCh[0].d, "other", 5) && !memcmp(g_plugLastCh[2].d, "missing", 7));
    assert(g_npend == 2 && g_pend[0].id == ((2u << 16) | REC_PLUG_FIRST) && g_pend[1].id == REC_PLUG_FIRST);
    units_shift_after_delete(0, 3);
    assert(!g_chU[0].unavailable && g_chU[1].unavailable && g_pluginMissing == 1);
    assert(!memcmp(g_plugLastCh[1].d, "missing", 7));
    assert(g_npend == 1 && g_pend[0].id == ((1u << 16) | REC_PLUG_FIRST) && g_pend[0].v == 300);
    /* No native loader call is permitted for a retained newer-build preset. */
    preset_file(path,"newer state"); load.mode=3; strcpy_s(load.presetFl,sizeof(load.presetFl),"26.1.7.5654");
    { LONG generation=u->applyGen; do_load(&load); assert(!g_compatFault && u->unavailable==2 && u->applyGen==generation);
      char *json=unit_json_fields(U_FX,1,0,(unsigned char *)"newer state",11); assert(json && strstr(json,"\"preset_fl\":\"26.1.7.5654\"")); free(json); }
    /* Identical bytes can acquire a newer authenticated origin after an earlier rejection. */
    u->unavailable=1; u->retryStamp=123;
    strcpy_s(u->presetFl,sizeof(u->presetFl),"25.2.5.5319");
    { LONG generation=u->applyGen; do_load(&load);
      assert(!g_compatFault && u->unavailable==2 && !u->retryStamp && u->applyGen==generation);
      assert(!strcmp(u->presetFl,"26.1.7.5654"));
      plugins_retry(); assert(u->applyGen==generation && u->unavailable==2); }
    /* Recovery runs automatically only after this matching binary changes. */
    { char binary[MAX_PATH]; FlimPlugin identity; unsigned char *bytes; size_t count; LONG generation;
      strcpy_s(g_tmpDir,sizeof(g_tmpDir),temp);
      assert(GetTempFileNameA(temp,"bin",0,binary)); preset_file(binary,"version 1"); wrapper_file(path);
      bytes=read_file(path,&count); assert(bytes && flim_plugin_read(bytes,count,&identity)); free(bytes);
      plugin_catalog_clear(); g_pluginCatalogRead=1; g_pluginCatalogStamp=plugin_catalog_stamp();
      g_pluginCatalog=(PluginEntry *)calloc(1,sizeof(PluginEntry)); assert(g_pluginCatalog);
      memcpy(g_pluginCatalog[0].id,identity.id,16); g_pluginCatalog[0].format=3; g_pluginCatalog[0].unit=U_FX;
      g_pluginCatalog[0].path=_strdup(binary); g_pluginCatalogN=1; g_pluginCatalogFormats[U_FX][1]=1;
      plugin_present=1; clear_mode=0; fail_load=2; load.mode=2; do_load(&load); assert(u->unavailable==1);
      generation=u->applyGen; plugins_retry(); assert(u->applyGen==generation && u->unavailable==1);
      preset_file(binary,"version 2 changed"); fail_load=0; plugins_retry();
      assert(!g_compatFault && !u->unavailable && u->applyGen==generation+2);
      assert(DeleteFileA(binary)); plugin_catalog_clear(); }
    assert(DeleteFileA(path));
    plug_agreed_reset(1, 1);
    puts("Plugin apply guards, rejection isolation/retry, blocked parameters, retained state through reorder/delete, and verified effect clearing: OK");
    return 0;
}
