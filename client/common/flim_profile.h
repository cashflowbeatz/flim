#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flim_compat.h"
#pragma comment(lib, "bcrypt.lib")

#define FLIM_IMAGE_BASE 0x400000
#define FLIM_PROFILE_MAX 16384

typedef struct {
    const wchar_t *build;
    int msgbox;
    uint64_t REC, MUTE, MIXARR, MIXCNT, MAINFORM, SAVEPRESET, LOADPRESET, HASPLUGIN, GETPLUGIN, INTFCLEAR, ADDCHAN, DELCHAN, RESOLVER, CHANLIST, MOVECHAN, MOVEOBJ, MOVEARG, CHANFILT, PATARR, NOTEARR, NOTEDEL, NOTELOCK, GRIDREFRESH, FLPROJ, MAINOBJ, SETPROJ, SAVEPROJ, BUSYBEGIN, BUSYEND, PROJNAME, ARRS, ARRCUR, PLFORM, PLREFRESH, HINT, MIXNAME, MIXCOLOR, UIUPDATE, MIXOBJ, SETROUTE, ROUTEUPD, PATNAME, PATCOLOR, PATREFRESH, PLTRACK, MARKADD, MARKTYPE, DYNSETLEN, MARKREFRESH, MARKSORT, PATLENUPD, PATMARKS, SONGLOCK, MODIFIED, TSNUM, TSDEN, TSSET, TSCTLS, MSGBOX, MSGBOX_CORE, PRFORM, CURPAT;
    uint32_t CH_SELECTED, CH_GRID, FORM_BUSY, PAT_STRIDE, PLFORM_PATS, PL_MARKSEL, ED_TICKSPERPX, ED_HSCROLL, PR_GRID, PL_GRID, GRID_VSCROLL;
    uint32_t MIX_STRIDE, MIX_SLOTS_OFF, MIX_ROUTE;
    uint32_t FLPROJ_LOAD_SLOT, PATOP_CLONE_SLOT, PATOP_DELETE_SLOT, PATOP_REARRANGE_SLOT, PATOP_EDIT_SLOT, PATOP_INSERT_SLOT, PATOP_SPLIT_SLOT;
} FlBuild;

typedef struct { const char *name; size_t offset; int width, code; } FlProfileField;
#define PF64(n, c) { #n, offsetof(FlBuild, n), 8, c }
#define PF32(n) { #n, offsetof(FlBuild, n), 4, 0 }
static const FlProfileField flim_profile_fields[] = {
    PF64(REC,1), PF64(MUTE,1), PF64(MIXARR,0), PF64(MIXCNT,0), PF64(MAINFORM,0),
    PF64(SAVEPRESET,1), PF64(LOADPRESET,1), PF64(HASPLUGIN,1), PF64(GETPLUGIN,1), PF64(INTFCLEAR,1),
    PF64(ADDCHAN,1), PF64(DELCHAN,1), PF64(RESOLVER,1), PF64(CHANLIST,0), PF64(MOVECHAN,1),
    PF64(MOVEOBJ,0), PF64(MOVEARG,0), PF64(CHANFILT,0), PF64(PATARR,0), PF64(NOTEARR,1), PF64(NOTEDEL,1),
    PF64(NOTELOCK,0), PF64(GRIDREFRESH,1), PF64(FLPROJ,0), PF64(MAINOBJ,0), PF64(SETPROJ,1),
    PF64(SAVEPROJ,1), PF64(BUSYBEGIN,1), PF64(BUSYEND,1), PF64(PROJNAME,0), PF64(ARRS,0), PF64(ARRCUR,0),
    PF64(PLFORM,0), PF64(PLREFRESH,1), PF64(HINT,1), PF64(MIXNAME,1), PF64(MIXCOLOR,1), PF64(UIUPDATE,1),
    PF64(MIXOBJ,0), PF64(SETROUTE,1), PF64(ROUTEUPD,1), PF64(PATNAME,1), PF64(PATCOLOR,1), PF64(PATREFRESH,1),
    PF64(PLTRACK,1), PF64(MARKADD,1), PF64(MARKTYPE,1), PF64(DYNSETLEN,1), PF64(MARKREFRESH,1),
    PF64(MARKSORT,1), PF64(PATLENUPD,1), PF64(PATMARKS,1), PF64(SONGLOCK,0), PF64(MODIFIED,1),
    PF64(TSNUM,0), PF64(TSDEN,0), PF64(TSSET,1), PF64(TSCTLS,1), PF64(MSGBOX,1), PF64(MSGBOX_CORE,1),
    PF64(PRFORM,0), PF64(CURPAT,0),
    PF32(CH_SELECTED), PF32(CH_GRID), PF32(FORM_BUSY), PF32(PAT_STRIDE), PF32(PLFORM_PATS), PF32(PL_MARKSEL),
    PF32(ED_TICKSPERPX), PF32(ED_HSCROLL), PF32(PR_GRID), PF32(PL_GRID), PF32(GRID_VSCROLL),
    PF32(MIX_STRIDE), PF32(MIX_SLOTS_OFF), PF32(MIX_ROUTE), PF32(FLPROJ_LOAD_SLOT), PF32(PATOP_CLONE_SLOT),
    PF32(PATOP_DELETE_SLOT), PF32(PATOP_REARRANGE_SLOT), PF32(PATOP_EDIT_SLOT), PF32(PATOP_INSERT_SLOT), PF32(PATOP_SPLIT_SLOT)
};
#undef PF64
#undef PF32
#define FLIM_PROFILE_FIELDS (sizeof(flim_profile_fields) / sizeof(flim_profile_fields[0]))

