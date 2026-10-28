#include <assert.h>
#include <stdlib.h>
#include <windows.h>

static int fail_calloc;
static void *checked_calloc(size_t count, size_t size) { return fail_calloc ? NULL : calloc(count, size); }
#define calloc checked_calloc
#include "../../engine/flim_engine.c"
#undef calloc

static unsigned char image[128], channels[32], channel;
static void *channel_items[MAX_CH], *channel_list;
static int added, loaded;
static uint64_t marker_memory[1 + MARK_MAX * MARK_SIZE / 8 + 1];
static unsigned char patterns[MAX_PAT * 64], mixer_records[3 * 4096];
static void *playlist_form, *mixer_object;
static int marker_clears, marker_adds, route_changes, tracks = 3;
static CRITICAL_SECTION song_cs;
static unsigned char project_holder[32], project_object[16];
static void *project_holder_pointer;
static int pattern_calls, pattern_insert, pattern_fault;

static void reset(void) {
    while (g_qh != g_qt) { discard_task(&g_q[g_qh]); g_qh = (g_qh + 1) % QCAP; }
    fail_calloc = 0; g_qh = g_qt = 0; g_compatFault = 0; g_compatReady = 1;
    g_netGen = 1; g_workBound = 0; g_ws = NULL; g_alert[0] = 0;
    memset(image, 0, sizeof(image)); memset(channels, 0, sizeof(channels));
    memset(&g_fl, 0, sizeof(g_fl)); g_fl.build = L"25.2.5.5319";
    g_flBase = (char *)image; g_fl.CHANLIST = IMG_BASE + 8;
    channel_list = channels; *(void **)(image + 8) = &channel_list;
    *(void **)(channels + 8) = channel_items;
    channel_items[0] = &channel; g_nslot = 0; added = loaded = 0;
}
static void stopped(void) { assert(g_compatFault && !g_compatReady && strstr(g_alert, "stopped sharing")); }
static void no_files(void) {
    char query[MAX_PATH]; WIN32_FIND_DATAA entry;
    sprintf_s(query, sizeof(query), "%s\\*.fst", g_tmpDir);
    HANDLE found = FindFirstFileA(query, &entry);
    assert(found == INVALID_HANDLE_VALUE);
}
static char *queue_path(const char *name) {
    char path[MAX_PATH]; sprintf_s(path, sizeof(path), "%s\\%s.fst", g_tmpDir, name); return _strdup(path);
}
static char fake_has_plugin(void *object) { (void)object; return 0; }
static int fake_resolve(int index, char full, void **out) {
    (void)full; *out = index == 0 ? &channel : NULL; return *out != NULL;
}
static void *fake_add(int a, int b, int c) {
    (void)a; (void)b; (void)c; added++;
    *(int *)(channels + 0x10) = 1;
    return &channel;
}
static int fake_load(const wchar_t *path, int mask, void *object, int flags) {
    (void)path; (void)mask; (void)flags;
    assert(object == &channel && g_qh != g_qt && g_q[g_qh].type == T_DELCH);
    loaded++;
    return 0;
}
static uint64_t function_va(void (*function)(void)) { return IMG_BASE + (uintptr_t)function - (uintptr_t)g_flBase; }
static void fake_clear_markers(void *array, void *type, int64_t dims, int64_t length) {
    int k; char **list = (char **)array;
    (void)type; (void)dims; assert(!length);
    for (k = 0; k < (int)marker_memory[0]; k++) dstr_free(*(wchar_t **)((char *)&marker_memory[1] + k * MARK_SIZE + 8));
    marker_memory[0] = 0; *list = (char *)&marker_memory[1]; marker_clears++;
}
static void fake_add_marker(void *array, int pos, const wchar_t *name, int action, uint8_t num, uint8_t den) {
    char **list = (char **)array; char *record = *list + (size_t)marker_memory[0] * MARK_SIZE;
    memset(record, 0, MARK_SIZE); *(int *)record = pos; *(int *)(record + 4) = action;
    *(const wchar_t **)(record + 8) = name; record[16] = num; record[17] = den;
    marker_memory[0]++; marker_adds++;
}
static void fake_form(void *form) { (void)form; }
static void fake_patlen(int pattern, int flag) { (void)pattern; (void)flag; }
static void fake_modified(int modified) { (void)modified; }
static int fake_route(void *mixer, int src, int dst, int on, int update) {
    (void)mixer; (void)update;
    mixer_records[src * MIX_STRIDE + MIX_ROUTE + dst * 8 + 4] = (char)on; route_changes++; return 1;
}
static void fake_uiupdate(int what, uint8_t flag) { (void)what; (void)flag; }
static void metadata_fixture(void) {
    reset(); memset(patterns, 0, sizeof(patterns)); memset(marker_memory, 0, sizeof(marker_memory));
    memset(mixer_records, 0, sizeof(mixer_records));
    g_fl.PATARR = IMG_BASE + 16; PAT_STRIDE = 64; *(void **)(image + 16) = patterns;
    g_fl.SONGLOCK = IMG_BASE + 24; *(void **)(image + 24) = &song_cs;
    g_fl.MARKTYPE = IMG_BASE + 32;
    g_fl.MIXARR = IMG_BASE + 40; *(void **)(image + 40) = mixer_records;
    g_fl.MIXCNT = IMG_BASE + 48; *(void **)(image + 48) = &tracks;
    g_fl.MIXOBJ = IMG_BASE + 56; mixer_object = mixer_records; *(void **)(image + 56) = &mixer_object;
    g_fl.PLFORM = IMG_BASE + 64; playlist_form = NULL; *(void **)(image + 64) = &playlist_form;
    g_fl.FLPROJ = IMG_BASE + 72; project_holder_pointer = project_holder; *(void **)(image + 72) = &project_holder_pointer;
    *(void **)(project_holder + 16) = project_object;
    g_fl.NOTELOCK = IMG_BASE + 80; *(void **)(image + 80) = &song_cs;
    g_fl.ARRCUR = IMG_BASE + 88; g_fl.ARRS = IMG_BASE + 96;
    MIX_STRIDE = 4096;
#undef MIX_ROUTE
    g_fl.MIX_ROUTE = 32;
#define MIX_ROUTE (g_fl.MIX_ROUTE)
    g_fl.MARKADD = function_va((void (*)(void))fake_add_marker);
    g_fl.DYNSETLEN = function_va((void (*)(void))fake_clear_markers);
    g_fl.MARKSORT = g_fl.PATMARKS = g_fl.ROUTEUPD = function_va((void (*)(void))fake_form);
    g_fl.PATLENUPD = function_va((void (*)(void))fake_patlen);
    g_fl.MODIFIED = function_va((void (*)(void))fake_modified);
    g_fl.SETROUTE = function_va((void (*)(void))fake_route);
    g_fl.UIUPDATE = function_va((void (*)(void))fake_uiupdate);
    *(void **)(patterns + 64 + PAT_MARKERS) = &marker_memory[1];
    marker_memory[0] = 1; *(int *)&marker_memory[1] = 88;
    marker_clears = marker_adds = route_changes = 0; g_isLeader = 0;
    memset(g_mAt, 0, sizeof(g_mAt)); memset(g_mHash, 0, sizeof(g_mHash)); memset(g_mSeen, 0, sizeof(g_mSeen));
}
static void clips(void) {
    const char *bad[] = {
        "{\"op\":{\"arr\":0,\"data\":\"%%%%\"}}",
        "{\"op\":{\"arr\":0,\"data\":\"AA==\"}}",
        "{\"op\":{\"arr\":-1,\"data\":\"\"}}",
        "{\"op\":{\"arr\":\"0\",\"data\":\"\"}}",
        "{\"op\":{\"arr\":2147483648,\"data\":\"\"}}",
        "{\"op\":{\"arr\":0,\"nested\":{\"data\":\"\"}}}",
        "{\"op\":{\"arr\":0,\"data\":\"\",\"data\":\"\"}}"
    };
    int i; char encoded[128], message[256]; unsigned char record[CLIP_SIZE] = {0};
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) { reset(); on_clips_set(bad[i]); stopped(); assert(g_qh == g_qt); }
    reset(); on_clips_set("{\"op\":{\"data\": \"\", \"arr\":0}}");
    assert(!g_compatFault && g_qt == 1 && g_q[0].type == T_CLIPS && !g_q[0].mode);
    b64enc(record, sizeof(record), encoded);
    sprintf_s(message, sizeof(message), "{\"op\":{\"data\":\"%s\",\"arr\":2}}", encoded);
    reset(); on_clips_set(message);
    assert(!g_compatFault && g_qt == 1 && g_q[0].a == 2 && g_q[0].mode == 1);
    puts("PASS invalid playlist data never becomes an empty replacement; valid reordered fields and intentional clears work");
}
static void plugins(void) {
    const char *bad[] = {
        "{}", "{\"op\":{\"nch\":0}}", "{\"op\":{\"nch\":1,\"items\":[]}}",
        "{\"op\":{\"nch\":-1,\"items\":[]}}",
        "{\"op\":{\"nch\":0,\"items\":[{\"unit\":\"fx\",\"trk\":0,\"slot\":0,\"data\":\"%%%%\"}]}}",
        "{\"op\":{\"nch\":1,\"items\":[{\"unit\":\"ch\",\"ch\":0,\"data\":\"aGVsbG8=\"},{}]}}",
        "{\"op\":{\"nch\":1,\"items\":[{\"unit\":\"ch\",\"data\":\"aGVsbG8=\"},{\"ch\":0}]}}",
        "{\"op\":{\"nch\":1,\"items\":[{\"unit\":\"ch\",\"ch\":0,\"data\":\"aGVsbG8=\"},{\"unit\":\"ch\",\"ch\":0,\"data\":\"aGVsbG8=\"}]}}",
        "{\"op\":{\"nch\":0,\"items\":[{\"unit\":\"fx\",\"trk\":0,\"slot\":10,\"data\":\"aGVsbG8=\"}]}}"
    };
    const char *valid = "{\"op\":{\"items\":[{\"data\":\"aGVsbG8=\",\"ch\":1,\"unit\":\"ch\"},{\"slot\":1,\"trk\":0,\"unit\":\"fx\",\"data\":\"aGVsbG8=\"},{\"data\":\"aGVsbG8=\",\"unit\":\"ch\",\"ch\":0}],\"nch\":2}}";
    const char *bad_unit[] = {
        "{\"op\":{\"unit\":\"unknown\",\"ch\":0,\"data\":\"aGVsbG8=\"}}",
        "{\"op\":{\"unit\":\"ch\",\"data\":\"aGVsbG8=\"}}",
        "{\"op\":{\"unit\":\"ch\",\"ch\":256,\"data\":\"aGVsbG8=\"}}",
        "{\"op\":{\"unit\":\"ch\",\"ch\":0,\"data\":\"\"}}",
        "{\"op\":{\"unit\":\"fx\",\"trk\":0,\"slot\":0,\"data\":\"aGVsbG9=\"}}",
        "{\"op\":{\"kind\":\"chan.add\",\"unit\":\"fx\",\"trk\":0,\"slot\":0,\"data\":\"aGVsbG8=\"}}"
    };
    int i, unit, a, b; char *path = NULL;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        reset(); *(int *)(channels + 0x10) = 2;
        apply_plug_full(bad[i]); stopped(); assert(g_qh == g_qt); no_files();
    }
    for (i = 0; i < sizeof(bad_unit) / sizeof(bad_unit[0]); i++) {
        reset(); assert(!parse_unit(bad_unit[i], &unit, &a, &b, &path)); stopped(); assert(!path); no_files();
    }
    reset(); assert(parse_unit("{\"op\":{\"data\":\"aGVsbG8=\",\"ch\":2,\"unit\":\"ch\"}}", &unit, &a, &b, &path));
    assert(unit == U_CH && a == 2 && b == 0 && path); DeleteFileA(path); free(path);
    reset(); *(int *)(channels + 0x10) = 1; apply_plug_full(valid);
    assert(!g_compatFault && g_qt == 4);
    assert(g_q[0].type == T_LOAD && g_q[0].a == 0);
    assert(g_q[1].type == T_ADDCH && g_q[1].a == 1);
    assert(g_q[2].type == T_LOAD && g_q[2].unit == U_FX && g_q[2].b == 1);
    assert(g_q[3].type == T_FXSYNC && ((unsigned char *)g_q[3].order)[1]);
    reset(); no_files(); *(int *)(channels + 0x10) = 2;
    apply_plug_full("{\"op\":{\"items\":[],\"nch\":0}}");
    assert(!g_compatFault && g_qt == 3 && g_q[0].type == T_DELCH && g_q[0].a == 1 && g_q[1].a == 0 && g_q[2].type == T_FXSYNC);
    reset(); fail_calloc = 1; apply_plug_full(valid); stopped(); assert(g_qh == g_qt); no_files();
    reset(); for (i = 0; i < QCAP - 2; i++) enqueue_apply((uint32_t)i, (uint32_t)i);
    apply_plug_full(valid); stopped(); assert(g_qt == QCAP - 2); no_files();
    reset(); assert(RemoveDirectoryA(g_tmpDir)); apply_plug_full(valid);
    stopped(); assert(g_qh == g_qt); assert(CreateDirectoryA(g_tmpDir, NULL));
    puts("PASS complete plugin snapshots validate and stage before deletions; malformed, duplicate, missing and unavailable data preserve queued state");
}
static void ordering(void) {
    const int barriers[] = {T_DELCH, T_ORDER, T_FXCLEAR, T_PARAM, T_ADDCH};
    int i; QItem load = {0}, barrier = {0};
    for (i = 0; i < sizeof(barriers) / sizeof(barriers[0]); i++) {
        reset(); load.type = T_LOAD; load.unit = U_CH; load.a = 0; load.path = queue_path("before"); enqueue(&load);
        memset(&barrier, 0, sizeof(barrier)); barrier.type = barriers[i];
        if (barrier.type == T_ORDER) barrier.order = (int *)calloc(2, sizeof(int));
        if (barrier.type == T_ADDCH) barrier.path = queue_path("new");
        enqueue(&barrier); load.path = queue_path("after"); enqueue(&load);
        assert(g_qt == 3 && strstr(g_q[0].path, "\\before.fst") && strstr(g_q[2].path, "\\after.fst"));
    }
    reset(); load.path = queue_path("before"); enqueue(&load); load.path = queue_path("after"); enqueue(&load);
    assert(g_qt == 1 && strstr(g_q[0].path, "\\after.fst"));
    reset(); g_qh = g_qt = QCAP - 1;
    load.path = queue_path("before"); enqueue(&load); load.path = queue_path("after"); enqueue(&load);
    assert(g_qh == QCAP - 1 && g_qt == 0 && strstr(g_q[QCAP - 1].path, "\\after.fst"));
    reset();
    load.type = T_ADDCH; load.path = b64_to_tmpfile("aGVsbG8=", 8); assert(load.path);
    g_addChannel = fake_add; g_loadPreset = fake_load; g_hasPlugin = fake_has_plugin; g_resolve = fake_resolve;
    memset(&barrier, 0, sizeof(barrier)); barrier.type = T_DELCH; enqueue(&barrier);
    run_task_inner(&load);
    assert(added == 1 && loaded == 1 && g_qt == 1 && g_q[0].type == T_DELCH); no_files();
    reset(); load.path = b64_to_tmpfile("aGVsbG8=", 8); load.a = 1;
    run_task_inner(&load); stopped(); assert(!added && !loaded); no_files();
    puts("PASS preset coalescing preserves structural/parameter order; channel presets load before later index changes and temp files are cleaned");
}
static void metadata(void) {
    const char *bad_routes[] = {"[[1,12800],[2]]", "[[1,12800],[1,2]]", "[[1,12800],null]", "[[1,12800],[502,2]]", "[[1,12800],[2,2147483648]]", "[[1,12800],[2,\"1\"]]"};
    char tail[64], message[512]; unsigned char tail_bytes[MARK_SIZE - MARK_TAIL] = {4, 4};
    QItem item = {0}; int k;
    b64enc(tail_bytes, sizeof(tail_bytes), tail);
    metadata_fixture();
    sprintf_s(message, sizeof(message), "{\"op\":{\"mk\":[[1,8,\"%s\",\"QQA=\"],[2,8,\"bad\",\"\"]]}}", tail);
    assert(!marks_apply(M_PMARK, 1, message)); stopped();
    assert(!marker_clears && !marker_adds && marker_memory[0] == 1 && *(int *)&marker_memory[1] == 88);
    metadata_fixture(); fail_calloc = 1;
    assert(!marks_apply(M_PMARK, 1, "{\"op\":{\"mk\":[]}}")); stopped(); assert(!marker_clears && marker_memory[0] == 1);
    metadata_fixture();
    sprintf_s(message, sizeof(message), "{\"op\":{\"mk\": [ [1, 8, \"%s\", \"QQA=\"] ]}}", tail);
    assert(marks_apply(M_PMARK, 1, message));
    assert(!g_compatFault && marker_clears == 1 && marker_adds == 1 && marker_memory[0] == 1 && *(int *)&marker_memory[1] == 1);
    assert(!wcscmp(*(wchar_t **)((char *)&marker_memory[1] + 8), L"A"));
    assert(marks_apply(M_PMARK, 1, "{\"op\":{\"mk\":[]}}") && !marker_memory[0]);
    for (k = 0; k < sizeof(bad_routes) / sizeof(bad_routes[0]); k++) {
        metadata_fixture(); mixer_records[MIX_ROUTE + 8 + 4] = 1; *(int *)(mixer_records + MIX_ROUTE + 8) = 100;
        sprintf_s(message, sizeof(message), "{\"op\":{\"e\":\"rt\",\"i\":0,\"r\":%s}}", bad_routes[k]);
        item.path = message; do_meta(&item); stopped();
        assert(!route_changes && mixer_records[MIX_ROUTE + 8 + 4] == 1 && *(int *)(mixer_records + MIX_ROUTE + 8) == 100);
    }
    metadata_fixture(); item.path = "{\"op\":{\"e\":\"rt\",\"i\":0,\"r\": [ [1, 12800], [2, -1] ]}}";
    do_meta(&item); assert(!g_compatFault && route_changes == 2 && *(int *)(mixer_records + MIX_ROUTE + 8) == 12800);
    item.path = "{\"op\":{\"e\":\"rt\",\"i\":0,\"r\":[]}}";
    do_meta(&item); assert(!g_compatFault && route_changes == 4 && !mixer_records[MIX_ROUTE + 8 + 4] && !mixer_records[MIX_ROUTE + 16 + 4]);
    puts("PASS marker/routing preflight preserves existing records on late malformed entries; spaced lists and intentional clears apply");
}
static uint64_t fake_pattern(void *project, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e) {
    assert(project == project_object && g_mbAuto && g_patRemote && g_patDepth == 1);
    pattern_calls++;
    if (pattern_fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, NULL);
    if (pattern_insert) assert(a == 3 && !b && !c && !d && !e);
    else {
        const int *pats = (const int *)(uintptr_t)a;
        assert(b == 1 && pats[0] == 2 && pats[1] == 3 && (int64_t)c == -1 && d == 9 && e == 65535);
    }
    return 0;
}
static void pattern_operations(void) {
    const char *bad[] = {
        "{\"s\":216,\"p\":[1,\"bad\"]}", "{\"s\":216,\"p\":[1,1]}",
        "{\"s\":216,\"p\":[1,1000]}", "{\"s\":216,\"p\":[0]}",
        "{\"s\":216,\"p\":[]}", "{\"s\":216,\"p\":[-1]}",
        "{\"s\":216,\"p\":[1],\"a\":\"bad\"}", "{\"s\":216,\"p\":[1],\"b\":2147483648}",
        "{\"s\":216,\"p\":[1],\"c\":65536}", "{\"s\":248,\"p\":[1,2]}",
        "{\"s\":999,\"p\":[1]}", "{\"s\":216,\"p\":[1],\"p\":[2]}"
    };
    QItem item = {0}; int k;
    for (k = 0; k < PATOP_N; k++) g_patOrig[k] = fake_pattern;
    for (k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        metadata_fixture(); pattern_calls = 0; item.path = (char *)bad[k];
        g_patHash[1] = 1234; do_patop(&item); stopped();
        assert(!pattern_calls && !g_mbAuto && !g_patRemote && !g_patDepth && g_patHash[1] == 1234);
    }
    metadata_fixture(); pattern_calls = pattern_fault = pattern_insert = 0;
    item.path = "{\"op\":{\"c\":65535,\"p\": [2, 3],\"b\":9,\"s\":216,\"a\":-1}}";
    do_patop(&item); assert(!g_compatFault && pattern_calls == 1 && !g_mbAuto && !g_patRemote && !g_patDepth);
    metadata_fixture(); pattern_insert = 1;
    item.path = "{\"op\":{\"p\":[3],\"s\":248}}";
    do_patop(&item); assert(!g_compatFault && pattern_calls == 2);
    metadata_fixture(); pattern_fault = 1; g_patHash[1] = 1234;
    do_patop(&item); stopped();
    assert(pattern_calls == 3 && g_patHash[1] == 1234 && !g_mbAuto && !g_patRemote && !g_patDepth);
    puts("PASS malformed pattern operations never invoke FL; supported reordered fields apply; native faults retain baselines and close dialog guards");
}
int main(void) {
    char temp[MAX_PATH];
    InitializeCriticalSection(&g_qLock); InitializeCriticalSection(&g_sendLock);
    InitializeCriticalSection(&g_netTextLock); InitializeCriticalSection(&g_ulockLock);
    InitializeCriticalSection(&g_slotLock); InitializeCriticalSection(&g_sampleLock);
    InitializeCriticalSection(&song_cs);
    GetTempPathA(MAX_PATH, temp);
    sprintf_s(g_tmpDir, sizeof(g_tmpDir), "%sflim-inbound-%lu-%llu", temp, GetCurrentProcessId(), GetTickCount64());
    assert(CreateDirectoryA(g_tmpDir, NULL));
    clips(); plugins(); ordering(); metadata(); pattern_operations(); reset(); no_files(); assert(RemoveDirectoryA(g_tmpDir));
    puts("Inbound validation and edit ordering: OK");
    return 0;
}
