#define FLIM_LOG_MAX_BYTES (64u * 1024u)
#include <assert.h>
#include <share.h>
#include "../../engine/flim_engine.c"

static wchar_t directory[FLIM_LOG_PATH_CAP];

static void logger_reset(void) {
    AcquireSRWLockExclusive(&g_logLock);
    log_close_file(); g_logInitialized = 0; g_logPath[0] = 0; g_logRetryAt = 0;
    ReleaseSRWLockExclusive(&g_logLock);
}
static void log_name(wchar_t *path, int rotation) {
    if (rotation) swprintf_s(path, FLIM_LOG_PATH_CAP, L"%s\\native-%lu.log.%d", directory, GetCurrentProcessId(), rotation);
    else swprintf_s(path, FLIM_LOG_PATH_CAP, L"%s\\native-%lu.log", directory, GetCurrentProcessId());
}
static void logs_remove(void) {
    int i; wchar_t path[FLIM_LOG_PATH_CAP];
    logger_reset();
    for (i = 0; i < 3; i++) { log_name(path, i); if (!DeleteFileW(path)) assert(GetLastError() == ERROR_FILE_NOT_FOUND); }
}
static char *log_read(int rotation, size_t *length) {
    wchar_t path[FLIM_LOG_PATH_CAP]; FILE *file = NULL; char *bytes;
    log_name(path, rotation); file = _wfsopen(path, L"rb", _SH_DENYNO); assert(file);
    assert(!fseek(file, 0, SEEK_END)); *length = (size_t)ftell(file); rewind(file);
    bytes = (char *)malloc(*length + 1); assert(bytes);
    assert(fread(bytes, 1, *length, file) == *length); bytes[*length] = 0; fclose(file);
    return bytes;
}
static void credentials(void) {
    char *text; size_t length; wchar_t expected[FLIM_LOG_PATH_CAP];
    const char *secrets[] = {
        "server: {\"blob\":\"private-blob-value\",\"t\":\"welcome\"}",
        "{\"password\":\"private-password-value\"}", "{\"OWNER\":\"private-owner-value\"}",
        "{\"hostToken\":\"private-host-value\"}", "{\"join_token\":\"private-join-value\"}",
        "Authorization: Bearer private-bearer-value", "password=private-url-value"
    };
    unsigned i, lines = 0;
    logmsg("BEGIN healthy startup");
    for (i = 0; i < sizeof(secrets) / sizeof(secrets[0]); i++) logmsg("%s", secrets[i]);
    SetLastError(1234); logmsg("control\r\nforged-entry\tkept on one line"); assert(GetLastError() == 1234);
    log_name(expected, 0); assert(!wcscmp(g_logPath, expected));
    text = log_read(0, &length);
    assert(length && strstr(text, "BEGIN healthy startup") && strstr(text, "fl=24.2.2.4597") && strstr(text, "tid="));
    assert(!strstr(text, "private-") && strstr(text, "[credential-bearing diagnostic omitted]"));
    assert(strstr(text, "control  forged-entry kept on one line"));
    for (i = 0; i < length; i++) if (text[i] == '\n') lines++;
    assert(lines == 9 && text[length - 1] == '\n'); free(text);
    puts("PASS Unicode per-run path, build/thread context, credential omission, line boundaries and preserved Win32 error state");
}
static DWORD WINAPI writer(LPVOID argument) {
    unsigned thread = (unsigned)(uintptr_t)argument, i;
    for (i = 0; i < 60; i++) {
        SetLastError(4321); logmsg("thread:%u entry:%u END", thread, i); assert(GetLastError() == 4321);
    }
    return 0;
}
static void concurrent(void) {
    HANDLE threads[6]; unsigned i, found[6][60] = {{0}}, count = 0; char *text, *line; size_t length;
    logs_remove();
    for (i = 0; i < 6; i++) { threads[i] = CreateThread(NULL, 0, writer, (LPVOID)(uintptr_t)i, 0, NULL); assert(threads[i]); }
    assert(WaitForMultipleObjects(6, threads, TRUE, 10000) == WAIT_OBJECT_0);
    for (i = 0; i < 6; i++) CloseHandle(threads[i]);
    text = log_read(0, &length); assert(length < FLIM_LOG_MAX_BYTES);
    for (line = text; *line; ) {
        char *record = strstr(line, "thread:"), *end = strchr(line, '\n'); unsigned thread, entry;
        assert(record && end && record < end && sscanf_s(record, "thread:%u entry:%u END", &thread, &entry) == 2);
        assert(thread < 6 && entry < 60 && !found[thread][entry]); found[thread][entry] = 1; count++;
        line = end + 1;
    }
    assert(count == 360); free(text);
    puts("PASS concurrent writers produce 360 complete, distinct records without interleaving");
}
static void rotation(void) {
    char message[2048], *text; size_t length; int i; wchar_t fourth[FLIM_LOG_PATH_CAP];
    logs_remove(); memset(message, 'X', sizeof(message) - 1); message[sizeof(message) - 1] = 0;
    for (i = 0; i < 250; i++) logmsg("rotation:%d %s", i, message);
    logmsg("ROTATION_FINAL");
    for (i = 0; i < 3; i++) {
        text = log_read(i, &length); assert(length && length <= FLIM_LOG_MAX_BYTES && text[length - 1] == '\n');
        if (!i) assert(strstr(text, "ROTATION_FINAL")); free(text);
    }
    log_name(fourth, 3); assert(GetFileAttributesW(fourth) == INVALID_FILE_ATTRIBUTES);
    puts("PASS repeated rotation retains only the active log and two bounded backups, with complete final records");
}
static void failures_and_fallback(void) {
    wchar_t missing[FLIM_LOG_PATH_CAP], native[FLIM_LOG_PATH_CAP], fallback[FLIM_LOG_PATH_CAP], temp[FLIM_LOG_PATH_CAP];
    logs_remove(); swprintf_s(missing, FLIM_LOG_PATH_CAP, L"%s\\missing", directory);
    assert(SetEnvironmentVariableW(L"FLIM_LOG_DIR", missing));
    SetLastError(5678); logmsg("unavailable directory");
    assert(GetLastError() == 5678 && g_logFile == INVALID_HANDLE_VALUE && g_logRetryAt);
    assert(CreateDirectoryW(missing, NULL)); g_logRetryAt = 0; logmsg("directory recovered");
    assert(g_logFile != INVALID_HANDLE_VALUE); wcscpy_s(native, FLIM_LOG_PATH_CAP, g_logPath);
    logger_reset(); assert(DeleteFileW(native)); assert(RemoveDirectoryW(missing));
    assert(SetEnvironmentVariableW(L"FLIM_LOG_DIR", L"relative-logging-path")); log_path_init();
    GetTempPathW(FLIM_LOG_PATH_CAP, temp); swprintf_s(fallback, FLIM_LOG_PATH_CAP, L"%sflim_engine.log", temp);
    assert(!wcscmp(g_logPath, fallback)); logger_reset();
    assert(SetEnvironmentVariableW(L"FLIM_LOG_DIR", NULL)); log_path_init(); assert(!wcscmp(g_logPath, fallback));
    logger_reset(); assert(SetEnvironmentVariableW(L"FLIM_LOG_DIR", directory));
    puts("PASS unavailable destinations are nonfatal and recoverable; absent or relative overrides retain the legacy temp path");
}
int main(void) {
    wchar_t temp[FLIM_LOG_PATH_CAP];
    assert(GetTempPathW(FLIM_LOG_PATH_CAP, temp));
    swprintf_s(directory, FLIM_LOG_PATH_CAP, L"%sflim-logs-%lu-%llu-\x6d4b\x8bd5", temp, GetCurrentProcessId(), GetTickCount64());
    assert(CreateDirectoryW(directory, NULL)); assert(SetEnvironmentVariableW(L"FLIM_LOG_DIR", directory));
    g_fl.build = L"24.2.2.4597";
    credentials(); concurrent(); rotation(); failures_and_fallback(); logs_remove(); assert(RemoveDirectoryW(directory));
    puts("Native logging: OK");
    return 0;
}
