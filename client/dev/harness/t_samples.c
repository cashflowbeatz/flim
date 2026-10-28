// Offline test of the plugin-data sample path code (collect + same-length rewrite).
// build.bat, then t_samples.exe: prints OK and removes its owned temporary profile.
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>
#include <assert.h>
#include <stdlib.h>

static unsigned fail_realloc_at, realloc_calls;
static void *sample_test_realloc(void *p, size_t n) {
    realloc_calls++;
    if (fail_realloc_at && realloc_calls == fail_realloc_at) return NULL;
    return realloc(p, n);
}

static int path_mock;
static const wchar_t *shared_path, *profile_path;
static char test_temp_dir[MAX_PATH];
static HRESULT WINAPI sample_test_documents(HWND parent, int folder, HANDLE token, DWORD flags, LPWSTR out) {
    if (path_mock) {
        if (!profile_path) {out[0] = 0; return E_FAIL;}
        return swprintf_s(out, MAX_PATH, L"%s\\Documents", profile_path) > 0 ? S_OK : E_FAIL;
    }
    return SHGetFolderPathW(parent, folder, token, flags, out);
}
static LSTATUS WINAPI sample_test_registry(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD flags,
                                          LPDWORD type, PVOID out, LPDWORD bytes) {
    if (path_mock && key == HKEY_CURRENT_USER && !wcscmp(subkey, L"Software\\Image-Line\\Shared\\Paths") &&
        !wcscmp(value, L"Shared data")) {
        DWORD needed;
        assert((flags & (RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ)) == (RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ));
        if (!shared_path) return ERROR_FILE_NOT_FOUND;
        needed = (DWORD)(wcslen(shared_path) + 1) * sizeof(wchar_t);
        if (*bytes < needed) {
            if (flags & RRF_ZEROONFAILURE) ZeroMemory(out, *bytes);
            *bytes = needed; return ERROR_MORE_DATA;
        }
        memcpy(out, shared_path, needed); *bytes = needed;
        if (type) *type = REG_SZ;
        return ERROR_SUCCESS;
    }
    return RegGetValueW(key, subkey, value, flags, type, out, bytes);
}
static DWORD WINAPI sample_test_environment(LPCWSTR name, LPWSTR out, DWORD cap) {
    if (path_mock && !wcscmp(name, L"USERPROFILE")) {
        size_t n = profile_path ? wcslen(profile_path) : 0;
        if (!n) return 0;
        if (n >= cap) return (DWORD)n + 1;
        memcpy(out, profile_path, (n + 1) * sizeof(wchar_t)); return (DWORD)n;
    }
    return GetEnvironmentVariableW(name, out, cap);
}
static DWORD WINAPI sample_test_temp_path(DWORD cap, LPSTR out) {
    size_t n = strlen(test_temp_dir);
    if (!n) return GetTempPathA(cap, out);
    if (n >= cap) return (DWORD)n + 1;
    memcpy(out, test_temp_dir, n + 1);
    return (DWORD)n;
}
#define RegGetValueW sample_test_registry
#define GetEnvironmentVariableW sample_test_environment
#define GetTempPathA sample_test_temp_path
#define realloc sample_test_realloc
#define SHGetFolderPathW sample_test_documents
#include "../../engine/flim_engine.c"
#undef realloc
#undef RegGetValueW
#undef GetEnvironmentVariableW
#undef GetTempPathA
#undef SHGetFolderPathW

