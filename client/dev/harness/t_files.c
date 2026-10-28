#include <windows.h>
#include <shlobj.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static int fail_malloc, fail_calloc, fail_seek, fail_tell, short_read, fail_close;
static int write_mode, write_calls, fail_flush, fail_move, fail_copy, fail_documents;
static void *file_malloc(size_t size) { return fail_malloc ? NULL : malloc(size); }
static void *file_calloc(size_t n, size_t size) { return fail_calloc ? NULL : calloc(n, size); }
static int file_seek(FILE *file, __int64 offset, int origin) { return fail_seek ? -1 : _fseeki64(file, offset, origin); }
static __int64 file_tell(FILE *file) { return fail_tell ? -1 : _ftelli64(file); }
static size_t file_read(void *out, size_t size, size_t count, FILE *file) {
    return fread(out, size, short_read && count ? count - 1 : count, file);
}
static int file_close(FILE *file) { int result = fclose(file); return fail_close ? EOF : result; }
static BOOL WINAPI file_write(HANDLE file, LPCVOID bytes, DWORD n, LPDWORD written, LPOVERLAPPED overlap) {
    write_calls++;
    if (write_mode == 2 && write_calls > 1) { *written = 0; SetLastError(ERROR_DISK_FULL); return FALSE; }
    if (write_mode && n > 1) n /= 2;
    return WriteFile(file, bytes, n, written, overlap);
}
static BOOL WINAPI file_flush(HANDLE file) { return fail_flush ? FALSE : FlushFileBuffers(file); }
static BOOL WINAPI file_move(LPCWSTR from, LPCWSTR to, DWORD flags) { return fail_move ? FALSE : MoveFileExW(from, to, flags); }
static BOOL WINAPI file_copy(LPCWSTR from, LPCWSTR to, BOOL exists) { return fail_copy ? FALSE : CopyFileW(from, to, exists); }
static HRESULT WINAPI file_documents(HWND parent, int folder, HANDLE token, DWORD flags, LPWSTR out) {
    return fail_documents ? E_FAIL : SHGetFolderPathW(parent, folder, token, flags, out);
}
#define malloc file_malloc
#define calloc file_calloc
#define _fseeki64 file_seek
#define _ftelli64 file_tell
#define fread file_read
#define fclose file_close
#define WriteFile file_write
#define FlushFileBuffers file_flush
#define MoveFileExW file_move
#define CopyFileW file_copy
#define SHGetFolderPathW file_documents
#include "../../engine/flim_engine.c"
#undef malloc
#undef calloc
#undef _fseeki64
#undef _ftelli64
#undef fread
#undef fclose
#undef WriteFile
#undef FlushFileBuffers
#undef MoveFileExW
#undef CopyFileW
#undef SHGetFolderPathW

static void reset(void) {
    fail_malloc = fail_calloc = fail_seek = fail_tell = short_read = fail_close = 0;
    write_mode = write_calls = fail_flush = fail_move = fail_copy = fail_documents = 0;
    g_compatFault = 0; g_compatReady = 1; g_alert[0] = 0;
}
static void equals(const wchar_t *path, const char *expected) {
    size_t n = 99; unsigned char *data = read_file_w(path, &n);
    assert(data && n == strlen(expected) && !memcmp(data, expected, n)); free(data);
}
static void no_temporary(const wchar_t *directory) {
    wchar_t pattern[MAX_PATH]; WIN32_FIND_DATAW found;
    swprintf_s(pattern, MAX_PATH, L"%s\\.flim-*.tmp", directory);
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search != INVALID_HANDLE_VALUE) FindClose(search);
    assert(search == INVALID_HANDLE_VALUE);
}
static void u32(unsigned char *out, uint32_t value) { memcpy(out, &value, 4); }
static void incoming_failure(void) {
    const char version[] = "25.2.3.5171";
    unsigned char project[128] = {0}; char encoded[256], message[512];
    size_t n = 24 + sizeof(version);
    memcpy(project, "FLhd", 4); u32(project + 4, 6);
    memcpy(project + 14, "FLdt", 4); u32(project + 18, 2 + sizeof(version));
    project[22] = 199; project[23] = sizeof(version); memcpy(project + 24, version, sizeof(version));
    b64enc(project, (int)n, encoded);
    sprintf_s(message, sizeof(message), "{\"op\":{\"name\":\"song.flp\",\"data\":\"%s\"}}", encoded);
    g_fl = k_flBuilds[0]; strcpy_s(g_hostFlBuild, sizeof(g_hostFlBuild), version);
    reset(); fail_documents = 1; on_project_load(message);
    assert(g_compatFault && !g_compatReady && g_qh == g_qt && !g_projBusy);
    reset(); on_project_load("{\"op\":{\"name\":\"a.flp\",\"name\":\"b.flp\",\"data\":\"AAAA\"}}");
    assert(g_compatFault && g_qh == g_qt);
    reset(); on_project_load("{\"op\":{\"name\":\"a.flp\",\"data\":\"A===\"}}");
    assert(g_compatFault && g_qh == g_qt);
    reset();
}