static int flim_profile_hex(const char *s, uint64_t *value) {
    uint64_t n = 0; int digits = 0;
    if (s[0] != '0' || s[1] != 'x') return 0;
    for (s += 2; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else return 0;
        if (++digits > 16) return 0;
        n = (n << 4) | d;
    }
    *value = n;
    return digits != 0;
}

/* Complete, bounded, all-or-nothing parsing. No missing-field defaults or nearest-build fallback. */
static int flim_profile_parse(char *text, size_t n, const wchar_t *expected, FlBuild *out,
                              wchar_t build[32], char sha[65]) {
    FlBuild row = {0}; unsigned char seen[FLIM_PROFILE_FIELDS] = {0};
    char *line = text; unsigned meta = 0; size_t i;
    if (!n || n > FLIM_PROFILE_MAX || memchr(text, 0, n)) return 0;
    text[n] = 0;
    while (*line) {
        char *next = strchr(line, '\n'), *eq; uint64_t value;
        if (next) *next++ = 0;
        n = strlen(line);
        if (n && line[n-1] == '\r') line[--n] = 0;
        if (!n || !(eq = strchr(line, '=')) || strchr(eq + 1, '=')) return 0;
        *eq++ = 0;
        if (!strcmp(line, "schema")) { if ((meta & 1) || strcmp(eq, "1")) return 0; meta |= 1; }
        else if (!strcmp(line, "build")) {
            if ((meta & 2) || !flim_version_family(eq) || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, eq, -1, build, 32) || wcscmp(build, expected)) return 0;
#ifndef FLIM_EXPERIMENTAL_FL26
            if (strncmp(eq, "25.", 3)) return 0;
#endif
            meta |= 2;
        } else if (!strcmp(line, "sha256")) {
            if ((meta & 4) || strlen(eq) != 64 || strspn(eq, "0123456789abcdef") != 64) return 0;
            strcpy_s(sha, 65, eq); meta |= 4;
        } else if (!strcmp(line, "wire")) { if ((meta & 8) || strcmp(eq, FLIM_WIRE)) return 0; meta |= 8; }
        else if (!strcmp(line, "msgbox")) {
            if ((meta & 16) || (strcmp(eq, "1") && strcmp(eq, "2"))) return 0;
            row.msgbox = eq[0] - '0'; meta |= 16;
        } else {
            for (i = 0; i < FLIM_PROFILE_FIELDS; i++) if (!strcmp(line, flim_profile_fields[i].name)) break;
            if (i == FLIM_PROFILE_FIELDS || seen[i] || !flim_profile_hex(eq, &value)) return 0;
            if (flim_profile_fields[i].width == 4) {
                if (!value || value > 0x10000 ||
                    (strlen(line) > 5 && !strcmp(line + strlen(line) - 5, "_SLOT") && (value > 0x1000 || value % 8))) return 0;
                *(uint32_t *)((char *)&row + flim_profile_fields[i].offset) = (uint32_t)value;
            } else *(uint64_t *)((char *)&row + flim_profile_fields[i].offset) = value;
            seen[i] = 1;
        }
        line = next ? next : line + strlen(line);
        if (!next) break;
    }
    if (meta != 31 || row.MIX_SLOTS_OFF >= row.MIX_STRIDE || row.MIX_ROUTE >= row.MIX_STRIDE) return 0;
    for (i = 0; i < FLIM_PROFILE_FIELDS; i++) if (!seen[i]) return 0;
    row.build = build;
    *out = row;
    return 1;
}

static int flim_profile_sha256(HANDLE file, char hex[65]) {
    BCRYPT_ALG_HANDLE alg = NULL; BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char buf[65536], digest[32]; DWORD got; int ok = 0, i;
    LARGE_INTEGER zero = {0};
    if (!SetFilePointerEx(file, zero, NULL, FILE_BEGIN) || BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) goto done;
    if (BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) < 0) goto done;
    for (;;) {
        if (!ReadFile(file, buf, sizeof(buf), &got, NULL)) goto done;
        if (!got) break;
        if (BCryptHashData(hash, buf, got, 0) < 0) goto done;
    }
    if (BCryptFinishHash(hash, digest, 32, 0) < 0) goto done;
    for (i = 0; i < 32; i++) sprintf_s(hex + i*2, 3, "%02x", digest[i]);
    ok = 1;
done:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

/* Check address ranges against the same open file that was hashed. This is a sanity check,
   not proof of function identity or object layout; a profile must also be reviewed and tested. */
static int flim_profile_pe(HANDLE file, const FlBuild *row) {
    HANDLE mapping = NULL; unsigned char *d = NULL; LARGE_INTEGER size;
    IMAGE_DOS_HEADER *dos; IMAGE_NT_HEADERS64 *nt; IMAGE_SECTION_HEADER *sections;
    size_t i, j, off; int ok = 0;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < sizeof(IMAGE_DOS_HEADER) || size.QuadPart > 0x40000000) return 0;
    mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!mapping || !(d = (unsigned char *)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0))) goto done;
    dos = (IMAGE_DOS_HEADER *)d;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || (uint64_t)dos->e_lfanew + sizeof(*nt) > (uint64_t)size.QuadPart) goto done;
    nt = (IMAGE_NT_HEADERS64 *)(d + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->OptionalHeader.ImageBase != FLIM_IMAGE_BASE ||
        nt->FileHeader.SizeOfOptionalHeader < sizeof(nt->OptionalHeader) || !nt->FileHeader.NumberOfSections || nt->FileHeader.NumberOfSections > 96) goto done;
    off = dos->e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
    if (off + (size_t)nt->FileHeader.NumberOfSections * sizeof(*sections) > (uint64_t)size.QuadPart) goto done;
    sections = (IMAGE_SECTION_HEADER *)(d + off);
    for (i = 0; i < FLIM_PROFILE_FIELDS; i++) {
        const FlProfileField *f = &flim_profile_fields[i]; uint64_t va, rva;
        if (f->width != 8) continue;
        va = *(const uint64_t *)((const char *)row + f->offset);
        if (!va && row->msgbox == 2 && !strcmp(f->name, "MSGBOX_CORE")) continue;
        if (va < FLIM_IMAGE_BASE || va - FLIM_IMAGE_BASE >= nt->OptionalHeader.SizeOfImage) goto done;
        rva = va - FLIM_IMAGE_BASE;
        for (j = 0; j < nt->FileHeader.NumberOfSections; j++) {
            uint64_t start = sections[j].VirtualAddress, len = sections[j].Misc.VirtualSize;
            if (rva >= start && rva - start < len &&
                (sections[j].Characteristics & (f->code ? IMAGE_SCN_MEM_EXECUTE : IMAGE_SCN_MEM_READ))) break;
        }
        if (j == nt->FileHeader.NumberOfSections) goto done;
    }
    ok = 1;
done:
    if (d) UnmapViewOfFile(d);
    if (mapping) CloseHandle(mapping);
    return ok;
}

/* 1=valid, 0=absent, -1=present but invalid. Profiles sit beside the client, never in FL. */
static int flim_profile_load(HMODULE client, const wchar_t *engine, const wchar_t *version,
                             FlBuild *out, wchar_t build[32], wchar_t *reason, size_t reasonCap) {
    wchar_t path[MAX_PATH * 2], *slash; char text[FLIM_PROFILE_MAX + 1], sha[65], actual[65];
    HANDLE file = INVALID_HANDLE_VALUE, target = INVALID_HANDLE_VALUE; LARGE_INTEGER size;
    DWORD got, count; FlBuild row = {0}; int result = -1;
    char narrow[32];
    reason[0] = 0;
    if (!WideCharToMultiByte(CP_UTF8, 0, version, -1, narrow, 32, NULL, NULL) || !flim_version_family(narrow)) return 0;
    count = GetModuleFileNameW(client, path, MAX_PATH * 2);
    if (!count || count >= MAX_PATH * 2 || !(slash = wcsrchr(path, L'\\'))) goto done;
    slash[1] = 0;
    if (wcslen(path) + wcslen(L"profiles\\") + wcslen(version) + wcslen(L".profile") >= MAX_PATH * 2) goto done;
    if (wcscat_s(path, MAX_PATH * 2, L"profiles\\") || wcscat_s(path, MAX_PATH * 2, version) || wcscat_s(path, MAX_PATH * 2, L".profile")) goto done;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return 0;
        goto done;
    }
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 1 || size.QuadPart > FLIM_PROFILE_MAX ||
        !ReadFile(file, text, (DWORD)size.QuadPart, &got, NULL) || got != size.QuadPart ||
        !flim_profile_parse(text, got, version, &row, build, sha)) goto done;
    target = CreateFileW(engine, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (target == INVALID_HANDLE_VALUE || !flim_profile_sha256(target, actual) || strcmp(sha, actual) || !flim_profile_pe(target, &row)) goto done;
    *out = row;
    result = 1;
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (target != INVALID_HANDLE_VALUE) CloseHandle(target);
    if (result < 0) swprintf_s(reason, reasonCap, L"The compatibility profile is invalid or does not match FLEngine_x64.dll. Install a reviewed profile for this exact engine file.");
    return result;
}