static int test_sample_macro_paths(void) {
    wchar_t out[512];
    int fails = 0;
    size_t n;
#define PATHCHECK(expr) do { if (!(expr)) { printf("path check failed at line %d\n", __LINE__); fails++; } } while (0)
    path_mock = 1;
    shared_path = L"D:\\Custom Root";
    profile_path = L"C:\\Users\\Test";
    PATHCHECK(expand_path(L"%ILSharedData%flim\\samples\\kick.wav", out, 512) &&
              !wcscmp(out, L"D:\\Custom Root\\flim\\samples\\kick.wav"));
    PATHCHECK(expand_path(L"%ilshareddata%\\flim\\samples\\kick.wav", out, 512) &&
              !wcscmp(out, L"D:\\Custom Root\\flim\\samples\\kick.wav"));
    PATHCHECK(expand_path(L"%FLStudioUserData%\\Samples\\kick.wav", out, 512) &&
              !wcscmp(out, L"D:\\Custom Root\\FL Studio\\Samples\\kick.wav"));
    shared_path = L"D:\\Custom Root\\";
    PATHCHECK(expand_path(L"%ILSharedData%flim\\samples\\kick.wav", out, 512) &&
              !wcscmp(out, L"D:\\Custom Root\\flim\\samples\\kick.wav"));
    PATHCHECK(expand_path(L"%ILSharedData%\\flim\\samples\\kick.wav", out, 512) &&
              !wcscmp(out, L"D:\\Custom Root\\flim\\samples\\kick.wav"));
    PATHCHECK(expand_path(L"%FLStudioUserData%Samples\\kick.wav", out, 512) &&
              !wcscmp(out, L"D:\\Custom Root\\FL Studio\\Samples\\kick.wav"));
    shared_path = L"\\\\studio\\shared";
    PATHCHECK(expand_path(L"%ILSharedData%flim\\kick.wav", out, 512) &&
              !wcscmp(out, L"\\\\studio\\shared\\flim\\kick.wav"));
    shared_path = NULL;
    PATHCHECK(expand_path(L"%ILSharedData%flim\\kick.wav", out, 512) &&
              !wcscmp(out, L"C:\\Users\\Test\\Documents\\Image-Line\\flim\\kick.wav"));
    PATHCHECK(expand_path(L"%FLStudioUserData%\\Samples\\kick.wav", out, 512) &&
              !wcscmp(out, L"C:\\Users\\Test\\Documents\\Image-Line\\FL Studio\\Samples\\kick.wav"));
    shared_path = L"";
    PATHCHECK(expand_path(L"%ILSharedData%flim\\kick.wav", out, 512));
    shared_path = L"D:\\Data";
    PATHCHECK(expand_path(L"%ILSharedData%kick.wav", out, 512));
    n = wcslen(out);
    PATHCHECK(expand_path(L"%ILSharedData%kick.wav", out, (int)n + 1));
    PATHCHECK(!expand_path(L"%ILSharedData%kick.wav", out, (int)n) && !out[0]);
    PATHCHECK(!expand_path(L"%ILSharedData%kick.wav", out, 1) && !out[0]);
    out[0] = L'x'; PATHCHECK(!expand_path(L"%ILSharedData%kick.wav", out, 0) && out[0] == L'x');
    shared_path = L"relative\\path";
    PATHCHECK(!expand_path(L"%ILSharedData%kick.wav", out, 512));
    shared_path = NULL; profile_path = NULL;
    PATHCHECK(!expand_path(L"%ILSharedData%kick.wav", out, 512) && !out[0]);
    PATHCHECK(!expand_path(L"%FLStudioFactoryData%kick.wav", out, 512));
    PATHCHECK(!expand_path(L"relative\\kick.wav", out, 512));
    PATHCHECK(expand_path(L"D:\\Samples\\kick.wav", out, 512) && !wcscmp(out, L"D:\\Samples\\kick.wav"));
    path_mock = 0;
#undef PATHCHECK
    printf("Sample path macro expansion: %s\n", fails ? "FAIL" : "OK");
    return fails;
}

static size_t put_ev(unsigned char *o, int id, const void *p, size_t n) {
    size_t k = 0, v = n;
    o[k++] = (unsigned char)id;
    do { unsigned char b = (unsigned char)(v & 0x7f); v >>= 7; if (v) b |= 0x80; o[k++] = b; } while (v);
    memcpy(o + k, p, n);
    return k + n;
}

static wchar_t owned_profile[MAX_PATH];

