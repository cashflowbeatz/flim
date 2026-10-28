#include <assert.h>
#include <windows.h>
#include "../../engine/flim_engine.c"

static uint64_t image[4];
static int tracks = MAX_MIX, writes;
static uint32_t fake_rec(uint32_t id, uint64_t value, uint32_t flags) {
    (void)id; if (flags != REC_GetValue) writes++;
    return flags == REC_GetValue ? 17 : (uint32_t)value;
}
static void reset(void) {
    int i;
    while (g_qh != g_qt) { discard_task(&g_q[g_qh]); g_qh = (g_qh + 1) % QCAP; }
    for (i = 0; i < g_nheld; i++) free(g_held[i].msg);
    g_nheld = 0; g_heldBytes = 0;
    g_qh = g_qt = 0; g_compatFault = 0; g_compatReady = 1; g_netGen = 1; g_workBound = 0;
    g_compatMixed = 0; g_isLeader = 0; g_notesCrdt = 0; g_portableSync = 1; g_portableLocal = 0;
    g_projBusy = 0; g_ws = NULL; g_npend = 0; g_nslot = 0; g_nfocus = g_ncur = 0; g_peersJson[0] = 0;
    g_nulocks = 0; writes = 0; tracks = MAX_MIX;
    memset(g_hash, 0, sizeof(g_hash)); memset(g_slots, 0, sizeof(g_slots));
    memset(g_chU, 0, sizeof(g_chU)); memset(g_fxU, 0, sizeof(g_fxU));
    memset(&g_fl, 0, sizeof(g_fl)); memset(image, 0, sizeof(image));
    g_fl.build = L"26.1.7.5654"; g_flBase = (char *)image; g_fl.MIXCNT = IMG_BASE + 8;
    image[1] = (uint64_t)(uintptr_t)&tracks; g_rec = fake_rec;
    strcpy_s(g_myId, sizeof(g_myId), "local");
}
static void matrix(void) {
    const char *versions[] = {"24.2.2.4597", "25.2.5.5319", "26.1.7.5654"};
    const wchar_t *wide[] = {L"24.2.2.4597", L"25.2.5.5319", L"26.1.7.5654"};
    char message[2048]; int host, member;
    for (host = 0; host < 3; host++) for (member = 0; member < 3; member++) {
        reset(); g_fl.build = wide[member]; g_compatReady = 0;
        sprintf_s(message, sizeof(message), "{ \"t\" : \"presence\", \"portable_sync\" : 1, \"compatibility\" : { \"protocol\" : 1, \"wire\" : \"" FLIM_WIRE "\", \"host_fl\" : \"%s\", \"mixed\" : %s } }", versions[host], host == member ? "false" : "true");
        assert(compat_accept(message));
        assert(g_portableLocal == (host > member));
        sprintf_s(message, sizeof(message), "{ \"t\" : \"op\", \"source_fl\" : \"%s\", \"op\" : { \"kind\" : \"param.set\", \"id\" : 0, \"v\" : 4294967295 } }", versions[host]);
        handle_msg(message); assert(!g_compatFault && g_qt == 1 && g_q[0].v == UINT32_MAX);
        sprintf_s(message, sizeof(message), "{ \"t\" : \"op\", \"source_fl\" : \"%s\", \"op\" : { \"kind\" : \"notes.set\", \"pat\" : 1, \"data\" : \"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" } }", versions[host]);
        handle_msg(message); assert(!g_compatFault && g_qt == 2 && g_q[1].type == T_NOTES);
        sprintf_s(message, sizeof(message), "{ \"t\" : \"op\", \"source_fl\" : \"%s\", \"noise\" : {\"kind\":\"project.load\"}, \"op\" : { \"kind\" : \"chan.del\", \"ch\" : 3 } }", versions[host]);
        handle_msg(message); assert(!g_compatFault && g_qt == 3 && g_q[2].type == T_DELCH && g_q[2].a == 3);
        sprintf_s(message, sizeof(message), "{ \"t\" : \"op\", \"source_fl\" : \"%s\", \"op\" : { \"kind\" : \"fx.clear\", \"trk\" : 0, \"slot\" : 9 } }", versions[host]);
        handle_msg(message); assert(!g_compatFault && g_qt == 4 && g_q[3].type == T_FXCLEAR);
    }
    puts("PASS nine host/member FL24/25/26 decisions with spaced controls, notes, channel and effect edits; nested kind cannot reroute edits");
}
static void controls(void) {
    const char *invalid[] = {"[[0,1],[1]]", "[[0,1],[1,2,3]]", "[[0,1],[1,\"2\"]]", "[[0,1],[1,-1]]", "[[0,1],[1,4294967296]]", "[[0,1],[0,2]]", "[[0,1],null]", "[[0,1],[4294967296,2]]", "[[0,1],[1,2.1]]"};
    char message[512]; unsigned i;
    reset(); apply_full_state("{\"op\":{\"vals\" : [ [1, 4294967295], [0, 5] ]}}");
    assert(!g_compatFault && g_qt == 2 && g_q[0].id == 0 && g_q[1].v == UINT32_MAX);
    for (i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        reset(); sprintf_s(message, sizeof(message), "{\"op\":{\"vals\":%s}}", invalid[i]);
        apply_full_state(message); assert(g_compatFault && !g_qt && !g_nslot && !writes);
    }
    reset(); for (i = 0; i < QCAP - 2; i++) enqueue_apply(i, i);
    apply_full_state("{\"op\":{\"vals\":[[0,1],[1,2]]}}"); assert(g_compatFault && g_qt == QCAP - 2 && !g_nslot);
    reset(); g_fl.build = L"24.2.2.4597";
    sprintf_s(message, sizeof(message), "{\"op\":{\"vals\":[[0,1],[%u,2]]}}", MIX_ID(300, 0x1fc0));
    apply_full_state(message); assert(!g_compatFault && g_qt == 1 && g_q[0].id == 0);
    reset(); g_npend = MAX_PEND; g_pend[0].id = 0; g_pend[0].v = 11;
    for (i = 1; i < MAX_PEND; i++) g_pend[i].id = i;
    pend_put(0, 22); assert(!g_compatFault && g_pend[0].v == 22);
    pend_put(MAX_PEND, 1); assert(g_compatFault && g_npend == MAX_PEND && g_pend[0].v == 22);
    puts("PASS complete typed control snapshots, duplicate detection, queue reservation, FL24 unsupported-track skip and explicit pending overflow");
}
static void queues(void) {
    QItem load = {0}, retry = {0}; unsigned i;
    reset(); load.type = T_LOAD; load.path = _strdup("before.fst"); strcpy_s(load.presetFl, 32, "24.2.2.4597"); enqueue(&load);
    load.path = _strdup("after.fst"); load.mode = 3; strcpy_s(load.presetFl, 32, "26.1.7.5654"); enqueue(&load);
    assert(g_qt == 1 && g_q[0].mode == 3 && !strcmp(g_q[0].presetFl, "26.1.7.5654"));
    reset(); for (i = 0; i < 10000; i++) enqueue_apply(0, i);
    assert(!g_compatFault && g_qt == 1 && g_q[0].v == 9999);
    enqueue_local_revert(0, 7, 2); enqueue_apply(0, 9); assert(g_qt == 3 && g_q[1].mode == 1 && !g_q[2].mode);
    enqueue_local_revert(0, 7, 2); enqueue_local_revert(0, 8, 4); assert(g_qt == 5);
    reset(); for (i = 0; i < QCAP - 1; i++) enqueue_apply(i, i);
    retry.type = T_PLUGRETRY; enqueue(&retry); assert(!g_compatFault && g_qt == QCAP - 1);
    reset(); for (i = 0; i < 100; i++) enqueue_apply(i, i);
    drain_queue_mainthread(); assert(writes > 0 && writes <= 64 && g_qh != g_qt);
    while (g_qh != g_qt) drain_queue_mainthread(); assert(writes == 100);
    reset(); g_projBusy = 1; assert(hold_if_loading("{ \"t\" : \"op\", \"op\" : {\"kind\":\"cur\"}}")); assert(g_nheld == 1);
    assert(message_has_samples("{\"t\" : \"op\",\"op\":{\"samples\" : []}}"));
    assert(!message_has_samples("{\"t\":\"op\",\"op\":{\"nested\":{\"samples\":[]}}}"));
    puts("PASS preset provenance, 10000 knob updates coalesced, revert barriers, optional retry pressure, bounded UI apply and spaced load/file routing");
}
static void presence(void) {
    uint32_t value = 123; int unit, a, b; char text[200];
    reset();
    assert(!jget_u32("{\"n\":\"1\"}", "n", &value) && value == 123);
    assert(!jget_u32("{\"n\":4294967296}", "n", &value));
    assert(!jget_u32("{\"nested\":{\"n\":1}}", "n", &value));
    assert(jget_paramkey("{ \"key\" : \"param:0\" }", &value) && value == 0);
    assert(!jget_paramkey("{\"key\":\"param:01\"}", &value) && !jget_paramkey("{\"key\":\"param:0tail\"}", &value));
    assert(plug_parse("plug:fx:511:9", &unit, &a, &b) && a == 511 && b == 9);
    assert(!plug_parse("plug:ch:1junk", &unit, &a, &b) && !plug_parse("plug:fx:01:0", &unit, &a, &b));
    assert(!unit_key_valid("pat:0") && !unit_key_valid("pat:1more") && unit_key_valid("pat:999"));
    assert(on_unit_lock("{ \"t\" : \"lock\", \"key\" : \"plug:ch:1\", \"granted\" : true, \"owner\" : \"peer\" }"));
    assert(g_nulocks == 1 && g_ulocks[0].peer);
    assert(on_unit_lock("{ \"t\" : \"lock_released\", \"key\" : \"plug:ch:1\" }")); assert(!g_ulocks[0].peer);
    on_focus("{\"from\":\"peer\",\"op\":{\"w\" : \"Piano \\\"roll\\\" \\u00e9 \\ud83c\\udfb9\"}}");
    assert(g_nfocus == 1 && strstr(g_focus[0].where, "\"roll\"") && strstr(g_focus[0].where, "\xc3\xa9"));
    { FlJson bad = {"\"\\ud800\"", 8}; assert(!fj_decode(bad, text, sizeof(text)) && !text[0]); }
    on_cursor("{\"from\":\"peer\",\"op\":{\"w\" : \"pr\",\"p\":1,\"x\":500,\"y\":60}}"); assert(g_ncur == 1 && g_cur[0].x == 500);
    on_cursor("{\"from\":\"peer\",\"op\":{\"w\":\"pr\",\"p\":1,\"x\":4294967296,\"y\":0}}"); assert(g_cur[0].x == 500);
    on_cursor("{\"from\":\"peer\",\"op\":{\"w\":\"pr-more\",\"p\":1,\"x\":3,\"y\":0}}"); assert(g_cur[0].x == 500);
    on_cursor("{\"from\":\"peer\",\"op\":{\"w\" : \"\"}}"); assert(!g_cur[0].w[0]);
    reset(); g_compatReady = 0;
    assert(parse_link("https://relay.example/room/00123456", &g_netUrl, g_session));
    handle_msg("{ \"t\" : \"welcome\", \"session\":\"00123456\", \"role\" : \"leader\", \"peers\":[{\"role\":\"member\"}], \"portable_sync\":1, \"compatibility\":{\"protocol\":1,\"host_fl\":\"26.1.7.5654\",\"wire\":\"" FLIM_WIRE "\",\"mixed\":false}}");
    assert(!g_compatFault && g_isLeader);
    reset(); g_compatReady = 0;
    handle_msg("{\"t\":\"welcome\",\"session\":\"00123456\",\"peers\":[{\"role\":\"leader\"}],\"portable_sync\":1,\"compatibility\":{\"protocol\":1,\"host_fl\":\"26.1.7.5654\",\"wire\":\"" FLIM_WIRE "\",\"mixed\":false}}");
    assert(g_compatFault && !g_isLeader);
    reset(); net_server_error("{ \"t\" : \"error\", \"code\" : \"compatibility\", \"msg\" : \"Update \\\"flim\\\" first\" }");
    assert(g_compatFault && strstr(g_alert, "\"flim\""));
    puts("PASS strict unsigned fields and canonical locks; spaced grant/release; escaped Unicode labels; invalid cursors preserve last valid location");
}
int main(void) {
    InitializeCriticalSection(&g_qLock); InitializeCriticalSection(&g_slotLock);
    InitializeCriticalSection(&g_sendLock); InitializeCriticalSection(&g_netTextLock);
    InitializeCriticalSection(&g_lifecycleLock); InitializeCriticalSection(&g_ulockLock);
    InitializeCriticalSection(&g_heldLock); InitializeCriticalSection(&g_sampleLock);
    InitializeCriticalSection(&g_outLock);
    matrix(); controls(); queues(); presence(); reset(); free(g_held);
    puts("FL editing compatibility sweep: OK (synthetic native records; no live FL instance used)"); return 0;
}
