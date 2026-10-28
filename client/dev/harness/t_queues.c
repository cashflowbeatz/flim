#include <windows.h>
#include <winhttp.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int fail_malloc, fail_calloc, fail_realloc, fail_dup;
static unsigned malloc_calls, fail_malloc_at;
static void *test_malloc(size_t n) { malloc_calls++; return fail_malloc || (fail_malloc_at && malloc_calls == fail_malloc_at) ? NULL : malloc(n); }
static void *test_calloc(size_t n, size_t size) { return fail_calloc ? NULL : calloc(n, size); }
static void *test_realloc(void *p, size_t n) { return fail_realloc ? NULL : realloc(p, n); }
static char *test_strdup(const char *s) { return fail_dup ? NULL : _strdup(s); }
static wchar_t *test_wcsdup(const wchar_t *s) { return fail_dup ? NULL : _wcsdup(s); }
static DWORD WINAPI test_shutdown(HINTERNET ws, USHORT status, PVOID reason, DWORD n) {
    (void)ws; (void)status; (void)reason; (void)n; return 0;
}
#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#define _strdup test_strdup
#define _wcsdup test_wcsdup
#define WinHttpWebSocketShutdown test_shutdown
#include "../../engine/flim_engine.c"
#undef malloc
#undef calloc
#undef realloc
#undef _strdup
#undef _wcsdup
#undef WinHttpWebSocketShutdown