static int test_profile_create(void) {
    wchar_t base[MAX_PATH], candidate[MAX_PATH], absolute[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, base);
    unsigned attempt;
    if (!n || n >= MAX_PATH) return 0;
    for (attempt = 0; attempt < 8; attempt++) {
        if (swprintf_s(candidate, MAX_PATH, L"%sflim-samples-%lu-%llx-%u", base,
                       GetCurrentProcessId(), (unsigned long long)GetTickCount64(), attempt) < 0) return 0;
        n = GetFullPathNameW(candidate, MAX_PATH, absolute, NULL);
        if (!n || n >= MAX_PATH) return 0;
        if (CreateDirectoryW(absolute, NULL)) {
            wcscpy_s(owned_profile, MAX_PATH, absolute);
            return 1;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    }
    return 0;
}

static int test_profile_remove(const wchar_t *path) {
    wchar_t absolute[MAX_PATH], search[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAW item;
    HANDLE find;
    DWORD n = GetFullPathNameW(path, MAX_PATH, absolute, NULL), attributes;
    size_t root_length = wcslen(owned_profile);
    int ok = 1;
    if (!root_length || !n || n >= MAX_PATH || _wcsnicmp(absolute, owned_profile, root_length) ||
        (absolute[root_length] && absolute[root_length] != L'\\')) return 0;
    attributes = GetFileAttributesW(absolute);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return RemoveDirectoryW(absolute) != 0;
    if (swprintf_s(search, MAX_PATH, L"%s\\*", absolute) < 0) return 0;
    find = FindFirstFileW(search, &item);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(item.cFileName, L".") || !wcscmp(item.cFileName, L"..")) continue;
            if (swprintf_s(child, MAX_PATH, L"%s\\%s", absolute, item.cFileName) < 0) { ok = 0; continue; }
            if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!test_profile_remove(child)) ok = 0;
            } else if (!DeleteFileW(child)) ok = 0;
        } while (FindNextFileW(find, &item));
        if (GetLastError() != ERROR_NO_MORE_FILES) ok = 0;
        FindClose(find);
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) ok = 0;
    return RemoveDirectoryW(absolute) && ok;
}

static int test_peer_path(wchar_t *out, size_t cap, size_t length, const wchar_t *name) {
    static const wchar_t prefix[] = L"D:\\Alex\\Sample Packs\\";
    size_t start = wcslen(prefix), tail = wcslen(name), i;
    if (length >= cap || length < start + tail + 2) return 0;
    wcscpy_s(out, cap, prefix);
    for (i = start; i < length - tail - 1; i++) out[i] = (i - start) % 32 == 31 ? L'\\' : L'x';
    out[length - tail - 1] = L'\\';
    memcpy(out + length - tail, name, (tail + 1) * sizeof(wchar_t));
    return 1;
}

static int test_rewrite_failures(const wchar_t *peer, const wchar_t *local) {
    unsigned char img[16384] = {0}, nested[4096] = {0}, *d, *original;
    size_t n, dn, payload = (wcslen(peer) + 1) * 2, start, end;
    unsigned calls, at;
    RewriteCtx growth = {0};
    PathList paths = {0};
    memcpy(img, "FLhd", 4); *(uint32_t *)(img + 4) = 6;
    memcpy(img + 14, "FLdt", 4);
    *(uint32_t *)nested = 0x55; *(uint64_t *)(nested + 4) = payload + 12;
    *(uint32_t *)(nested + 12) = 0x66; *(uint64_t *)(nested + 16) = payload;
    memcpy(nested + 24, peer, payload);
    n = 22 + put_ev(img + 22, EV_PLUGIN_DATA, nested, payload + 24);
    *(uint32_t *)(img + 18) = (uint32_t)(n - 22);
    memset(img + n, 0x5a, 8192); n += 8192;
    d = (unsigned char *)malloc(n); assert(d); memcpy(d, img, n); dn = n;
    realloc_calls = 0; samples_rewrite(&d, &dn); calls = realloc_calls;
    assert(!g_compatFault && calls >= 4);
    assert(flp_data_bounds(d, dn, &start, &end) && dn - end == 8192 && !memcmp(d + end, img + n - 8192, 8192));
    assert(flp_walk(d, dn, collect_cb, &paths) && paths.n == 1 && !wcscmp(paths.paths[0], local));
    pathlist_free(&paths); free(d);
    for (at = 1; at <= calls; at++) {
        d = (unsigned char *)malloc(n); assert(d); memcpy(d, img, n); dn = n; original = d;
        g_compatFault = 0; g_compatReady = 1;
        realloc_calls = 0; fail_realloc_at = at;
        samples_rewrite(&d, &dn);
        fail_realloc_at = 0;
        assert(g_compatFault && d == original && dn == n && !memcmp(d, img, n));
        free(d);
    }
    g_compatFault = 0; g_compatReady = 1;
    rw_put(&growth, "ok", 2); assert(growth.o && growth.n == 2);
    original = growth.o;
    rw_put(&growth, "x", (size_t)-1);
    assert(growth.failed && growth.o == original && growth.n == 2 && !memcmp(growth.o, "ok", 2));
    free(growth.o);
    printf("PASS sample rewrite nested round trip, trailer preservation, %u allocation failures and overflow\n", calls);
    return 0;
}

