/* Synthetic FL records: remote edits must preserve unsent local metadata. */
#include <assert.h>
#include "../../engine/flim_engine.c"

static uint64_t image[8], patterns[MAX_PAT * 8];
static int tracks, numerator = 4, denominator = 4;
static void *playlist_form;

static void pattern_name(int i, const char *name) {
    wchar_t **p = (wchar_t **)(pat_rec(i) + PAT_NAME);
    dstr_free(*p);
    *p = dstr_new(name);
    assert(*p);
}

static void fixture(void) {
    int i;
    g_compatFault = 0; g_compatReady = 1;
    for (i = 1; i < MAX_PAT; i++)
        dstr_free(*(wchar_t **)((char *)patterns + (size_t)i * 64 + PAT_NAME));
    memset(image, 0, sizeof(image));
    memset(patterns, 0, sizeof(patterns));
    memset(&g_fl, 0, sizeof(g_fl));
    memset(g_mHash, 0, sizeof(g_mHash));
    memset(g_mSeen, 0, sizeof(g_mSeen));
    memset(g_mAt, 0, sizeof(g_mAt));
    memset(g_mSentAt, 0, sizeof(g_mSentAt));
    g_flBase = (char *)image;
    g_fl.PATARR = IMG_BASE + 8; PAT_STRIDE = 64;
    g_fl.MIXCNT = IMG_BASE + 16; g_fl.ARRS = IMG_BASE + 24;
    g_fl.TSNUM = IMG_BASE + 32; g_fl.TSDEN = IMG_BASE + 40;
    g_fl.PLFORM = IMG_BASE + 48;
    *(void **)((char *)image + 8) = patterns;
    *(void **)((char *)image + 16) = &tracks;
    *(void **)((char *)image + 32) = &numerator;
    *(void **)((char *)image + 40) = &denominator;
    *(void **)((char *)image + 48) = &playlist_form;
    g_nChKnown = 0; g_isLeader = 0; g_metaReady = 1;
    g_qh = g_qt = 0;
    pattern_name(1, "Local pattern");
    pattern_name(2, "Remote pattern");
    meta_rebaseline(0);
    assert(g_mHash[M_PAT][1] > 2 && g_mHash[M_PAT][2] > 2);
}

static void channel_rebaseline_preserves_pending(void) {
    uint64_t before, renamed;
    fixture();
    before = g_mHash[M_PAT][1];
    pattern_name(1, "Unsent rename");
    renamed = meta_hash(M_PAT, 1);
    assert(renamed != before);
    meta_observe(1000);
    g_mHash[M_MIX][1] = 20; g_mSeen[M_MIX][1] = 21; g_mAt[M_MIX][1] = 124;
    g_mHash[M_CHAN][0] = 30; g_mSeen[M_CHAN][0] = 31; g_mAt[M_CHAN][0] = 125;
    meta_rebaseline(1);
    assert(g_mHash[M_PAT][1] == before);
    assert(g_mSeen[M_PAT][1] == renamed && g_mAt[M_PAT][1] == 1000);
    assert(g_mHash[M_MIX][1] == 20 && g_mSeen[M_MIX][1] == 21 && g_mAt[M_MIX][1] == 124);
    assert(!g_mHash[M_CHAN][0] && !g_mSeen[M_CHAN][0] && !g_mAt[M_CHAN][0]);
}

static void channel_rebaseline_preserves_unobserved(void) {
    uint64_t before, renamed;
    fixture();
    before = g_mHash[M_PAT][1];
    pattern_name(1, "Rename before scan");
    renamed = meta_hash(M_PAT, 1);
    assert(renamed != before);
    meta_rebaseline(1);
    assert(g_mHash[M_PAT][1] == before && g_mSeen[M_PAT][1] == before);
    assert(!g_mAt[M_PAT][1]);
    meta_observe(2000);
    assert(g_mHash[M_PAT][1] == before);
    assert(g_mSeen[M_PAT][1] == renamed && g_mAt[M_PAT][1] == 2000);
}

