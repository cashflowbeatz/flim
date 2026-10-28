#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int deny_move;
static BOOL WINAPI status_move(LPCWSTR from, LPCWSTR to, DWORD flags) {
    if (deny_move) {SetLastError(ERROR_ACCESS_DENIED); return FALSE;}
    return MoveFileExW(from, to, flags);
}
#define MoveFileExW status_move
#include "../../common/flim_files.h"
#undef MoveFileExW

int main(void) {
    wchar_t base[MAX_PATH], directory[MAX_PATH], file[MAX_PATH], temporary[MAX_PATH];
    char hash[65]; int i, found = 0;
    assert(GetTempPathW(MAX_PATH, base)); assert(GetTempFileNameW(base, L"ffs", 0, directory));
    assert(DeleteFileW(directory) && CreateDirectoryW(directory, NULL));
    assert(SetEnvironmentVariableW(L"FLIM_FILES_DIR", directory));
    ff_begin(L"kick.wav", NULL, "download", 100, "ready");
    assert(ff_current.done == 100 && ff_entries[0].done == 100);
    ff_begin(L"C:\\", NULL, "download", 10, "checking"); assert(!strcmp(ff_current.name, "Sample file"));
    memset(ff_entries, 0, sizeof(ff_entries)); ff_count = 0;
    ff_begin(L"pending.wav", NULL, "download", 100, "waiting");
    for (i = 1; i < FF_MAX_ITEMS; i++) {
        sprintf_s(hash, sizeof(hash), "%064x", i); ff_begin(L"ready.wav", hash, "download", 100, "ready");
    }
    sprintf_s(hash, sizeof(hash), "%064x", FF_MAX_ITEMS); ff_begin(L"new.wav", hash, "download", 100, "downloading");
    for (i = 0; i < ff_count; i++) if (!strcmp(ff_entries[i].name, "pending.wav")) found++;
    assert(found == 1 && ff_count == FF_MAX_ITEMS);
    for (i = 0; i < ff_count; i++) strcpy_s(ff_entries[i].state, sizeof(ff_entries[i].state), "waiting");
    sprintf_s(hash, sizeof(hash), "%064x", FF_MAX_ITEMS + 1); ff_begin(L"extra-ready.wav", hash, "download", 100, "ready");
    for (i = 0; i < ff_count; i++) assert(strcmp(ff_entries[i].name, "extra-ready.wav"));
    swprintf_s(file, MAX_PATH, L"%s\\files-%lu.json", directory, GetCurrentProcessId());
    swprintf_s(temporary, MAX_PATH, L"%s.part", file);
    deny_move = 1; ff_finish("waiting", "retrying");
    assert(GetFileAttributesW(temporary) == INVALID_FILE_ATTRIBUTES);
    assert(GetFileAttributesW(file) != INVALID_FILE_ATTRIBUTES);
    assert(DeleteFileW(file) && RemoveDirectoryW(directory));
    puts("File status: OK (ready progress, empty names, pending retention, failed-publish cleanup)");
    return 0;
}