static int test_alias_collision(const wchar_t *local, const wchar_t *peer) {
    wchar_t alias[MAX_PATH], copy[MAX_PATH];
    size_t length = wcslen(peer);
    FILE *f = NULL;
    assert(alias_path(local, length, alias, MAX_PATH));
    assert(same_sample_file(alias, local));
    assert(DeleteFileW(alias));
    assert(!_wfopen_s(&f, alias, L"wb") && f);
    assert(fwrite("NOPE", 1, 4, f) == 4); assert(!fclose(f));
    assert(!alias_path(local, length, copy, MAX_PATH));
    assert(!same_sample_file(alias, local));
    {
        unsigned char img[4096] = {0}, *d, *original;
        size_t n, dn;
        memcpy(img, "FLhd", 4); *(uint32_t *)(img + 4) = 6; memcpy(img + 14, "FLdt", 4);
        n = 22 + put_ev(img + 22, EV_PLUGIN_DATA, peer, (length + 1) * 2);
        *(uint32_t *)(img + 18) = (uint32_t)(n - 22);
        d = (unsigned char *)malloc(n); assert(d); memcpy(d, img, n); dn = n; original = d;
        samples_rewrite(&d, &dn);
        assert(g_compatFault && d == original && dn == n && !memcmp(d, img, n));
        assert(strstr(g_alert, "correct local file"));
        free(d); g_compatFault = 0; g_compatReady = 1;
    }
    assert(DeleteFileW(alias) && CopyFileW(local, alias, TRUE));
    assert(alias_path(local, length, copy, MAX_PATH));
    assert(same_sample_file(alias, local));
    path_mock = 1; profile_path = NULL;
    assert(!samples_dir(copy, MAX_PATH) && !copy[0]);
    profile_path = owned_profile;
    assert(!samples_dir(copy, 4) && !copy[0]);
    puts("PASS sample alias identity, equal copy, same-size collision stops before load, and invalid cache root");
    return 0;
}

static void test_sample_locations(void) {
    wchar_t out[MAX_PATH * 2], root[181], source[600];
    const char *hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    size_t i;
    path_mock = 1; profile_path = L"D:\\Redirected";
    assert(samples_dir(out, MAX_PATH) && !wcscmp(out, L"D:\\Redirected\\Documents\\flim\\samples"));
    assert(SetEnvironmentVariableW(L"FLIM_SAMPLE_DIR", L"E:\\音楽\\Shared"));
    assert(samples_dir(out, MAX_PATH) && !wcscmp(out, L"E:\\音楽\\Shared"));
    assert(!samples_dir(out, 4) && !out[0]);
    assert(cache_path(hex, L"C:\\peer\\CON.wav", out, MAX_PATH * 2) && wcsstr(out, L"\\_CON.wav"));
    assert(cache_path(hex, L"C:\\peer\\kick.wav. ", out, MAX_PATH * 2) && wcsstr(out, L"\\kick.wav"));
    assert(cache_path(hex, L"C:\\peer\\...", out, MAX_PATH * 2) && wcsstr(out, L"\\sample.wav"));
    assert(!cache_path("bad", L"kick.wav", out, MAX_PATH * 2) && !out[0]);
    assert(!cache_path(hex, L"kick.wav", out, 10) && !out[0]);
    wcscpy_s(root, 181, L"C:\\"); for (i = 3; i < 180; i++) root[i] = L'r'; root[180] = 0;
    assert(SetEnvironmentVariableW(L"FLIM_SAMPLE_DIR", root));
    wcscpy_s(source, 600, L"D:\\"); for (i = 3; i < 500; i++) source[i] = L'x'; wcscpy_s(source + 500, 100, L".wav");
    assert(cache_path(hex, source, out, MAX_PATH * 2) && wcslen(out) < MAX_PATH && !wcscmp(out + wcslen(out) - 4, L".wav"));
    source[59] = 0xd83c; source[60] = 0xdfb5;
    assert(cache_path(hex, source, out, MAX_PATH * 2));
    for (i = 0; out[i]; i++) {
        if (out[i] >= 0xd800 && out[i] <= 0xdbff) {assert(out[i + 1] >= 0xdc00 && out[i + 1] <= 0xdfff); i++;}
        else assert(out[i] < 0xdc00 || out[i] > 0xdfff);
    }
    wcscpy_s(source + 500, 100, L".w?av ");
    assert(cache_path(hex, source, out, MAX_PATH * 2) && !wcschr(out, L'?') && out[wcslen(out) - 1] != L' ');
    assert(SetEnvironmentVariableW(L"FLIM_SAMPLE_DIR", L"relative\\folder")); assert(!samples_dir(out, MAX_PATH) && !out[0]);
    assert(SetEnvironmentVariableW(L"FLIM_SAMPLE_DIR", L"\\\\?\\C:\\device")); assert(!samples_dir(out, MAX_PATH) && !out[0]);
    assert(SetEnvironmentVariableW(L"FLIM_SAMPLE_DIR", NULL));
    path_mock = 0; profile_path = NULL;
    puts("PASS redirected Documents, explicit Unicode folder, bounded cache paths, reserved names, extensions, and invalid override refusal");
}