static void remote_meta_preserves_unobserved(void) {
    QItem remote = {0};
    uint64_t before, renamed, remote_before;
    DWORD settled;
    fixture();
    before = g_mHash[M_PAT][1];
    remote_before = g_mHash[M_PAT][2];
    pattern_name(1, "Rename before remote edit");
    renamed = meta_hash(M_PAT, 1);
    assert(renamed != before);
    /* Reproduce the gap between a local edit and the next scanner tick. */
    assert(g_mSeen[M_PAT][1] == before && !g_mAt[M_PAT][1]);
    remote.path = "{\"t\":\"op\",\"op\":{\"kind\":\"meta.set\",\"e\":\"pt\",\"i\":2,\"l\":123}}";
    do_meta(&remote);
    assert(*(int32_t *)(pat_rec(2) + PAT_LENGTH) == 123);
    assert(g_mHash[M_PAT][2] != remote_before);
    assert(g_mHash[M_PAT][2] == meta_hash(M_PAT, 2));
    assert(g_mSeen[M_PAT][2] == g_mHash[M_PAT][2] && !g_mAt[M_PAT][2]);
    assert(g_mHash[M_PAT][1] == before);
    assert(g_mSeen[M_PAT][1] == renamed && g_mAt[M_PAT][1]);
    settled = g_mAt[M_PAT][1] + META_SETTLE_MS + META_SCAN_MS;
    scan_meta(settled);
    assert(g_qh == 0 && g_qt == 1);
    assert(g_q[0].type == T_METASNAP && g_q[0].unit == M_PAT && g_q[0].a == 1);
    assert(g_mHash[M_PAT][1] == before && g_mSeen[M_PAT][1] == renamed);
}

static void typed_details(void) {
    const char *invalid[] = {
        "{\"e\":\"pt-more\",\"i\":2,\"l\":8}",
        "{\"e\":\"pt\",\"i\":4294967298,\"l\":8}",
        "{\"e\":\"pt\",\"i\":2,\"l\":4294967296}",
        "{\"e\":\"pt\",\"i\":2,\"l\":8,\"c\":-1}",
        "{\"e\":\"pt\",\"i\":2,\"l\":8,\"n\":\"%%%%\"}",
        "{\"e\":\"pt\",\"i\":2,\"l\":8,\"n\":\"QQ==\"}",
        "{\"e\":\"pt\",\"i\":2,\"l\":8,\"n\":\"AAAAAA==\"}",
        "{\"e\":\"pt\",\"i\":2,\"l\":8,\"n\":\"ANg=\"}",
        "{\"e\":\"ts\",\"i\":0,\"nu\":17,\"de\":4}"
    };
    QItem item = {0}; char message[512]; unsigned k; uint64_t before;
    for (k = 0; k < sizeof(invalid)/sizeof(invalid[0]); k++) {
        fixture(); g_compatFault = 0; g_compatReady = 1; before = g_mHash[M_PAT][2];
        sprintf_s(message, sizeof(message), "{\"op\":%s}", invalid[k]); item.path = message; do_meta(&item);
        assert(g_compatFault && !*(int32_t *)(pat_rec(2) + PAT_LENGTH) && g_mHash[M_PAT][2] == before);
    }
    fixture(); g_compatFault = 0;
    item.path = "{ \"op\" : { \"e\" : \"pt\", \"i\" : 2, \"l\" : 321 } }";
    do_meta(&item); assert(!g_compatFault && *(int32_t *)(pat_rec(2) + PAT_LENGTH) == 321);
    fixture(); g_compatFault = 0; before = g_mHash[M_PAT][2];
    item.path = "{\"op\":{\"e\":\"pt\",\"i\":2,\"n\":\"\"}}";
    do_meta(&item); /* No setter is installed in this fixture: SEH must preserve the baseline. */
    assert(g_compatFault && g_mHash[M_PAT][2] == before);
    puts("PASS exact metadata types, direct bounded targets, strict lengths/colors, complete UTF16 names, time signatures and spaced valid details");
}

int main(void) {
    int i;
    InitializeCriticalSection(&g_qLock);
    InitializeCriticalSection(&g_sendLock); InitializeCriticalSection(&g_netTextLock);
    typed_details();
    channel_rebaseline_preserves_pending();
    channel_rebaseline_preserves_unobserved();
    remote_meta_preserves_unobserved();
    for (i = 1; i < MAX_PAT; i++) dstr_free(*(wchar_t **)(pat_rec(i) + PAT_NAME));
    DeleteCriticalSection(&g_qLock);
    puts("Metadata: OK (channel rebaseline and remote edits preserve unsent local renames)");
    return 0;
}
