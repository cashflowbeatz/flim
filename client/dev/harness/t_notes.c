#include <assert.h>
#include <windows.h>
#include <winhttp.h>
static DWORD WINAPI notes_send(HINTERNET ws, WINHTTP_WEB_SOCKET_BUFFER_TYPE type, PVOID data, DWORD bytes);
#define WinHttpWebSocketSend notes_send
#include "../../engine/flim_engine.c"
#undef WinHttpWebSocketSend

static uint64_t image[4], patterns[MAX_PAT * 8], array[4];
static unsigned char record[NOTE_SIZE * 6002];
static CRITICAL_SECTION note_cs;
static void *note_vtable[2];
static unsigned added, deleted, sent;
static int ignore_add;
static char last_send[2 * 1024 * 1024];

static DWORD WINAPI notes_send(HINTERNET ws, WINHTTP_WEB_SOCKET_BUFFER_TYPE type, PVOID data, DWORD bytes) {
    (void)ws; (void)type; assert(bytes < sizeof(last_send));
    memcpy(last_send, data, bytes); last_send[bytes] = 0; sent++; return 0;
}
static void *test_note_array(int pattern, char create) { (void)create; assert(pattern == 1); return array; }
static void test_note_delete(void *arr, int index, char flag) {
    unsigned *count = (unsigned *)((char *)arr + NOTES_COUNT); (void)flag;
    assert(index >= 0 && (unsigned)index < *count);
    memmove(record + (size_t)index * NOTE_SIZE, record + ((size_t)index + 1) * NOTE_SIZE, (size_t)(*count - index - 1) * NOTE_SIZE);
    (*count)--; deleted++;
}
static void test_note_add(void *arr, const void *note) {
    unsigned *count = (unsigned *)((char *)arr + NOTES_COUNT); assert(*count < 6002);
    if (ignore_add) return;
    memcpy(record + (size_t)(*count)++ * NOTE_SIZE, note, NOTE_SIZE); added++;
}

static void fixture(void) {
    unsigned i;
    for (i = 0; i < MAX_PAT; i++) fnc_free(&g_noteSets[i]);
    g_noteSetCount = 0; g_notesCrdt = 0; g_ws = NULL;
    added = deleted = sent = 0; last_send[0] = 0;
    memset(image, 0, sizeof(image));
    memset(patterns, 0, sizeof(patterns));
    memset(array, 0, sizeof(array));
    memset(record, 0, sizeof(record));
    memset(g_patHash, 0, sizeof(g_patHash));
    memset(g_patSeen, 0, sizeof(g_patSeen));
    memset(g_patChangedAt, 0, sizeof(g_patChangedAt));
    memset(g_patDirtyAt, 0, sizeof(g_patDirtyAt));
    memset((void *)g_patSuppress, 0, sizeof(g_patSuppress));
    memset(&g_fl, 0, sizeof(g_fl));
    g_flBase = (char *)image;
    g_fl.PATARR = IMG_BASE + 8; PAT_STRIDE = 64;
    g_fl.NOTELOCK = IMG_BASE + 16;
    *(void **)((char *)image + 8) = patterns;
    *(void **)((char *)image + 16) = &note_cs;
    *(void **)((char *)patterns + 64 + PAT_NOTES) = array;
    *(unsigned *)((char *)array + NOTES_COUNT) = 1;
    *(void **)((char *)array + NOTES_DATA) = record;
    note_vtable[1] = test_note_add; array[0] = (uint64_t)note_vtable;
    g_noteArr = test_note_array; g_noteDel = test_note_delete;
    g_notesReady = 1; g_qh = g_qt = 0;
    g_compatFault = 0;
    g_patSeen[1] = g_patHash[1] = notes_hash(1);
}

static void refused_native_add(void) {
    unsigned char incoming[NOTE_SIZE * 2] = {0}; uint64_t before;
    fixture(); incoming[NOTE_SIZE] = 64; before = g_patHash[1]; ignore_add = 1;
    notes_apply(1, incoming, 2); ignore_add = 0;
    assert(g_compatFault && g_patHash[1] == before && !g_patLast[1].known);
    puts("PASS a native add that silently refuses a note never advances the shared baseline");
}

