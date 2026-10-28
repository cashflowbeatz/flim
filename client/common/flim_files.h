#ifndef FLIM_FILES_H
#define FLIM_FILES_H

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FF_MAX_ITEMS 96
#define FF_NAME_CAP 768
typedef struct {
    char id[96], name[FF_NAME_CAP], direction[12], state[16], reason[32];
    uint64_t bytes, done, updated;
} FlimFileEntry;
static SRWLOCK ff_lock = SRWLOCK_INIT;
static FlimFileEntry ff_entries[FF_MAX_ITEMS];
static int ff_count;
static wchar_t ff_directory[4096];
static int ff_initialized;
static __declspec(thread) FlimFileEntry ff_current;
static __declspec(thread) int ff_active;
static __declspec(thread) ULONGLONG ff_last_progress;

static uint64_t ff_now(void) {
    FILETIME t; ULARGE_INTEGER n;
    GetSystemTimeAsFileTime(&t); n.LowPart = t.dwLowDateTime; n.HighPart = t.dwHighDateTime;
    return (n.QuadPart - 116444736000000000ULL) / 10000;
}

static void ff_json(FILE *file, const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    fputc('"', file);
    for (; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', file); fputc(*p, file); }
        else if (*p < 32) fprintf(file, "\\u%04x", *p);
        else fputc(*p, file);
    }
    fputc('"', file);
}

/* Called only by the transfer workers. A failed status write cannot stop sharing. */
static void ff_publish(void) {
    wchar_t file[4200], temporary[4200]; FILE *out = NULL; int i, ok;
    if (!ff_initialized) {
        DWORD n = GetEnvironmentVariableW(L"FLIM_FILES_DIR", ff_directory, 4096);
        ff_initialized = 1;
        if (!n || n >= 4096 || !((ff_directory[0] && ff_directory[1] == L':') ||
            (ff_directory[0] == L'\\' && ff_directory[1] == L'\\'))) ff_directory[0] = 0;
    }
    if (!ff_directory[0]) return;
    if (_snwprintf_s(file, 4200, _TRUNCATE, L"%s\\files-%lu.json", ff_directory, GetCurrentProcessId()) < 0 ||
        _snwprintf_s(temporary, 4200, _TRUNCATE, L"%s.part", file) < 0 || _wfopen_s(&out, temporary, L"wb")) return;
    fprintf(out, "{\"version\":1,\"pid\":%lu,\"updatedAt\":%llu,\"items\":[", GetCurrentProcessId(), (unsigned long long)ff_now());
    for (i = 0; i < ff_count; i++) {
        FlimFileEntry *e = &ff_entries[i];
        if (i) fputc(',', out);
        fputs("{\"id\":", out); ff_json(out, e->id);
        fputs(",\"name\":", out); ff_json(out, e->name);
        fputs(",\"direction\":", out); ff_json(out, e->direction);
        fputs(",\"state\":", out); ff_json(out, e->state);
        fputs(",\"reason\":", out); ff_json(out, e->reason);
        fprintf(out, ",\"bytes\":%llu,\"done\":%llu,\"updatedAt\":%llu}",
            (unsigned long long)e->bytes, (unsigned long long)e->done, (unsigned long long)e->updated);
    }
    fputs("]}", out); ok = !ferror(out); if (fclose(out)) ok = 0;
    if (ok) { if (!MoveFileExW(temporary, file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) DeleteFileW(temporary); }
    else DeleteFileW(temporary);
}

static void ff_save(void) {
    int i, oldest = 0;
    ff_current.updated = ff_now();
    AcquireSRWLockExclusive(&ff_lock);
    for (i = 0; i < ff_count && strcmp(ff_entries[i].id, ff_current.id); i++) {}
    if (i == ff_count && ff_count == FF_MAX_ITEMS) {
        for (i = 1; i < ff_count; i++) {
            int ready = !strcmp(ff_entries[i].state, "ready"), previous_ready = !strcmp(ff_entries[oldest].state, "ready");
            if ((ready && !previous_ready) || (ready == previous_ready && ff_entries[i].updated < ff_entries[oldest].updated)) oldest = i;
        }
        if (!strcmp(ff_current.state, "ready") && strcmp(ff_entries[oldest].state, "ready")) {
            ReleaseSRWLockExclusive(&ff_lock); return;
        }
        i = oldest;
    } else if (i == ff_count) ff_count++;
    ff_entries[i] = ff_current;
    ff_publish();
    ReleaseSRWLockExclusive(&ff_lock);
}

static void ff_begin(const wchar_t *path, const char *hash, const char *direction, uint64_t bytes, const char *state) {
    const wchar_t *name = path, *p;
    uint64_t identity = 1469598103934665603ULL;
    memset(&ff_current, 0, sizeof(ff_current));
    for (p = path; *p; p++) { identity = (identity ^ (uint16_t)*p) * 1099511628211ULL; if (*p == L'\\' || *p == L'/') name = p + 1; }
    if (!*name || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, -1, ff_current.name, FF_NAME_CAP, NULL, NULL))
        strcpy_s(ff_current.name, FF_NAME_CAP, "Sample file");
    if (hash && strlen(hash) == 64) sprintf_s(ff_current.id, sizeof(ff_current.id), "%s:%s", direction, hash);
    else sprintf_s(ff_current.id, sizeof(ff_current.id), "%s:path-%016llx", direction, (unsigned long long)identity);
    strcpy_s(ff_current.direction, sizeof(ff_current.direction), direction);
    strcpy_s(ff_current.state, sizeof(ff_current.state), state);
    ff_current.bytes = bytes; ff_current.done = !strcmp(state, "ready") ? bytes : 0;
    ff_active = 1; ff_last_progress = GetTickCount64(); ff_save();
}

static void ff_progress(DWORD bytes) {
    ULONGLONG now;
    if (!ff_active) return;
    ff_current.done += bytes;
    if (ff_current.done > ff_current.bytes) ff_current.done = ff_current.bytes;
    now = GetTickCount64();
    if (now - ff_last_progress >= 250) { ff_last_progress = now; ff_save(); }
}

static void ff_identify(const char *hash) {
    int i;
    AcquireSRWLockExclusive(&ff_lock);
    for (i = 0; i < ff_count; i++) if (!strcmp(ff_entries[i].id, ff_current.id)) {
        ff_entries[i] = ff_entries[--ff_count]; break;
    }
    ReleaseSRWLockExclusive(&ff_lock);
    sprintf_s(ff_current.id, sizeof(ff_current.id), "%s:%s", ff_current.direction, hash);
}

static void ff_finish(const char *state, const char *reason) {
    if (!ff_active) return;
    strcpy_s(ff_current.state, sizeof(ff_current.state), state);
    strcpy_s(ff_current.reason, sizeof(ff_current.reason), reason);
    if (!strcmp(state, "ready")) ff_current.done = ff_current.bytes;
    ff_save(); ff_active = 0;
}

#endif