int main(void) {
    static unsigned char plug[4096], img[8192];
    wchar_t wpeer[MAX_PATH], local[MAX_PATH], wa[MAX_PATH];
    char apeer[MAX_PATH];
    size_t pn = 0, n = 0, i, plugin_at;
    PathList L;
    int fails;
    assert(SetEnvironmentVariableW(L"FLIM_SAMPLE_DIR", NULL));
    fails = test_sample_macro_paths();
    test_sample_locations();
#ifdef FLIM_SAMPLE_PATH_TEST_ONLY
    return fails;
#endif
    if (!test_profile_create()) { printf("could not create owned temporary profile\n"); return fails + 1; }
    path_mock = 1; shared_path = NULL; profile_path = owned_profile;
    {
        wchar_t log_dir[MAX_PATH];
        size_t peer_length = wcslen(owned_profile) + wcslen(L"\\Documents\\flim\\samples") + 48;
        if (swprintf_s(log_dir, MAX_PATH, L"%s\\", owned_profile) < 0 ||
            !WideCharToMultiByte(CP_ACP, 0, log_dir, -1, test_temp_dir, MAX_PATH, NULL, NULL) ||
            !test_peer_path(wpeer, MAX_PATH, peer_length, L"kick 01.wav") ||
            !test_peer_path(wa, MAX_PATH, peer_length + 8, L"snare 02.wav") ||
            !WideCharToMultiByte(CP_ACP, 0, wa, -1, apeer, MAX_PATH, NULL, NULL)) {
            printf("temporary profile is too long for the native path fixture\n");
            if (!test_profile_remove(owned_profile)) printf("temporary profile cleanup failed\n");
            return fails + 1;
        }
    }
    InitializeCriticalSection(&g_sampleLock);
    InitializeCriticalSection(&g_sendLock);
    InitializeCriticalSection(&g_netTextLock);
    // plugin data in some unknown format: junk, a UTF-16 path + NUL, junk, an ANSI path + NUL, junk
    memcpy(plug, "\x01\x02\x03\x04\x05\x06\x07", 7); pn = 7;
    memcpy(plug + pn, wpeer, wcslen(wpeer) * 2 + 2); pn += wcslen(wpeer) * 2 + 2;
    memcpy(plug + pn, "\x10\x20\x30", 3); pn += 3;
    memcpy(plug + pn, apeer, strlen(apeer) + 1); pn += strlen(apeer) + 1;
    memcpy(plug + pn, "\xaa\xbb", 2); pn += 2;
    // FL26's extended creator ID must survive rewriting before plugin data.
    memcpy(img, "FLhd", 4); *(uint32_t *)(img + 4) = 6; memset(img + 8, 0, 6); n = 14;
    memcpy(img + n, "FLdt", 4); n += 8;
    memcpy(img + n, "\xac\x01\x01\x00\xc0\x04\x46\x00\x4c\x00", 10); n += 10;
    plugin_at = n;
    n += put_ev(img + n, EV_PLUGIN_DATA, plug, pn);
    *(uint32_t *)(img + 18) = (uint32_t)(n - 22);

    memset(&L, 0, sizeof(L));
    flp_walk(img, n, collect_cb, &L);
    printf("collected %d paths\n", L.n);
    for (i = 0; i < (size_t)L.n; i++) printf("  %S\n", L.paths[i]);
    if (L.n != 2) fails++;
    pathlist_free(&L);

    // the receiver has the files at these local places
    {   // a stand-in for the downloaded sample
        FILE *f = NULL;
        int wrote, closed;
        swprintf_s(local, MAX_PATH, L"%s\\kick.wav", owned_profile);
        if (_wfopen_s(&f, local, L"wb") || !f) { printf("fixture file creation failed\n"); fails++; goto cleanup; }
        wrote = fwrite("RIFF", 1, 4, f) == 4;
        closed = fclose(f) == 0;
        if (!wrote || !closed) { printf("fixture file write failed\n"); fails++; goto cleanup; }
    }
    if (!MultiByteToWideChar(CP_ACP, 0, apeer, -1, wa, MAX_PATH)) { fails++; goto cleanup; }
    map_set(wpeer, local);
    map_set(wa, local);
    {
        unsigned char *d = (unsigned char *)malloc(n);
        size_t dn = n;
        if (!d) { fails++; goto cleanup; }
        memcpy(d, img, n);
        samples_rewrite(&d, &dn);
        printf("image %zu -> %zu bytes\n", n, dn);
        if (dn != n) fails++;
        memset(&L, 0, sizeof(L));
        flp_walk(d, dn, collect_cb, &L);
        for (i = 0; i < (size_t)L.n; i++) {
            printf("  now %S (exists %d)\n", L.paths[i], file_exists_w(L.paths[i]));
            if (!file_exists_w(L.paths[i]) || wcsstr(L.paths[i], L"Alex")) fails++;
        }
        if (memcmp(d + plugin_at + 3, "\x01\x02\x03\x04\x05\x06\x07", 7) != 0) { printf("junk before the path changed\n"); fails++; }
        if (memcmp(d + 22, img + 22, 10)) { printf("extended event changed\n"); fails++; }
        pathlist_free(&L);
        free(d);
    }
    {
        unsigned char *d;
        size_t dn, start, end;
        /* An ordinary Sampler path after an extended ID may change size. */
        n = 32;
        n += put_ev(img + n, EV_SAMPLE_PATH, wpeer, (wcslen(wpeer) + 1) * 2);
        *(uint32_t *)(img + 18) = (uint32_t)(n - 22);
        memcpy(img + n, "TAIL", 4); n += 4;
        d = (unsigned char *)malloc(n); dn = n;
        if (!d) { fails++; goto cleanup; }
        memcpy(d, img, n);
        samples_rewrite(&d, &dn);
        if (!flp_data_bounds(d, dn, &start, &end) || end != dn - 4 || memcmp(d + end, "TAIL", 4)) fails++;
        if (memcmp(d + 22, img + 22, 10)) fails++;
        memset(&L, 0, sizeof(L));
        if (!flp_walk(d, dn, collect_cb, &L) || L.n != 1 || wcscmp(L.paths[0], local)) fails++;
        pathlist_free(&L);
        free(d);
        /* A truncated later event must leave the caller's input unchanged. */
        d = (unsigned char *)malloc(n); dn = n;
        if (!d) { fails++; goto cleanup; }
        memcpy(d, img, n);
        *(uint32_t *)(d + 18) += 1; d[n - 4] = 0xc4;
        { unsigned char *original = d; samples_rewrite(&d, &dn); if (d != original || dn != n) fails++; }
        free(d);
    }
    fails += test_rewrite_failures(wpeer, local);
    fails += test_alias_collision(local, wpeer);
cleanup:
    for (i = 0; i < (size_t)g_npmap; i++) { free(g_pmap[i].from); free(g_pmap[i].to); }
    free(g_pmap); g_pmap = NULL; g_npmap = g_cappmap = 0;
    DeleteCriticalSection(&g_sampleLock);
    DeleteCriticalSection(&g_sendLock);
    DeleteCriticalSection(&g_netTextLock);
    path_mock = 0; profile_path = NULL;
    if (!test_profile_remove(owned_profile)) { printf("temporary profile cleanup failed\n"); fails++; }
    else printf("owned temporary profile and aliases removed\n");
    printf(fails ? "FAIL (%d)\n" : "OK\n", fails);
    return fails;
}