static void incremental_apply(void) {
    unsigned i; unsigned char *incoming = (unsigned char *)calloc(6001, NOTE_SIZE);
    assert(incoming); fixture();
    for (i = 0; i < 6000; i++) {
        memcpy(record + (size_t)i * NOTE_SIZE, &i, sizeof(i));
        record[(size_t)i * NOTE_SIZE + NOTE_SEL_OFS] = NOTE_SEL_BIT;
        memcpy(incoming + (size_t)(5999 - i) * NOTE_SIZE, &i, sizeof(i));
    }
    *(unsigned *)((char *)array + NOTES_COUNT) = 6000;
    i = 6000; memcpy(incoming + 6000 * NOTE_SIZE, &i, sizeof(i));
    notes_apply(1, incoming, 6001);
    assert(!g_compatFault && added == 1 && deleted == 0);
    for (i = 0; i < 6000; i++) assert(record[(size_t)i * NOTE_SIZE + NOTE_SEL_OFS] & NOTE_SEL_BIT);
    added = deleted = 0; notes_apply(1, incoming, 6001); assert(!added && !deleted);
    incoming[4] = 96; notes_apply(1, incoming, 6001); assert(added == 1 && deleted == 1);
    free(incoming);
    fixture(); *(unsigned *)((char *)array + NOTES_COUNT) = 2;
    record[NOTE_SIZE + NOTE_SEL_OFS] = NOTE_SEL_BIT;
    { unsigned char one[NOTE_SIZE] = {0}; notes_apply(1, one, 1); }
    assert(deleted == 1 && !added && (record[NOTE_SEL_OFS] & NOTE_SEL_BIT));
}

static void crdt_pending_recording(void) {
    const unsigned char scope[16] = {1};
    FncState remote = {0}; FncEntry *delta; unsigned changes, bytes, n;
    unsigned char notes[NOTE_SIZE * 2] = {0}, *wire; QItem task = {0};
    FlJson root, op, data; const FncEntry *received; unsigned char decoded_scope[16], *decoded; size_t decoded_bytes;
    fixture(); g_notesCrdt = 1; g_compatReady = 1; g_compatMixed = 1;
    g_fl.build = L"25.2.5.5319"; strcpy_s(g_hostFlBuild, sizeof(g_hostFlBuild), "24.2.2.4597"); g_ws = (HINTERNET)1;
    record[0] = notes[0] = 60;
    assert(fnc_seed(&g_noteSets[1], record, 1, scope)); g_noteSetCount = 1;
    assert(fnc_seed(&remote, record, 1, scope));
    notes[NOTE_SIZE] = 67;
    assert(fnc_capture(&remote, notes, 2, notes_crdt_id, &delta, &changes, FNC_LIMIT));
    wire = fnc_pack(&remote, delta, changes, &bytes); assert(wire);
    /* Local MIDI recording has not reached its batching deadline when the remote delta arrives. */
    record[NOTE_SIZE] = 65; record[NOTE_SEL_OFS] = NOTE_SEL_BIT;
    *(unsigned *)((char *)array + NOTES_COUNT) = 2;
    task.a = 1; task.b = 1; task.mode = bytes; task.order = (int *)wire;
    do_notes(&task);
    assert(!g_compatFault && sent == 1 && added == 1 && deleted == 0);
    assert(*(unsigned *)((char *)array + NOTES_COUNT) == 3);
    assert(record[0] == 60 && record[24] == 65 && record[48] == 67 && (record[NOTE_SEL_OFS] & NOTE_SEL_BIT));
    root.p = last_send; root.n = strlen(last_send);
    assert(fj_get(root, "op", &op) && !fj_get(op, "key", &data));
    { int pat; assert(notes_item_data(op, &pat, &data) && pat == 1); }
    decoded = b64dec(data.p, data.n, &decoded_bytes);
    assert(decoded && fnc_unpack(decoded, (unsigned)decoded_bytes, decoded_scope, &received, &n) && n == 1);
    assert(received[0].note[0] == 65 && !received[0].dead);
    assert(fnc_merge(&remote, decoded_scope, received, n));
    assert(remote.count == g_noteSets[1].count && !memcmp(remote.entries,g_noteSets[1].entries,remote.count * FNC_ENTRY));
    do_notes(&task); assert(!g_compatFault && sent == 1 && added == 1 && deleted == 0);
    free(decoded); free(wire); free(delta); fnc_free(&remote); g_ws = NULL;
}

