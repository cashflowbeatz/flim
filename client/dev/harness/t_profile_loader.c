/* Prove an uncompiled build can be selected, while a changed engine/profile is refused.
   Uses a synthetic PE file in a unique temporary folder; it is never executed or loaded. */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
static wchar_t module_path[MAX_PATH * 2];
static DWORD WINAPI fixture_module(HMODULE h, wchar_t *out, DWORD cap) {
    (void)h; assert(wcslen(module_path) < cap); wcscpy_s(out, cap, module_path); return (DWORD)wcslen(out);
}
#define GetModuleFileNameW fixture_module
#include "../../common/flim_profile.h"
#undef GetModuleFileNameW

static unsigned char pe[8192];
static char profile[FLIM_PROFILE_MAX + 1];
static void write_bytes(const wchar_t *path, const void *data, DWORD n) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written; assert(file != INVALID_HANDLE_VALUE);
    assert(WriteFile(file, data, n, &written, NULL) && written == n); CloseHandle(file);
}
int main(void) {
    wchar_t root[MAX_PATH], dir[MAX_PATH * 2], engine[MAX_PATH * 2], path[MAX_PATH * 2], build[32], why[256];
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)pe;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(pe + 0x80);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    FlBuild row = {0}, before; char sha[65]; HANDLE file; size_t i, n;
    assert(GetTempPathW(MAX_PATH, root));
    swprintf_s(dir, MAX_PATH * 2, L"%sflim-profile-fixture-%lu-%llu", root, GetCurrentProcessId(), GetTickCount64());
    assert(CreateDirectoryW(dir, NULL));
    swprintf_s(module_path, MAX_PATH * 2, L"%s\\flim.exe", dir);
    swprintf_s(engine, MAX_PATH * 2, L"%s\\fixture.dll", dir);
    swprintf_s(path, MAX_PATH * 2, L"%s\\profiles", dir); assert(CreateDirectoryW(path, NULL));
    swprintf_s(path, MAX_PATH * 2, L"%s\\profiles\\25.9.9.65000.profile", dir);
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
    nt->Signature = IMAGE_NT_SIGNATURE; nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 1; nt->FileHeader.SizeOfOptionalHeader = sizeof(nt->OptionalHeader);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.ImageBase = FLIM_IMAGE_BASE; nt->OptionalHeader.SizeOfImage = sizeof(pe);
    section = IMAGE_FIRST_SECTION(nt);
    section->VirtualAddress = 0x1000; section->Misc.VirtualSize = 0x1000;
    section->Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
    write_bytes(engine, pe, sizeof(pe));
    file = CreateFileW(engine, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(file != INVALID_HANDLE_VALUE && flim_profile_sha256(file, sha)); CloseHandle(file);
    n = sprintf_s(profile, sizeof(profile), "schema=1\nbuild=25.9.9.65000\nsha256=%s\nwire=" FLIM_WIRE "\nmsgbox=1\n", sha);
    for (i = 0; i < FLIM_PROFILE_FIELDS; i++) {
        const FlProfileField *f = &flim_profile_fields[i]; uint64_t value = f->width == 8 ? 0x401000 : 0x10;
        if (!strcmp(f->name, "MIX_STRIDE")) value = 0x100;
        n += sprintf_s(profile + n, sizeof(profile) - n, "%s=0x%llx\n", f->name, (unsigned long long)value);
    }
    assert(flim_profile_load(NULL, engine, L"25.9.9.65000", &row, build, why, 256) == 0);
    write_bytes(path, profile, (DWORD)n);
    assert(flim_profile_load(NULL, engine, L"25.9.9.65000", &row, build, why, 256) == 1);
    assert(!wcscmp(row.build, L"25.9.9.65000") && row.REC == 0x401000 && row.MIX_STRIDE == 0x100);
    before = row;
    pe[4096] = 1; write_bytes(engine, pe, sizeof(pe));
    assert(flim_profile_load(NULL, engine, L"25.9.9.65000", &row, build, why, 256) == -1);
    assert(why[0] && row.REC == before.REC);
    pe[4096] = 0; write_bytes(engine, pe, sizeof(pe));
    profile[n - 2] = 'z'; write_bytes(path, profile, (DWORD)n);
    assert(flim_profile_load(NULL, engine, L"25.9.9.65000", &row, build, why, 256) == -1);
    assert(DeleteFileW(path) && DeleteFileW(engine));
    swprintf_s(path, MAX_PATH * 2, L"%s\\profiles", dir); assert(RemoveDirectoryW(path));
    assert(RemoveDirectoryW(dir));
    puts("External profile selection: OK (uncompiled build admitted; absent, modified-engine and corrupted profiles refused; synthetic PE never executed)");
    return 0;
}
