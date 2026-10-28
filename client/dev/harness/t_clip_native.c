#include <assert.h>
#include <stdlib.h>
static int allocations_left = -1;
static void *test_malloc(size_t n) {
    if (!allocations_left) return NULL;
    if (allocations_left > 0) allocations_left--;
    return malloc(n);
}
#define malloc test_malloc
#include "../../engine/flim_engine.c"
#undef malloc

static uint64_t image[8], arrangements[2];
static unsigned char arrangement[64], clips[32], records[3 * CLIP_SIZE], mainform[0x1000];
static void *clip_vmt[4], *playlist, *main_form;
static CRITICAL_SECTION note_cs;
static unsigned purges, adds;
static int ignore_add;
static uint32_t last_rec_id;
static unsigned rec_calls, mute_calls;
static uint32_t rec_override;
static int rec_has_override;
static int mixer_count = 512;

static uint32_t fake_rec(uint32_t id, uint64_t value, uint32_t flags) {
    (void)value; (void)flags; rec_calls++; last_rec_id = id;
    return rec_has_override ? rec_override : id;
}
static void fake_mute(int track, int enabled) { (void)track; (void)enabled; mute_calls++; }
static void fake_purge(void *ca) {
    purges++; *(unsigned *)((char *)ca + CLIPS_COUNT) = 0;
}
static int fake_add(void *ca, const void *record) {
    unsigned n = *(unsigned *)((char *)ca + CLIPS_COUNT);
    assert(n < 3);
    if (ignore_add) return -1;
    memcpy(records + n * clip_native_size(), record, clip_native_size());
    *(unsigned *)(records + n * clip_native_size() + CLIP_ID_OFS) = 100 + n;
    *(unsigned *)((char *)ca + CLIPS_COUNT) = n + 1;
    adds++; return (int)n;
}
static void fixture(const wchar_t *build) {
    memset(&g_fl, 0, sizeof(g_fl)); g_fl.build = build;
    memset(image, 0, sizeof(image)); memset(arrangement, 0, sizeof(arrangement));
    memset(clips, 0, sizeof(clips)); memset(records, 0, sizeof(records));
    memset(mainform, 0, sizeof(mainform));
    free(g_clipLast.d); memset(&g_clipLast, 0, sizeof(g_clipLast));
    free(g_clipPend.d); memset(&g_clipPend, 0, sizeof(g_clipPend));
    g_flBase = (char *)image;
    g_fl.NOTELOCK = IMG_BASE + 8; *(void **)((char *)image + 8) = &note_cs;
    g_fl.ARRS = IMG_BASE + 16; *(void **)((char *)image + 16) = &arrangements[1];
    arrangements[0] = 1; arrangements[1] = (uint64_t)(uintptr_t)arrangement;
    *(void **)(arrangement + ARR_CLIPS) = clips;
    *(void **)clips = clip_vmt; *(void **)(clips + CLIPS_DATA) = records;
    clip_vmt[CLIPADD_VSLOT] = fake_add; clip_vmt[CLIPPURGE_VSLOT] = fake_purge;
    g_fl.ARRCUR = IMG_BASE + 24;
    g_fl.PLFORM = IMG_BASE + 32; playlist = NULL; *(void **)((char *)image + 32) = &playlist;
    g_fl.MAINFORM = IMG_BASE + 40; main_form = mainform; *(void **)((char *)image + 40) = &main_form;
    g_fl.PATARR = IMG_BASE + 48; /* no patterns in this synthetic image */
    g_fl.MIXCNT = IMG_BASE + 56; *(void **)((char *)image + 56) = &mixer_count;
    g_rec = fake_rec; g_clipArr = 0; g_clipHash = 0; g_clipSeen = 0;
    g_nulocks = 0; g_qh = g_qt = 0; g_compatFault = 0;
    purges = adds = 0; allocations_left = -1;
    rec_calls = mute_calls = 0; g_setEnabled = fake_mute;
    rec_has_override = 0;
}
static void native_record(unsigned i) {
    unsigned char *r = records + i * clip_native_size();
    unsigned char canonical[CLIP_SIZE] = {0};
    canonical[0] = (unsigned char)(10 + i);
    memcpy(canonical + 60, flim_clip_extension_default, 20);
    assert(flim_clip_to_native(r, canonical, clip_native_size()));
}
static void translation(void) {
    fixture(L"24.2.2.4597");
    assert(mix_count() == 126 && clip_native_size() == 60);
    assert(read_value(0x70401fc0u) == 0x20401fc0u && last_rec_id == 0x20401fc0u);
    assert(write_value(MIX_ID(125, 0x1fc0), 1) == MIX_ID(125, 0x1fc0) - 0x50000000u);
    rec_calls = 0;
    assert(read_value(MIX_ID(126, 0x1fc0)) == REC_NONE);
    assert(write_value(MIX_ID(127, 0x1fc0), 1) == REC_NONE);
    assert(read_value(FX_ID(300, 0, 0)) == REC_NONE);
    assert(read_value(FX_ID(125, 10, 0)) == REC_NONE);
    assert(write_value(FX_ID(0, 63, 0), 1) == REC_NONE);
    assert(!route_inbound(FX_ID(0, 10, 0), 1, 0));
    assert(write_value(MUTE_ID(126), 1) == REC_NONE);
    assert(read_value(0x20401fc0u) == REC_NONE);
    assert(!route_inbound(MIX_ID(126, 0x1fc0), 1, 0));
    assert(!rec_calls && !mute_calls && !g_npend);
    assert(read_value(FX_ID(125, 9, 0)) == FX_ID(125, 9, 0) - 0x50000000u);
    rec_has_override = 1; rec_override = 0x7fffffffu;
    assert(read_value(0) == REC_NONE && write_value(0, 1) == REC_NONE);
    g_nslot = 0; memset(g_hash, 0, sizeof(g_hash));
    assert(!add_slot(0) && !g_nslot);
    rec_override = 10000; assert(read_value(0) == 10000);
    assert(!fx_obj(126, 0) && !fx_obj(-1, 0) && !fx_obj(0, MAX_FX_SLOTS));
    assert(local_rec_id(0x6fffffffu) == 0x6fffffffu);
    assert(local_rec_id(0x90000000u) == 0x90000000u);
    assert(local_rec_id(REC_NONE) == REC_NONE && local_rec_id(0xf0000000u) == 0xf0000000u);
    assert(!fl_ready()); *(void **)(mainform + 0xf38) = records; assert(fl_ready());
    fixture(L"25.2.5.5319");
    assert(read_value(0x70401fc0u) == 0x70401fc0u && clip_native_size() == 80);
    assert(read_value(MIX_ID(127, 0x1fc0)) == MIX_ID(127, 0x1fc0));
    assert(read_value(FX_ID(300, 0, 0)) == FX_ID(300, 0, 0));
    rec_calls = 0;
    assert(read_value(FX_ID(300, 10, 0)) == REC_NONE);
    assert(write_value(FX_ID(0, 63, 0), 1) == REC_NONE && !rec_calls);
    rec_has_override = 1; rec_override = 0x7fffffffu;
    assert(read_value(0) == 0x7fffffffu && write_value(0, 1) == 0x7fffffffu);
    rec_override = REC_NONE; assert(read_value(0) == REC_NONE);
    assert(!fl_ready()); *(void **)(mainform + 0xf30) = records; assert(fl_ready());
    fixture(L"24.2.2.4598"); assert(!flim_profile_fl24(&g_fl));
}
static void snapshots_and_apply(const wchar_t *build) {
    unsigned n; uint64_t hash;
    unsigned char *wire, expected[2 * CLIP_SIZE];
    fixture(build); native_record(0); native_record(1);
    *(unsigned *)(clips + CLIPS_COUNT) = 2;
    records[CLIP_SEL_OFS] |= CLIP_SEL_BIT;
    wire = clips_copy(0, &n); assert(wire && n == 2);
    assert(wire[CLIP_SIZE] == 11);
    memcpy(expected, wire, sizeof(expected));
    hash = clips_hash(0);
    records[CLIP_SEL_OFS] ^= CLIP_SEL_BIT;
    *(unsigned *)(records + CLIP_ID_OFS) = 456;
    assert(clips_hash(0) == hash);
    records[CLIP_SEL_OFS] |= CLIP_SEL_BIT;
    assert(clips_apply(0, wire, 2));
    assert(purges == 1 && adds == 2 && g_clipLast.known && g_clipLast.n == 2);
    assert(records[CLIP_SEL_OFS] & CLIP_SEL_BIT);
    assert(records[clip_native_size()] == 11);
    free(wire);
    fixture(L"24.2.2.4597"); native_record(0); *(unsigned *)(clips + CLIPS_COUNT) = 1;
    expected[60] = 1;
    assert(!clips_apply(0, expected, 2));
    assert(!purges && !adds && records[0] == 10 && !g_clipLast.known);
}
static void failed_reads_and_allocations(void) {
    unsigned n;
    unsigned char *wire;
    QItem item = {0}; UState state = {0}; unsigned char byte = 7;
    fixture(L"24.2.2.4597"); native_record(0); *(unsigned *)(clips + CLIPS_COUNT) = 1;
    allocations_left = 2; do_clipsnap(&item); allocations_left = -1;
    assert(!g_nulocks && !g_clipPend.known && !g_clipLast.known);
    assert(ustate_set(&state, &byte, 1, 1));
    allocations_left = 0; assert(!ustate_set(&state, records, 2, 1)); allocations_left = -1;
    assert(state.known && state.n == 1 && state.d[0] == 7); free(state.d);
    *(void **)(clips + CLIPS_DATA) = NULL;
    wire = clips_copy(0, &n); assert(!wire && n == UINT_MAX);
    units_capture_all(); assert(!g_clipLast.known);
    *(unsigned *)(clips + CLIPS_COUNT) = 200001;
    units_capture_all(); assert(!g_clipLast.known);
    *(unsigned *)(clips + CLIPS_COUNT) = 0;
    wire = clips_copy(0, &n); assert(!wire && n == 0);
    on_clips_set("{\"arr\":0,\"data\":\"AA==\"}"); assert(g_qt == 0);
}
static void refused_native_clip(void) {
    unsigned char expected[CLIP_SIZE] = {0};
    fixture(L"24.2.2.4597"); memcpy(expected + 60, flim_clip_extension_default, 20);
    ignore_add = 1; assert(!clips_apply(0, expected, 1)); ignore_add = 0;
    assert(g_compatFault && !g_clipLast.known);
    puts("PASS silently refused native clips do not commit an incorrect shared playlist baseline");
}
int main(void) {
    InitializeCriticalSection(&note_cs); InitializeCriticalSection(&g_ulockLock);
    InitializeCriticalSection(&g_sendLock); InitializeCriticalSection(&g_netTextLock);
    InitializeCriticalSection(&g_qLock);
    InitializeCriticalSection(&g_slotLock);
    refused_native_clip();
    translation(); snapshots_and_apply(L"24.2.2.4597"); snapshots_and_apply(L"25.2.5.5319");
    failed_reads_and_allocations();
    puts("Native clip integration: OK (60/80 strides, selection, refusal, mixer translation, readiness and failed snapshots)");
    return 0;
}