static void crdt_wire_gates(void) {
    char message[1024]; unsigned bytes; unsigned char scope[16] = {7}, *wire; FncState state = {0};
    FlJson item, data; int pat;
    fixture(); assert(fnc_seed(&state, NULL, 0, scope)); wire = fnc_pack(&state, NULL, 0, &bytes); assert(wire);
    strcpy_s(message, sizeof(message), "{\"pat\":1,\"crdt\":1,\"data\":\"");
    b64enc(wire, bytes, message + strlen(message)); strcat_s(message, sizeof(message), "\"}");
    item.p = message; item.n = strlen(message); assert(!notes_item_data(item, &pat, &data));
    g_notesCrdt = 1; assert(notes_item_data(item, &pat, &data));
    item.p = "{\"pat\":1,\"data\":\"\"}"; item.n = strlen(item.p); assert(!notes_item_data(item, &pat, &data));
    g_notesCrdt = 0; assert(notes_item_data(item, &pat, &data));
    { const char *legacy = "{\"t\":\"welcome\",\"compatibility\":{\"protocol\":1,\"host_fl\":\"25.2.5.5319\",\"wire\":\"flim-25-26-v1\",\"mixed\":true}}";
      const char *modern = "{\"t\":\"welcome\",\"notes_crdt\":1,\"compatibility\":{\"protocol\":1,\"host_fl\":\"25.2.5.5319\",\"wire\":\"flim-25-26-v1\",\"mixed\":true}}";
      g_compatReady = 0; g_fl.build = L"26.1.7.5654"; assert(compat_accept(legacy) && !g_notesCrdt);
      assert(!compat_accept(modern) && g_compatFault); g_compatReady = g_compatFault = 0;
      assert(compat_accept(modern) && g_notesCrdt); assert(!compat_accept(legacy) && g_compatFault);
    }
    free(wire); fnc_free(&state);
}

static void crdt_snapshot_and_budget(void) {
    FncState remote = {0}; unsigned bytes;
    unsigned char local_scope[16] = {1}, remote_scope[16] = {2}, note[NOTE_SIZE] = {67}, *wire;
    QItem task = {0};
    fixture(); g_notesCrdt = 1; record[0] = 60;
    assert(fnc_seed(&g_noteSets[1], record, 1, local_scope)); g_noteSetCount = 1;
    assert(fnc_seed(&remote, note, 1, remote_scope)); wire = fnc_pack(&remote,remote.entries,remote.count,&bytes); assert(wire);
    task.a = 1; task.b = 1; task.order = (int *)wire; task.mode = bytes;
    do_notes(&task); assert(g_compatFault && !added && !deleted && record[0] == 60);
    g_compatFault = 0; task.b = 2; /* Host full snapshot may establish a new project baseline. */
    do_notes(&task); assert(!g_compatFault && added == 1 && deleted == 1 && record[0] == 67);
    assert(!memcmp(g_noteSets[1].scope, remote_scope, 16)); free(wire); fnc_free(&remote);
    g_noteSetCount = 262144;
    assert(notes_crdt_capture(1, note, 1, 0)); /* A no-op must still work at the aggregate history cap. */
    note[0] = 69; assert(!notes_crdt_capture(1, note, 1, 0));
    assert(g_compatFault && g_noteSets[1].count == 1 && g_noteSets[1].entries[0].note[0] == 67 && record[0] == 67);
}

static void crdt_structural_refresh(void) {
    unsigned char scope[16], previous[16];
    fixture(); g_notesCrdt = 1; record[0] = 60;
    notes_crdt_rebaseline(); assert(!g_compatFault && g_noteSets[1].count == 1);
    memcpy(scope, g_noteSets[1].scope, 16);
    record[NOTE_SIZE] = 65; *(unsigned *)((char *)array + NOTES_COUNT) = 2;
    assert(notes_crdt_capture(1, record, 2, 0));
    memmove(record, record + NOTE_SIZE, NOTE_SIZE); *(unsigned *)((char *)array + NOTES_COUNT) = 1;
    assert(notes_crdt_capture(1, record, 1, 0)); assert(g_noteSets[1].count == 2);
    notes_crdt_rebaseline(); assert(!g_compatFault && g_noteSets[1].count == 2 && !memcmp(scope,g_noteSets[1].scope,16));
    record[4] = 10; notes_crdt_rebaseline();
    assert(!g_compatFault && memcmp(scope,g_noteSets[1].scope,16)); memcpy(previous,g_noteSets[1].scope,16);
    record[4] = 0; notes_crdt_rebaseline();
    assert(!g_compatFault && memcmp(scope,g_noteSets[1].scope,16) && memcmp(previous,g_noteSets[1].scope,16));
    assert(!fnc_merge(&g_noteSets[1],scope,NULL,0));
}