static const char edit[] = "{\"t\":\"op\",\"op\":{\"kind\":\"cur\"}}";
static void fixture(void) {
    while (g_qh != g_qt) { discard_task(&g_q[g_qh]); g_qh = (g_qh + 1) % QCAP; }
    fail_malloc = fail_calloc = fail_realloc = fail_dup = 0;
    malloc_calls = fail_malloc_at = 0;
    samples_disconnected(); net_reset();
    free(g_held); g_held = NULL; g_capheld = 0;
    g_compatFault = 0; g_compatReady = 1; g_netGen = 1;
    g_workBound = 0; g_outBusy = g_inBusy = 0; g_ws = NULL;
    g_alert[0] = 0; g_qh = g_qt = 0;
}
static void stopped(void) { assert(g_compatFault && !g_compatReady && strstr(g_alert, "stopped sharing")); }
static void sample_message(char *out, size_t cap, const char *path, const char *size) {
    sprintf_s(out, cap, "{\"t\":\"op\",\"op\":{\"kind\":\"project.load\",\"samples\":[{\"p\":\"%s\",\"h\":\"%064d\",\"s\":%s}]}}", path, 0, size);
}
static int resolve_snapshot(int index, char full, void **out) {
    (void)index; (void)full; *out = (void *)1; return 1;
}
static int save_snapshot(const wchar_t *path, int mask, void *object, int flags) {
    unsigned char data[400] = {1}; FILE *f = NULL;
    (void)mask; (void)object; (void)flags;
    assert(!_wfopen_s(&f, path, L"wb") && f);
    assert(fwrite(data, 1, sizeof(data), f) == sizeof(data)); assert(!fclose(f));
    return 0;
}
int main(void) {
    int i;
    char message[512];
    InitializeCriticalSection(&g_sendLock); InitializeCriticalSection(&g_netTextLock);
    InitializeCriticalSection(&g_qLock); InitializeCriticalSection(&g_lifecycleLock);
    InitializeCriticalSection(&g_heldLock); InitializeCriticalSection(&g_slotLock);
    InitializeCriticalSection(&g_ulockLock); InitializeCriticalSection(&g_sampleLock);
    InitializeCriticalSection(&g_outLock); InitializeCriticalSection(&g_inLock);
    g_outEvt = CreateEventW(NULL, FALSE, FALSE, NULL); g_inEvt = CreateEventW(NULL, FALSE, FALSE, NULL);

    fixture(); g_outBusy = 1;
    ws_send(edit); ws_send("{\"t\":\"bye\"}");
    assert(g_outCount == 2 && g_outHead->next == g_outTail && !strcmp(g_outHead->msg, edit));
    assert(g_outBytes == strlen(edit) + strlen(g_outTail->msg) + 2);
    g_workBound = 1; g_workGen = 0; ws_send(edit); g_workBound = 0;
    assert(g_outCount == 2);
    fixture(); assert(!g_outBytes && !g_outCount && !g_outHead && !g_outTail);
    g_outBusy = 1;
    for(i = 0; i < TRANSFER_QUEUE_MAX_COUNT; i++) ws_send(edit);
    assert(!g_compatFault && g_outCount == TRANSFER_QUEUE_MAX_COUNT);
    ws_send(edit); stopped(); assert(g_outCount == TRANSFER_QUEUE_MAX_COUNT);
    fixture(); g_outBusy = 1; g_outBytes = TRANSFER_QUEUE_MAX_BYTES;
    ws_send(edit); stopped(); assert(!g_outHead);
    fixture(); g_outBusy = 1; fail_calloc = 1; ws_send(edit); stopped(); assert(!g_outHead);
    fixture(); g_outBusy = 1; fail_dup = 1; ws_send(edit); stopped(); assert(!g_outHead);
    fixture(); g_ws = (HINTERNET)1; fail_malloc = 1;
    ws_send_samples(_strdup(edit), (const unsigned char *)"blob", 4); stopped(); assert(!g_outHead);
    fixture(); g_ws = (HINTERNET)1;
    ws_send_samples(_strdup(edit), NULL, 4); stopped(); assert(!g_outHead);
    puts("PASS outbound order, generation, count/byte bounds, allocation failures, ownership and cleanup");

    fixture(); g_inBusy = 1;
    inbox_msg(edit, 0); assert(!g_inHead);
    inbox_msg(edit, 1); inbox_msg("{\"t\":\"bye\"}", 1);
    assert(g_inCount == 2 && g_inHead->next == g_inTail && !strcmp(g_inHead->msg, edit));
    assert(g_inBytes == strlen(edit) + strlen(g_inTail->msg) + 2);
    fixture(); g_inBusy = 1;
    for(i = 0; i < TRANSFER_QUEUE_MAX_COUNT; i++) inbox_msg(edit, 1);
    inbox_msg(edit, 1); stopped(); assert(g_inCount == TRANSFER_QUEUE_MAX_COUNT);
    fixture(); g_inBusy = 1; g_inBytes = TRANSFER_QUEUE_MAX_BYTES;
    inbox_msg(edit, 1); stopped(); assert(!g_inHead);
    fixture(); g_inBusy = 1; fail_calloc = 1; inbox_msg(edit, 1); stopped(); assert(!g_inHead);
    fixture(); g_inBusy = 1; fail_dup = 1; inbox_msg(edit, 1); stopped(); assert(!g_inHead);
    puts("PASS inbound order, stale generation, count/byte bounds and allocation failures");

    fixture(); g_projBusy = 1;
    for(i = 0; i < HELD_MAX_COUNT; i++) assert(hold_if_loading(edit));
    assert(g_heldBytes == HELD_MAX_COUNT * (strlen(edit) + 1));
    assert(hold_if_loading(edit)); stopped(); assert(g_nheld == HELD_MAX_COUNT);
    fixture(); g_projBusy = 1; fail_realloc = 1;
    assert(hold_if_loading(edit)); stopped(); assert(!g_held && !g_nheld);
    fixture(); g_projBusy = 1;
    for(i = 0; i < 64; i++) hold_if_loading(edit);
    { HeldMsg *before = g_held; fail_realloc = 1; hold_if_loading(edit); stopped(); assert(g_held == before && g_nheld == 64); }
    fixture(); g_projBusy = 1; fail_dup = 1; hold_if_loading(edit); stopped(); assert(!g_nheld);
    fixture(); g_projBusy = 1; g_heldBytes = HELD_MAX_BYTES;
    hold_if_loading(edit); stopped(); assert(!g_nheld);
    fixture(); assert(!g_heldBytes && !g_nheld);
    puts("PASS project-load backlog bounds, preserved allocation on realloc failure and reset");

    fixture();
    for(i = 0; i < QCAP - 1; i++) enqueue_apply((uint32_t)i, (uint32_t)i);
    assert(!g_compatFault); enqueue_apply(0, 999); stopped();
    assert(g_qt == QCAP - 1); drain_queue_mainthread(); assert(g_qh == g_qt);
    puts("PASS main-thread overflow stops sharing instead of silently losing edits");

    for(i = 0; i < 3; i++) {
        PathList list = {0}; fixture();
        if(i == 0) fail_realloc = 1; else if(i == 1) fail_dup = 1; else list.n = SAMPLE_REFERENCE_MAX;
        if(i == 2) { list.n = 0; for(int k = 0; k < SAMPLE_REFERENCE_MAX; k++) { wchar_t path[32]; swprintf_s(path, 32, L"sample-%d.wav", k); pathlist_add(&list, path); } }
        pathlist_add(&list, L"extra.wav"); assert(list.failed); pathlist_free(&list);
    }
    fixture(); {size_t n = 99; fail_malloc = 1; assert(!b64dec("QQ==", 4, &n) && n == 0); stopped();}
    for(i = 0; i < 6; i++) {
        fixture();
        sample_message(message, sizeof(message), i == 0 ? "%%%%" : i == 1 ? "QQ==" : i == 2 ? "AAAA" : "QwA6AFwAYQAuAHcAYQB2AA==", i == 3 ? "-1" : "10");
        if(i == 4) fail_calloc = 1; if(i == 5) fail_realloc = 1;
        samples_resolve(message); stopped();
    }
    fixture(); samples_resolve("{\"op\":{\"samples\":[]}}"); assert(!g_compatFault);
    fixture(); map_set(L"original.wav", L"first.wav");
    assert(g_npmap == 1 && !wcscmp(map_find(L"original.wav"), L"first.wav"));
    fail_dup = 1; map_set(L"original.wav", L"replacement.wav"); stopped();
    assert(g_npmap == 1 && !wcscmp(map_find(L"original.wav"), L"first.wav"));
    fixture(); map_set(L"original.wav", NULL); assert(!g_npmap);
    free(g_pmap); g_pmap = NULL; g_cappmap = 0;
    fail_realloc = 1; map_set(L"original.wav", L"first.wav"); stopped(); assert(!g_pmap && !g_npmap);
    fixture();
    { FILETIME mt = {0};
      fail_realloc = 1; hash_put(L"first.wav", 4, mt, "hash"); assert(!g_hashes && !g_nhash && !g_compatFault);
      fail_realloc = 0; fail_dup = 1; hash_put(L"first.wav", 4, mt, "hash"); assert(!g_nhash);
      fail_dup = 0;
      for(i = 0; i < HASH_CACHE_MAX + 1; i++) { wchar_t file[32]; swprintf_s(file, 32, L"sample-%d.wav", i); hash_put(file, 4, mt, "hash"); }
      assert(g_nhash == HASH_CACHE_MAX && !wcscmp(g_hashes[0].path, L"sample-8192.wav"));
      for(i = 0; i < g_nhash; i++) free(g_hashes[i].path);
      free(g_hashes); g_hashes = NULL; g_nhash = g_caphash = 0;
    }
    puts("PASS sample-path replacement is atomic on failure and hash caching remains optional and bounded");
    fixture(); free(g_declined); g_declined = NULL; g_ndeclined = g_capdeclined = 0;
    fail_realloc = 1; decline("first"); stopped(); assert(!g_declined && !g_ndeclined);
    fixture(); decline("first"); decline("first"); assert(g_ndeclined == 1 && declined("first"));
    for (i = 1; i < TRANSFER_QUEUE_MAX_COUNT; i++) { char value[65]; sprintf_s(value, sizeof(value), "%064d", i); decline(value); }
    assert(!g_compatFault && g_ndeclined == TRANSFER_QUEUE_MAX_COUNT);
    decline("extra"); stopped(); assert(g_ndeclined == TRANSFER_QUEUE_MAX_COUNT && declined("first"));
    free(g_declined); g_declined = NULL; g_ndeclined = g_capdeclined = 0;
    puts("PASS declined sample allocation, deduplication and bounded retention");
    fixture(); map_set(L"peer.wav", L"local.wav"); decline("sample"); strcpy_s(g_blobKey, sizeof(g_blobKey), "private");
    samples_disconnected();
    assert(!g_npmap && !g_pmap && !g_declined && !g_ndeclined && !g_blobKey[0]);
    g_workBound = 1; g_workGen = g_netGen - 1;
    map_set(L"stale.wav", L"old.wav"); decline("old");
    assert(!g_pmap && !g_declined); g_workBound = 0;
    puts("PASS disconnect clears room sample paths and choices; old workers cannot restore them");

    fixture(); fail_dup = 1; on_meta_set("{}"); stopped(); assert(g_qh == g_qt);
    fixture(); fail_dup = 1; on_pat_op("{}"); stopped(); assert(g_qh == g_qt);
    fixture(); fail_calloc = 1; apply_plug_full("{}"); stopped(); assert(g_qh == g_qt);
    fixture(); { QItem item = {0}; item.type = T_PROJLOAD; enqueue(&item); stopped(); assert(g_qh == g_qt); }
    fixture(); { QItem item = {0}; item.type = T_UNITREVERT; enqueue(&item); stopped(); assert(g_qh == g_qt); }
    puts("PASS incomplete metadata, pattern, project, plugin and revert tasks never enter the FL queue");

    { const char *invalid[] = {
        "{\"op\":{}}", "{\"op\":{\"items\":[{}]}}",
        "{\"op\":{\"items\":[{\"pat\":1,\"data\":\"\"},{\"pat\":1,\"data\":\"\"}]}}",
        "{\"op\":{\"items\":[{\"pat\":1,\"data\":\"QQ==\"}]}}",
        "{\"op\":{\"items\":[{\"pat\":1,\"data\":\"\"},{\"pat\":2}]}}",
        "{\"op\":{\"items\":[{\"pat\":1000,\"data\":\"\"}]}}",
        "{\"op\":{\"items\":[{\"pat\":1,\"data\":\"%%%%\"}]}}"
      };
      for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
          fixture(); on_notes(invalid[i], 1); stopped(); assert(g_qh == g_qt);
      }
    }
    fixture(); fail_calloc = 1; on_notes("{\"op\":{\"items\":[]}}", 1); stopped(); assert(g_qh == g_qt);
    fixture(); fail_malloc = 1; on_notes("{\"op\":{\"pat\":1,\"data\":\"\"}}", 0); stopped(); assert(g_qh == g_qt);
    fixture(); on_notes("{\"op\":{\"data\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\",\"pat\":1}}", 0);
    assert(!g_compatFault && g_qt == 1 && g_q[0].type == T_NOTES && g_q[0].a == 1 && g_q[0].mode == 1);
    fixture(); on_notes("{\"op\":{\"items\":[{\"pat\":1,\"data\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"},{\"data\":\"\",\"pat\":2}]}}", 1);
    assert(!g_compatFault && g_qt == 3 && g_q[2].type == T_NOTESYNC);
    assert(((unsigned char *)g_q[2].order)[1] && ((unsigned char *)g_q[2].order)[2] && !((unsigned char *)g_q[2].order)[3]);
    fixture(); on_notes("{\"op\":{\"items\":[]}}", 1);
    assert(!g_compatFault && g_qt == 1 && g_q[0].type == T_NOTESYNC);
    puts("PASS malformed full-note snapshots queue no changes; reordered fields, valid notes and intentional empty snapshots work");

    { const char *invalid[] = {
        "{\"op\":{\"n\":2,\"from\":[0]}}", "{\"op\":{\"n\":2,\"from\":[0,0]}}",
        "{\"op\":{\"n\":2,\"from\":[0,2]}}", "{\"op\":{\"n\":2,\"from\":[0,-1]}}",
        "{\"op\":{\"n\":2,\"from\":[0,1,2]}}", "{\"op\":{\"n\":2,\"from\":[0,\"1\"]}}"
      };
      for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
          fixture(); on_chan_order(invalid[i]); stopped(); assert(g_qh == g_qt);
      }
    }
    fixture(); fail_malloc = 1; on_chan_order("{\"op\":{\"n\":2,\"from\":[1,0]}}"); stopped(); assert(g_qh == g_qt);
    fixture(); on_chan_order("{\"op\":{\"from\": [1, 0], \"n\":2}}");
    assert(!g_compatFault && g_qt == 1 && g_q[0].type == T_ORDER && g_q[0].order[0] == 2 && g_q[0].order[1] == 1 && !g_q[0].order[2]);
    puts("PASS only complete channel permutations are queued, with failed allocation handled without dereferencing NULL");

    fixture(); fail_malloc = 1; assert(!notes_json_item(1, NULL, 0)); stopped();
    fixture(); fail_malloc = 1; assert(!meta_json(M_PAT, 1)); stopped();
    fixture(); fail_malloc = 1; assert(!unit_json_fields(U_CH, 0, 0, (const unsigned char *)"x", 1)); stopped();
    fixture(); assert(!unit_json_fields(U_CH, 0, 0, (const unsigned char *)"x", (size_t)-1)); stopped();
    fixture(); assert(!unit_json_fields(U_CH, 0, 0, NULL, 1)); stopped();
    fixture(); fail_malloc_at = 2; send_unit("plug.state", U_CH, 0, 0, (const unsigned char *)"x", 1, NULL); stopped(); assert(!g_outHead);
    fixture(); fail_malloc = 1; do_full(); stopped(); assert(!g_outHead);
    fixture(); { char *fields = unit_json_fields(U_CH, 7, 0, (const unsigned char *)"hello", 5);
      assert(fields && strstr(fields, "\"ch\":7") && strstr(fields, "aGVsbG8=")); free(fields); }
    fixture(); fail_malloc = 1; send_notes_full(); stopped(); assert(!g_outHead);
    fixture(); fail_malloc = 1; send_project(NULL, 0, NULL); stopped(); assert(!g_outHead);
    fixture(); fail_malloc = 1; assert(!b64_to_tmpfile("aGVsbG8=", 8)); stopped();
    fixture(); fail_malloc = 1; assert(!tmp_path("test")); stopped();
    fixture();
    { unsigned char data[] = {1, 2, 3, 4};
      g_plugLastCh[0].d = data; g_plugLastCh[0].n = sizeof(data); g_plugLastCh[0].known = 1;
      fail_malloc = 1; plug_revert_state(U_CH, 0, 0); stopped(); assert(g_qh == g_qt);
      memset(&g_plugLastCh[0], 0, sizeof(g_plugLastCh[0]));
    }
    fixture();
    { static uint64_t image[4], patterns[MAX_PAT * 8], array[4];
      unsigned char records[NOTE_SIZE * 8] = {0}; CRITICAL_SECTION note_cs;
      InitializeCriticalSection(&note_cs);
      memset(&g_fl, 0, sizeof(g_fl)); g_flBase = (char *)image;
      g_fl.PATARR = IMG_BASE + 8; PAT_STRIDE = 64; g_fl.NOTELOCK = IMG_BASE + 16;
      image[1] = (uint64_t)(uintptr_t)patterns; image[2] = (uint64_t)(uintptr_t)&note_cs;
      *(void **)((char *)patterns + 64 + PAT_NOTES) = array;
      *(unsigned *)((char *)array + NOTES_COUNT) = 8;
      *(void **)((char *)array + NOTES_DATA) = records;
      fail_realloc = 1; send_notes_full(); stopped(); assert(!g_outHead);
      DeleteCriticalSection(&note_cs); g_flBase = NULL;
    }
    fixture();
    { char temp[MAX_PATH], *file; size_t length; unsigned char *bytes;
      GetTempPathA(MAX_PATH, temp);
      sprintf_s(g_tmpDir, sizeof(g_tmpDir), "%sflim-queues-%lu-%llu", temp, GetCurrentProcessId(), GetTickCount64());
      assert(CreateDirectoryA(g_tmpDir, NULL));
      {
          unsigned char image[64] = {0}, channels[32] = {0};
          void *list = channels, *items[1] = {(void *)1}; char saved[MAX_PATH];
          memset(&g_fl, 0, sizeof(g_fl)); g_flBase = (char *)image; g_fl.CHANLIST = IMG_BASE + 8;
          *(void **)(image + 8) = &list; *(void **)(channels + 8) = items; *(int *)(channels + 16) = 1;
          g_resolve = resolve_snapshot; g_savePreset = save_snapshot; fail_realloc = 1;
          do_full(); stopped(); assert(!g_outHead);
          sprintf_s(saved, sizeof(saved), "%s\\snap.fst", g_tmpDir); assert(DeleteFileA(saved));
          g_flBase = NULL; g_resolve = NULL; g_savePreset = NULL; fixture();
      }
      file = b64_to_tmpfile("aGVsbG8=", 8); assert(file && !g_compatFault);
      bytes = read_file(file, &length); assert(bytes && length == 5 && !memcmp(bytes, "hello", 5));
      free(bytes); DeleteFileA(file); free(file); assert(RemoveDirectoryA(g_tmpDir));
      assert(!b64_to_tmpfile("aGVsbG8=", 8)); stopped();
    }
    puts("PASS outgoing note/project allocation failures send no partial snapshots; plugin decoding and failed disk writes fail safely");
    fixture(); CloseHandle(g_outEvt); CloseHandle(g_inEvt);
    puts("PASS sample-list bounds, failed decoding/allocation and malformed path metadata");
    return 0;
}
