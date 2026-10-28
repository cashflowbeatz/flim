#include <windows.h>
#include <winhttp.h>
#include <assert.h>

static int partial_writes, cancel_read;
static BOOL WINAPI transfer_write(HINTERNET request, LPCVOID data, DWORD bytes, LPDWORD written) {
    if (partial_writes && bytes > 1024) bytes = 1024;
    return WinHttpWriteData(request, data, bytes, written);
}
static BOOL WINAPI transfer_read(HINTERNET request, LPVOID data, DWORD bytes, LPDWORD read);
#define WinHttpWriteData transfer_write
#define WinHttpReadData transfer_read
#include "../../engine/flim_engine.c"
#undef WinHttpWriteData
#undef WinHttpReadData

static BOOL WINAPI transfer_read(HINTERNET request, LPVOID data, DWORD bytes, LPDWORD read) {
    BOOL ok = WinHttpReadData(request, data, bytes, read);
    if (cancel_read && ok && *read) { cancel_read = 0; g_netGen++; }
    return ok;
}

static void lookup_mode(const char *mode) {
    assert(blob_http(L"POST", "/test-mode", NULL, 0, mode, (DWORD)strlen(mode), NULL, NULL, NULL, 0) == 200);
}

static void lookup_recovery(void) {
    SEnt entries[513] = {0};
    unsigned char image[2048] = {0};
    wchar_t full[MAX_PATH];
    size_t length, at, remain;
    char *message;
    int i;
    for (i = 0; i < 513; i++) sprintf_s(entries[i].hex, 65, "%064x", i);
    lookup_mode("retry");
    assert(samples_missing(entries, 1) && entries[0].upload);
    puts("PASS sample lookup retries one temporary service failure");
    lookup_mode("partial");
    assert(!samples_missing(entries, 513));
    for (i = 0; i < 513; i++) assert(!entries[i].upload);
    puts("PASS failed later lookup batch clears every partial upload decision");
    lookup_mode("alien");
    assert(!samples_missing(entries, 1) && !entries[0].upload);
    puts("PASS unrequested sample hashes are rejected");
    lookup_mode("failed");
    assert(GetFullPathNameW(L"upload.bin", MAX_PATH, full, NULL));
    length = (wcslen(full) + 1) * sizeof(wchar_t);
    memcpy(image, "FLhd", 4); *(uint32_t *)(image + 4) = 6;
    memcpy(image + 14, "FLdt", 4); at = 22;
    image[at++] = EV_SAMPLE_PATH; remain = length;
    do { unsigned char b = (unsigned char)(remain & 127); remain >>= 7; image[at++] = b | (remain ? 128 : 0); } while (remain);
    assert(at + length < sizeof(image)); memcpy(image + at, full, length); at += length;
    *(uint32_t *)(image + 18) = (uint32_t)(at - 22);
    message = samples_attach("{\"op\":{\"t\":\"test\"}}", image, at);
    assert(message && strstr(message, "\"samples\":[]") && !g_compatFault);
    free(message);
    puts("PASS failed sample lookup preserves the edit and does not stop sharing");
    lookup_mode("normal");
}

int main(int argc, char **argv) {
    SEnt *entries;
    FILE *file;
    char actual[65], expected[65];
    int i;
    uint64_t bytes;
    assert(argc == 2);
    InitializeCriticalSection(&g_sampleLock);
    InitializeCriticalSection(&g_netTextLock);
    InitializeCriticalSection(&g_sendLock);
    assert(parse_host(argv[1], &g_blobUrl));
    strcpy_s(g_blobKey, sizeof(g_blobKey), "local-transfer-fixture");
    g_blobOn = 1; g_cloudTransport = 1;
    assert(sample_sendable(90000000) && !sample_sendable(90000001) && !sample_sendable(0));
    g_cloudTransport = 0;
    assert(sample_sendable(SAMPLE_MAX_BYTES) && !sample_sendable(SAMPLE_MAX_BYTES + 1));
    g_cloudTransport = 1;

    entries = (SEnt *)calloc(1025, sizeof(SEnt));
    assert(entries);
    for (i = 0; i < 1025; i++) sprintf_s(entries[i].hex, 65, "%064x", i);
    assert(samples_missing(entries, 1025));
    for (i = 0; i < 1025; i++) assert(entries[i].upload == (i % 2 == 0));
    free(entries);
    puts("PASS 1025 sample references looked up in bounded batches");

    assert(!fopen_s(&file, "upload.bin", "wb"));
    for (i = 0; i < 65569; i++) fputc(i % 251, file);
    fclose(file);
    partial_writes = 1;
    assert(blob_http(L"PUT", "/blob/upload", L"upload.bin", 65569, NULL, 0, NULL, NULL, NULL, 0) == 201);
    partial_writes = 0;
    assert(g_xferDone == 65569);
    puts("PASS partial HTTP writes send the complete file");

    assert(blob_http(L"GET", "/blob/download", NULL, 0, NULL, 0, L"download.bin", actual, NULL, 0) == 200);
    assert(sha_hex("flim transfer download", 22, expected));
    assert(!strcmp(actual, expected));
    assert(file_info(L"download.bin", &bytes, NULL) && bytes == 22);
    puts("PASS streamed download content and checksum");

    lookup_recovery();

    g_workBound = 1; g_workGen = g_netGen = 3; cancel_read = 1;
    assert(blob_http(L"GET", "/blob/download", NULL, 0, NULL, 0, L"cancelled.bin", actual, NULL, 0) == 0);
    assert(file_info(L"cancelled.bin", &bytes, NULL) && bytes == 0);
    puts("PASS a stale session stops writing downloaded data");
    return 0;
}