static void crdt_empty_pattern_snapshot(void) {
    unsigned char scope[16]; int tasks = 0;
    fixture(); g_notesCrdt = 1; g_compatReady = 1; g_compatMixed = 0; g_ws = (HINTERNET)1;
    *(unsigned *)((char *)array + NOTES_COUNT) = 0;
    notes_crdt_rebaseline(); assert(!g_compatFault); memcpy(scope,g_noteSets[1].scope,16);
    send_notes_full(); assert(sent == 1 && strstr(last_send,"\"notes.full\""));
    g_noteSets[1].scope[0] ^= 1; /* A member's previous project had a different empty-pattern scope. */
    on_notes(last_send,1); assert(!g_compatFault && (g_qt-g_qh+QCAP)%QCAP == MAX_PAT);
    while(g_qh != g_qt) {
        QItem task=g_q[g_qh]; g_qh=(g_qh+1)%QCAP;
        if(task.type == T_NOTES) {
            assert(task.b == 2 && task.mode == FNC_HEADER); tasks++;
            do_notes(&task); assert(!g_compatFault && !memcmp(scope,g_noteSets[task.a].scope,16));
        }
        discard_task(&task);
    }
    assert(tasks == MAX_PAT-1); g_ws = NULL;
}

static void continuous_recording(void) {
    DWORD t;
    fixture();
    for (t = 1000; t <= 1200; t += 100) {
        record[0]++;
        scan_notes(t);
        assert(g_qt == (t == 1200 ? 1 : 0));
    }
    assert(g_q[0].type == T_NOTESNAP && g_q[0].a == 1);
    for (t = 1300; t <= 2500; t += 100) { record[0]++; scan_notes(t); }
    assert(g_qt == 1); /* A slow main thread has only one pending snapshot per pattern. */
}

static void suppressed_local_edit_retries(void) {
    fixture();
    record[0]++;
    g_patSuppress[1] = 4500;
    scan_notes(4000); scan_notes(4300);
    assert(g_qt == 0 && g_patChangedAt[1] == 4000);
    scan_notes(4500);
    assert(g_qt == 1 && g_q[0].type == T_NOTESNAP);
}

static void full_queue_retries(void) {
    fixture();
    memset(g_q, 0, sizeof(g_q));
    g_qh = 0; g_qt = QCAP - 1;
    record[0]++;
    scan_notes(5000); scan_notes(5300);
    assert(g_qt == QCAP - 1);
    g_qh = g_qt = 0;
    scan_notes(5400);
    assert(g_qt == 1 && g_q[0].type == T_NOTESNAP);
}

static void uptime_wrap(void) {
    volatile DWORD until = 0;
    assert(!notes_suppressed(&until, 0x80001000u));
    until = 15;
    assert(notes_suppressed(&until, 0xfffffff0u));
    assert(!notes_suppressed(&until, 15) && !until);
    assert(!notes_suppressed(&until, 0x80001000u));
    fixture(); record[0]++;
    scan_notes(0x80001000u); scan_notes(0x80001200u);
    assert(g_qt == 1);
}

static void failed_snapshot_is_not_empty(void) {
    unsigned n;
    unsigned char *copy;
    QItem q = {0};
    uint64_t before;
    fixture();
    before = g_patHash[1];
    *(void **)((char *)array + NOTES_DATA) = NULL;
    copy = notes_copy(1, &n);
    assert(!copy && n == UINT_MAX);
    q.a = 1;
    do_notesnap(&q);
    assert(g_patHash[1] == before);
    *(unsigned *)((char *)array + NOTES_COUNT) = 0;
    copy = notes_copy(1, &n);
    assert(!copy && n == 0);
}

