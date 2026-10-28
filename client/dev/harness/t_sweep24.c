#include <assert.h>
#include <stdlib.h>
#include <windows.h>
#include <bcrypt.h>
static int allocation_budget = -1, hash_failure, finish_failure;
static void *budget_malloc(size_t n) {
    if (!allocation_budget) return NULL;
    if (allocation_budget > 0) allocation_budget--;
    return malloc(n);
}
static NTSTATUS WINAPI checked_hash(BCRYPT_HASH_HANDLE hash, PUCHAR data, ULONG n, ULONG flags) {
    return hash_failure ? (NTSTATUS)0xc0000001 : BCryptHashData(hash, data, n, flags);
}
static NTSTATUS WINAPI checked_finish(BCRYPT_HASH_HANDLE hash, PUCHAR data, ULONG n, ULONG flags) {
    return finish_failure ? (NTSTATUS)0xc0000001 : BCryptFinishHash(hash, data, n, flags);
}
#define malloc budget_malloc
#define BCryptHashData checked_hash
#define BCryptFinishHash checked_finish
#include "../../engine/flim_engine.c"
#undef malloc
#undef BCryptHashData
#undef BCryptFinishHash

static void reset(void) {
    while (g_qh != g_qt) { discard_task(&g_q[g_qh]); g_qh = (g_qh + 1) % QCAP; }
    memset(g_q, 0, sizeof(g_q));
    allocation_budget = -1; hash_failure = finish_failure = 0;
    g_qh = g_qt = 0; g_compatFault = 0; g_compatReady = 1; g_netGen = 1; g_workBound = 0;
    g_notesCrdt = 0; g_ws = NULL; g_alert[0] = 0; g_sampleRetry = 0;
}
static void snapshots(void) {
    const char *message = "{\"op\":{\"items\":[{\"pat\":1,\"data\":\"\"},{\"pat\":2,\"data\":\"\"}]}}";
    reset(); on_notes(message, 1); assert(!g_compatFault && g_qt == 3);
    reset(); g_qt = QCAP - 3; on_notes(message, 1); assert(g_compatFault && g_qt == QCAP - 3);
    reset(); allocation_budget = 1; on_notes(message, 1); assert(g_compatFault && !g_qt); allocation_budget = -1;
    reset(); on_notes("{\"op\":{\"items\":[{\"pat\":1,\"data\":\"\"},{\"pat\":1,\"data\":\"\"}]}}", 1); assert(g_compatFault && !g_qt);
    reset();
    {
        FncState state = {0}; unsigned bytes, i; unsigned char scope[16] = {1};
        unsigned char *notes = (unsigned char *)calloc(FNC_LIMIT, FNC_NOTE), *wire;
        char *encoded, *message; size_t at = 0, encoded_n;
        assert(notes && fnc_seed(&state, notes, FNC_LIMIT, scope));
        wire = fnc_pack(&state, state.entries, state.count, &bytes); assert(wire);
        encoded_n = ((size_t)bytes + 2) / 3 * 4;
        encoded = (char *)malloc(encoded_n + 1); message = (char *)malloc((encoded_n + 64) * 17 + 64);
        assert(encoded && message); b64enc(wire, bytes, encoded);
        at += sprintf_s(message, 64, "{\"op\":{\"items\":[");
        for (i = 1; i <= 17; i++) at += sprintf_s(message + at, encoded_n + 64, "%s{\"pat\":%u,\"crdt\":1,\"data\":\"%s\"}", i == 1 ? "" : ",", i, encoded);
        strcpy_s(message + at, 8, "]}}"); g_notesCrdt = 1;
        on_notes(message, 1); assert(g_compatFault && !g_qt);
        free(notes); free(wire); free(encoded); free(message); fnc_free(&state);
    }
    puts("PASS note snapshots reserve all tasks, discard late allocation failures and reject aggregate CRDT overflow before queuing");
}
static void paths(void) {
    wchar_t invalid[] = L"C:\\Audio\\bad.wav"; wchar_t output[512];
    unsigned char raw[] = "C:/Audio/kick.wav"; PathList list = {0};
    assert(sample_source_valid(L"C:\\Audio\\kick.wav", 17));
    assert(sample_source_valid(L"%ILSharedData%kick.wav", 22));
    invalid[9] = 0xd800; assert(!sample_source_valid(invalid, wcslen(invalid)));
    assert(!sample_source_valid(L"C:\\Audio\\kick.wav:stream", 24));
    assert(!sample_source_valid(L"\\\\?\\C:\\kick.wav", 15));
    assert(!sample_source_valid(L"relative.wav", 12));
    raw_collect(raw, sizeof(raw), &list); assert(list.n == 1 && !wcscmp(list.paths[0], L"C:/Audio/kick.wav")); pathlist_free(&list);
    reset(); map_set(L"C:\\Audio\\Kick.wav", L"D:\\Cache\\first.wav"); map_set(L"c:\\audio\\kick.WAV", L"D:\\Cache\\second.wav");
    assert(g_npmap == 1 && !wcscmp(map_find(L"C:\\AUDIO\\KICK.WAV"), L"D:\\Cache\\second.wav"));
    map_set(L"C:\\AUDIO\\KICK.WAV", NULL); assert(!g_npmap);
    memset(g_tmpDir, 'x', sizeof(g_tmpDir) - 1); g_tmpDir[sizeof(g_tmpDir) - 1] = 0;
    assert(!tmp_path("long") && g_compatFault); g_tmpDir[0] = 0;
    (void)output;
    puts("PASS Windows case aliases, forward slash sample paths, invalid UTF-16/device/stream paths and long preset TEMP paths");
}
static void sample_metadata(void) {
    const wchar_t *source = L"C:\\Audio\\kick.wav";
    char encoded[256], message[1024], changed[256]; size_t n;
    b64enc((unsigned char *)source, (int)(wcslen(source) * 2), encoded); n = strlen(encoded);
    reset();
    sprintf_s(message, sizeof(message), "{\"op\":{\"samples\":[{\"p\":\"%s\",\"h\":\"%064d\",\"s\":4},{\"p\":\"%s\",\"h\":\"%064d\",\"s\":5}]}}", encoded, 0, encoded, 0);
    samples_resolve(message); assert(g_compatFault && !g_npmap);
    reset();
    strcpy_s(changed, sizeof(changed), encoded);
    /* 34 UTF-16 bytes leave two '=' padding bytes; nonzero unused bits are invalid. */
    assert(changed[n-1] == '=' && changed[n-2] == '='); changed[n-3] = 'B';
    sprintf_s(message, sizeof(message), "{\"op\":{\"samples\":[{\"p\":\"%s\",\"h\":\"%064d\",\"s\":4}]}}", changed, 0);
    samples_resolve(message); assert(g_compatFault && !g_npmap);
    puts("PASS conflicting source metadata and noncanonical path base64 fail before any local path mapping");
}
static void hashing(void) {
    wchar_t root[MAX_PATH], file[MAX_PATH]; char hex[65]; HANDLE writer;
    FILE *out = NULL;
    assert(GetTempPathW(MAX_PATH, root) && GetTempFileNameW(root, L"s24", 0, file));
    assert(!_wfopen_s(&out, file, L"wb") && out); assert(fwrite("audio", 1, 5, out) == 5); assert(!fclose(out));
    reset(); assert(hash_file(file, hex) && strlen(hex) == 64);
    hash_failure = 1; assert(!hash_file(file, hex)); hash_failure = 0;
    finish_failure = 1; assert(!hash_file(file, hex) && !hex[0]); finish_failure = 0;
    writer = CreateFileW(file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    assert(writer != INVALID_HANDLE_VALUE && !hash_file(file, hex)); CloseHandle(writer);
    g_workBound = 1; g_workGen = 1; g_netGen = 2; assert(!hash_file(file, hex)); g_workBound = 0;
    assert(DeleteFileW(file));
    puts("PASS hash provider failures, actively written files and stale room generations cannot publish content identities");
}
static DWORD WINAPI concurrent_progress(void *unused) {
    (void)unused;
    assert(!g_xferDone && !g_xferTotal && !g_xferWhat[0]);
    g_xferDone = 987; g_xferTotal = 1000; g_xferWhat = "Uploading";
    return 0;
}
static void progress(void) {
    HANDLE worker; DWORD code;
    g_xferDone = 123; g_xferTotal = 456; g_xferWhat = "Downloading";
    worker = CreateThread(NULL, 0, concurrent_progress, NULL, 0, NULL); assert(worker);
    assert(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0 && GetExitCodeThread(worker, &code) && !code); CloseHandle(worker);
    assert(g_xferDone == 123 && g_xferTotal == 456 && !strcmp(g_xferWhat, "Downloading"));
    puts("PASS simultaneous upload/download workers keep independent progress counters and direction labels");
}
int main(void) {
    InitializeCriticalSection(&g_qLock); InitializeCriticalSection(&g_sendLock);
    InitializeCriticalSection(&g_netTextLock); InitializeCriticalSection(&g_sampleLock);
    InitializeCriticalSection(&g_lifecycleLock); InitializeCriticalSection(&g_ulockLock);
    snapshots(); reset(); paths(); reset(); sample_metadata(); hashing(); progress(); reset();
    free(g_pmap); g_pmap = NULL;
    puts("0.2.24 native regression sweep: OK"); return 0;
}