int main(void) {
    wchar_t base[MAX_PATH], directory[MAX_PATH], path[MAX_PATH], backup[MAX_PATH], missing[MAX_PATH], source[MAX_PATH], output[MAX_PATH];
    char narrow[MAX_PATH]; size_t n; unsigned char *data;
    InitializeCriticalSection(&g_sendLock); InitializeCriticalSection(&g_netTextLock);
    InitializeCriticalSection(&g_qLock); InitializeCriticalSection(&g_lifecycleLock);
    InitializeCriticalSection(&g_sampleLock); InitializeCriticalSection(&g_heldLock);
    {
        wchar_t expected[MAX_PATH], *value;
        char encoded[MAX_PATH];
        assert(WideCharToMultiByte(CP_ACP, 0, L"caf\x00e9.fst", -1, encoded, MAX_PATH, NULL, NULL));
        assert(MultiByteToWideChar(CP_ACP, 0, encoded, -1, expected, MAX_PATH));
        value = dstr_new(encoded);
        assert(value && !wcscmp(value, expected) && ((int32_t *)value)[-1] == (int32_t)wcslen(expected));
        dstr_free(value);
        fail_calloc = 1; assert(!dstr_new(encoded)); reset();
        assert(!dstr_new(NULL));
    }
    assert(GetTempPathW(MAX_PATH, base));
    assert(GetTempFileNameW(base, L"flm", 0, directory));
    assert(DeleteFileW(directory) && CreateDirectoryW(directory, NULL));
    swprintf_s(path, MAX_PATH, L"%s\\project.flp", directory);
    swprintf_s(backup, MAX_PATH, L"%s.bak", path);
    swprintf_s(source, MAX_PATH, L"%s\\source.flp", directory);
    swprintf_s(missing, MAX_PATH, L"%s\\absent.flp", directory);
    assert(WideCharToMultiByte(CP_ACP, 0, path, -1, narrow, MAX_PATH, NULL, NULL));
    assert(write_file_w(path, (const unsigned char *)"original", 8));
    n = 99; data = read_file(narrow, &n); assert(data && n == 8); free(data);
    n = 99; assert(!read_file_w(missing, &n) && n == 0);
    n = 99; fail_malloc = 1; assert(!read_file_w(path, &n) && n == 0); reset();
    n = 99; fail_seek = 1; assert(!read_file_w(path, &n) && n == 0); reset();
    n = 99; fail_tell = 1; assert(!read_file_w(path, &n) && n == 0); reset();
    n = 99; short_read = 1; assert(!read_file_w(path, &n) && n == 0); reset();
    n = 99; fail_close = 1; assert(!read_file_w(path, &n) && n == 0); reset();
    write_mode = 2; assert(!write_file_w(path, (const unsigned char *)"new contents", 12)); reset(); equals(path, "original"); no_temporary(directory);
    fail_flush = 1; assert(!write_file_w(path, (const unsigned char *)"new contents", 12)); reset(); equals(path, "original"); no_temporary(directory);
    fail_move = 1; assert(!write_file_w(path, (const unsigned char *)"new contents", 12)); reset(); equals(path, "original"); no_temporary(directory);
    {
        HANDLE locked = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        assert(locked != INVALID_HANDLE_VALUE);
        assert(!write_file_w(path, (const unsigned char *)"locked write", 12));
        assert(CloseHandle(locked)); equals(path, "original"); no_temporary(directory);
    }
    write_mode = 1; assert(write_file_w(path, (const unsigned char *)"replacement", 11) && write_calls > 1); reset(); equals(path, "replacement");
    assert(backup_existing(path)); equals(path, "replacement"); equals(backup, "replacement");
    assert(write_file_w(path, (const unsigned char *)"new version", 11));
    fail_copy = 1; assert(!backup_existing(path)); reset(); equals(path, "new version"); equals(backup, "replacement"); no_temporary(directory);
    fail_move = 1; assert(!backup_existing(path)); reset(); equals(path, "new version"); equals(backup, "replacement"); no_temporary(directory);
    assert(backup_existing(missing));
    assert(write_file_w(source, (const unsigned char *)"readonly source", 15));
    assert(SetFileAttributesW(source, FILE_ATTRIBUTE_READONLY));
    assert(copy_file_w(source, path)); equals(path, "readonly source");
    assert(!(GetFileAttributesW(path) & FILE_ATTRIBUTE_READONLY));
    assert(SetFileAttributesW(source, FILE_ATTRIBUTE_NORMAL));
    assert(flim_name(L"C:/folder.with.dots/song", output, MAX_PATH));
    assert(!wcsncmp(output, L"C:/folder.with.dots/song_flim_", 28));
    assert(!flim_name(L"song.flp", output, 20) && !output[0]);
    { wchar_t too_long[MAX_PATH + 1];
      wmemset(too_long, L'a', MAX_PATH); too_long[MAX_PATH] = 0;
      assert(!write_file_w(too_long, (const unsigned char *)"x", 1));
      assert(!backup_existing(too_long)); }
    {
        HANDLE big = CreateFileW(missing, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        LARGE_INTEGER size; size.QuadPart = FLIM_CLOUD_MAX + 1ull;
        assert(big != INVALID_HANDLE_VALUE && SetFilePointerEx(big, size, NULL, FILE_BEGIN) && SetEndOfFile(big) && CloseHandle(big));
        n = 99; assert(!read_file_w(missing, &n) && n == 0 && g_compatFault); reset();
        assert(DeleteFileW(missing));
    }
    incoming_failure(); no_temporary(directory);
    assert(DeleteFileW(source) && DeleteFileW(path) && DeleteFileW(backup) && RemoveDirectoryW(directory));
    puts("File safety: OK (full reads, short writes, disk/flush/rename faults, locked targets, intact backups, bounded paths, project refusal)");
    return 0;
}