static void relay_rejection_stops_sharing(void) {
    const char *codes[] = {"server_error", "rate_limit", "storage_limit", "locked", "lock_limit", "bad_request", "future_error"};
    char message[160];
    unsigned i;
    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        g_compatReady = 1; g_compatFault = 0;
        sprintf_s(message, sizeof(message), "{\"t\":\"error\",\"code\":\"%s\",\"msg\":\"Literal %%s %%n reason\"}", codes[i]);
        net_server_error(message);
        assert(!g_compatReady && g_compatFault);
        assert(strstr(g_alert, "Literal %s %n reason"));
        assert(!strstr(g_netErr, "password"));
    }
    g_compatFault = 0; g_compatReady = 1;
    blob_limit_error(429, "{\"code\":\"r2_monthly_limit\"}");
    assert(g_compatFault && strstr(g_alert, "monthly R2"));
    g_compatFault = 0; g_compatReady = 1;
    blob_limit_error(507, "{}");
    assert(g_compatFault && strstr(g_alert, "storage allowance"));
}

static void *missing_note_array(int pattern, char create) { (void)pattern; (void)create; return NULL; }
static void *faulting_note_array(int pattern, char create) {
    (void)pattern; (void)create;
    RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, NULL);
    return NULL;
}

static void failed_apply_stops_sharing(void) {
    uint64_t before;
    fixture();
    before = g_patHash[1];
    g_noteArr = missing_note_array;
    g_compatFault = 0; g_compatReady = 1;
    notes_apply(1, NULL, 0);
    assert(g_compatFault && g_patHash[1] == before && !g_patLast[1].known);
    g_noteArr = faulting_note_array;
    g_compatFault = 0; g_compatReady = 1;
    notes_apply(1, NULL, 0);
    assert(g_compatFault && g_patHash[1] == before && !g_patLast[1].known);
}

static void queued_edits_rejected_after_stop(void) {
    QItem note = {0}, revert = {0};
    uint64_t before;
    fixture();
    before = g_patHash[1];
    note.type = T_NOTES; note.a = 1; note.order = (int *)malloc(NOTE_SIZE);
    enqueue(&note);
    enqueue_apply(0, 123);
    revert.type = T_UNITREVERT; revert.path = _strdup("pat:1");
    enqueue(&revert);
    assert(g_qt == 3);
    g_compatFault = 1;
    drain_queue_mainthread();
    assert(g_qh == g_qt && g_patHash[1] == before);
    enqueue_apply(0, 456);
    assert(g_qh == g_qt);
    note.order = (int *)malloc(NOTE_SIZE);
    run_task(&note);
    assert(g_patHash[1] == before);
}

int main(void) {
    InitializeCriticalSection(&g_qLock);
    InitializeCriticalSection(&note_cs);
    InitializeCriticalSection(&g_sendLock);
    InitializeCriticalSection(&g_netTextLock);
    InitializeCriticalSection(&g_lifecycleLock);
    InitializeCriticalSection(&g_ulockLock);
    InitializeCriticalSection(&g_outLock);
    continuous_recording();
    suppressed_local_edit_retries();
    full_queue_retries();
    uptime_wrap();
    failed_snapshot_is_not_empty();

    relay_rejection_stops_sharing();
    failed_apply_stops_sharing();
    queued_edits_rejected_after_stop();
    refused_native_add();
    incremental_apply();
    crdt_pending_recording();
    crdt_wire_gates();
    crdt_snapshot_and_budget();
    crdt_structural_refresh();
    crdt_empty_pattern_snapshot();
    fixture();
    DeleteCriticalSection(&note_cs);
    DeleteCriticalSection(&g_qLock);
    DeleteCriticalSection(&g_sendLock);
    DeleteCriticalSection(&g_netTextLock);
    DeleteCriticalSection(&g_lifecycleLock);
    DeleteCriticalSection(&g_ulockLock);
    DeleteCriticalSection(&g_outLock);
    puts("Notes: OK (200 ms recording batch, queue retry, failed snapshot/apply, incremental 6000-note selection, CRDT merge during pending MIDI recording, duplicate delivery, negotiation/fallback)");
    return 0;
}
