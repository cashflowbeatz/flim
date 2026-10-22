// Offline test of the plugin-data sample path code (collect + same-length rewrite).
// build.bat, then t_samples.exe: prints OK. Leaves small aliases under Documents\flim\samples.
#include <windows.h>
#include <wchar.h>
#include <assert.h>

static int path_mock;
static const wchar_t *shared_path, *profile_path;
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
#define RegGetValueW sample_test_registry
#define GetEnvironmentVariableW sample_test_environment
#include "../../engine/flim_engine.c"
#undef RegGetValueW
#undef GetEnvironmentVariableW

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

int main(void) {
    static unsigned char plug[4096], img[8192];
    const wchar_t *wpeer = L"D:\\Alex\\Sample Packs\\Very Long Folder Name For Drums\\kick 01.wav";
    const char *apeer = "D:\\Alex\\Sample Packs\\Another Long Folder Name\\snare 02.wav";
    wchar_t local[MAX_PATH], wa[MAX_PATH];
    size_t pn = 0, n = 0, i, plugin_at;
    PathList L;
    int fails = test_sample_macro_paths();
#ifdef FLIM_SAMPLE_PATH_TEST_ONLY
    return fails;
#endif
    InitializeCriticalSection(&g_sampleLock);
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

    // the receiver has the files at these local places
    {   // a stand-in for the downloaded sample
        FILE *f = NULL;
        GetTempPathW(MAX_PATH, local);
        wcscat_s(local, MAX_PATH, L"flim_harness_kick.wav");
        if (_wfopen_s(&f, local, L"wb") == 0 && f) { fputs("RIFF", f); fclose(f); }
    }
    MultiByteToWideChar(CP_ACP, 0, apeer, -1, wa, MAX_PATH);
    map_set(wpeer, local);
    map_set(wa, local);
    {
        unsigned char *d = (unsigned char *)malloc(n);
        size_t dn = n;
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
        d = (unsigned char *)malloc(n); dn = n; memcpy(d, img, n);
        samples_rewrite(&d, &dn);
        if (!flp_data_bounds(d, dn, &start, &end) || end != dn - 4 || memcmp(d + end, "TAIL", 4)) fails++;
        if (memcmp(d + 22, img + 22, 10)) fails++;
        memset(&L, 0, sizeof(L));
        if (!flp_walk(d, dn, collect_cb, &L) || L.n != 1 || wcscmp(L.paths[0], local)) fails++;
        free(d);
        /* A truncated later event must leave the caller's input unchanged. */
        d = (unsigned char *)malloc(n); dn = n; memcpy(d, img, n);
        *(uint32_t *)(d + 18) += 1; d[n - 4] = 0xc4;
        { unsigned char *original = d; samples_rewrite(&d, &dn); if (d != original || dn != n) fails++; }
        free(d);
    }
    printf(fails ? "FAIL (%d)\n" : "OK\n", fails);
    return fails;
}
