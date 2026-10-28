// Exercise launcher control flow without creating a process or loading a DLL.
#include <windows.h>
#include <shobjidl.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static VS_FIXEDFILEINFO test_version;
static int allocations, remote_threads, resumed, terminated, released, closed;
static int alloc_fails, write_fails, thread_fails, resume_fails;
static int long_module_path;
static int profile_result;
static int profile_hash_ok, profile_hash_calls;
static wchar_t info_line[320];
static DWORD wait_result, load_result, creation_flags;

static DWORD WINAPI test_version_size(LPCWSTR p, LPDWORD h) { (void)p; *h = 0; return sizeof(test_version); }
static BOOL WINAPI test_version_info(LPCWSTR p, DWORD h, DWORD n, LPVOID out) {
    (void)p; (void)h; (void)n; memcpy(out, &test_version, sizeof(test_version)); return TRUE;
}
static BOOL WINAPI test_version_query(LPCVOID p, LPCWSTR key, LPVOID *out, PUINT n) {
    (void)key; *out = (LPVOID)p; *n = sizeof(test_version); return TRUE;
}
static DWORD WINAPI test_attributes(LPCWSTR p) { (void)p; return FILE_ATTRIBUTE_DIRECTORY; }
static DWORD WINAPI test_module_path(HMODULE m, LPWSTR out, DWORD cap) {
    (void)m;
    if (long_module_path) {
        DWORD i;
        for (i = 0; i < cap - 9; i++) out[i] = L'a';
        out[0] = L'C'; out[1] = L':'; out[2] = L'\\'; out[cap - 10] = L'\\';
        wcscpy_s(out + cap - 9, 9, L"flim.exe");
    } else wcscpy_s(out, cap, L"C:\\flim\\flim.exe");
    return (DWORD)wcslen(out);
}
static int WINAPI test_message(HWND h, LPCWSTR text, LPCWSTR title, UINT flags) {
    (void)h; (void)text; (void)title; (void)flags; return IDYES;
}
static BOOL WINAPI test_create_process(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
    LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env, LPCWSTR dir,
    LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    (void)app; (void)cmd; (void)pa; (void)ta; (void)inherit; (void)env; (void)dir; (void)si;
    creation_flags = flags;
    pi->hProcess = (HANDLE)1; pi->hThread = (HANDLE)2; pi->dwProcessId = 123;
    return TRUE;
}
static LPVOID WINAPI test_alloc(HANDLE h, LPVOID p, SIZE_T n, DWORD flags, DWORD prot) {
    (void)h; (void)p; (void)n; (void)flags; (void)prot; allocations++;
    return alloc_fails ? NULL : (LPVOID)4;
}
static BOOL WINAPI test_write(HANDLE h, LPVOID p, LPCVOID buf, SIZE_T n, SIZE_T *written) {
    (void)h; (void)p; (void)buf; *written = write_fails ? 0 : n; return !write_fails;
}
static HANDLE WINAPI test_remote_thread(HANDLE h, LPSECURITY_ATTRIBUTES sa, SIZE_T stack,
    LPTHREAD_START_ROUTINE start, LPVOID arg, DWORD flags, LPDWORD id) {
    (void)h; (void)sa; (void)stack; (void)start; (void)arg; (void)flags; (void)id;
    remote_threads++; return thread_fails ? NULL : (HANDLE)3;
}
static DWORD WINAPI test_wait(HANDLE h, DWORD ms) { (void)h; assert(ms == 15000); return wait_result; }
static BOOL WINAPI test_exit_code(HANDLE h, LPDWORD code) { (void)h; *code = load_result; return TRUE; }
static DWORD WINAPI test_resume(HANDLE h) { (void)h; resumed++; return resume_fails ? (DWORD)-1 : 1; }
static BOOL WINAPI test_free(HANDLE h, LPVOID p, SIZE_T n, DWORD flags) {
    (void)h; (void)p; (void)n; (void)flags; assert(wait_result == WAIT_OBJECT_0); released++; return TRUE;
}
static BOOL WINAPI test_terminate(HANDLE h, UINT code) { (void)h; (void)code; terminated++; return TRUE; }
static BOOL WINAPI test_close(HANDLE h) { assert(h); if (h != (HANDLE)5) closed++; return TRUE; }
static HANDLE WINAPI test_open_engine(LPCWSTR file, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD attrs, HANDLE templateFile) {
    (void)file; (void)security; (void)attrs; (void)templateFile;
    assert(access == GENERIC_READ && share == FILE_SHARE_READ && creation == OPEN_EXISTING);
    return (HANDLE)5;
}
static UINT WINAPI test_dialog_text(HWND h, int id, LPWSTR out, int cap) {
    (void)h; (void)id; wcscpy_s(out, cap, L"C:\\FL"); return 5;
}
static BOOL WINAPI test_set_dialog_text(HWND h, int id, LPCWSTR text) {
    (void)h; (void)id; wcscpy_s(info_line, 320, text); return TRUE;
}

#define GetFileVersionInfoSizeW test_version_size
#define GetFileVersionInfoW test_version_info
#define VerQueryValueW test_version_query
#define GetFileAttributesW test_attributes
#define GetModuleFileNameW test_module_path
#define MessageBoxW test_message
#define CreateProcessW test_create_process
#define VirtualAllocEx test_alloc
#define WriteProcessMemory test_write
#define CreateRemoteThread test_remote_thread
#define WaitForSingleObject test_wait
#define GetExitCodeThread test_exit_code
#define ResumeThread test_resume
#define VirtualFreeEx test_free
#define TerminateProcess test_terminate
#define CloseHandle test_close
#define GetDlgItemTextW test_dialog_text
#define SetDlgItemTextW test_set_dialog_text
#include "../../common/flim_profile.h"
static int test_profile_hash(HANDLE file, char hash[65]) {
    assert(file == (HANDLE)5); profile_hash_calls++;
    strcpy_s(hash, 65, profile_hash_ok ? "5e5d083bd0f0fcbc8d948570cf4c27c7e9349e6851478c7a9c05a11509ab8f32"
                                    : "0000000000000000000000000000000000000000000000000000000000000000");
    return 1;
}
static int test_profile_load(HMODULE client, const wchar_t *engine, const wchar_t *version,
                            FlBuild *out, wchar_t build[32], wchar_t *reason, size_t reasonCap) {
    (void)client; (void)engine; (void)version; (void)out; (void)build;
    reason[0] = 0;
    if (profile_result < 0) wcscpy_s(reason, reasonCap, L"Fixture profile invalid");
    return profile_result;
}
#define flim_profile_load test_profile_load
#define flim_profile_sha256 test_profile_hash
#define CreateFileW test_open_engine
#include "../../launcher/flim.c"

static void reset(void) {
    memset(&test_version, 0, sizeof(test_version));
    test_version.dwSignature = 0xFEEF04BD;
    test_version.dwFileVersionMS = (25u << 16) | 2u;
    test_version.dwFileVersionLS = (3u << 16) | 5171u;
    allocations = remote_threads = resumed = terminated = released = closed = 0;
    alloc_fails = write_fails = thread_fails = resume_fails = 0;
    long_module_path = 0;
    profile_result = 0; profile_hash_ok = 1; profile_hash_calls = 0; info_line[0] = 0;
    g_machineOutput = 0; g_allowExperimental = 0; g_launchedPid = 0;
    wait_result = WAIT_OBJECT_0; load_result = 1; creation_flags = 0;
}

int main(void) {
    size_t i;
    for (i = 0; i < sizeof(FLIM_FL_BUILDS) / sizeof(FLIM_FL_BUILDS[0]); i++) {
        unsigned int a, b, c, d;
        reset();
        assert(swscanf_s(FLIM_FL_BUILDS[i], L"%u.%u.%u.%u", &a, &b, &c, &d) == 4);
        test_version.dwFileVersionMS = (a << 16) | b;
        test_version.dwFileVersionLS = (c << 16) | d;
        assert(launch(L"C:\\FL") == 0);
        assert(creation_flags == CREATE_SUSPENDED && allocations == 1 && remote_threads == 1);
        assert(resumed == 1 && !terminated && released == 1 && closed == 3);
        assert(g_launchedPid == 123);
    }

    reset(); long_module_path = 1;
    assert(launch(L"C:\\FL") == 1);
    assert(!creation_flags && !allocations && !remote_threads && !closed);

    reset(); wait_result = WAIT_TIMEOUT;
    assert(launch(L"C:\\FL") == 1);
    assert(!resumed && terminated == 1 && !released && closed == 3);

    reset(); load_result = 0;
    assert(launch(L"C:\\FL") == 1);
    assert(!resumed && terminated == 1 && !released && closed == 3);

    reset(); alloc_fails = 1;
    assert(launch(L"C:\\FL") == 1);
    assert(!remote_threads && !resumed && terminated == 1 && closed == 2);

    reset(); write_fails = 1;
    assert(launch(L"C:\\FL") == 1);
    assert(!remote_threads && !resumed && terminated == 1 && closed == 2);

    reset(); thread_fails = 1;
    assert(launch(L"C:\\FL") == 1);
    assert(!resumed && terminated == 1 && !released && closed == 2);

    reset(); resume_fails = 1;
    assert(launch(L"C:\\FL") == 1);
    assert(resumed == 1 && terminated == 1 && released == 1 && closed == 3);

    reset();
    test_version.dwFileVersionMS = (26u << 16) | 99u;
    test_version.dwFileVersionLS = (99u << 16) | 65535u;
    assert(launch(L"C:\\FL") == 0);
    assert(!creation_flags && !allocations && !remote_threads && !resumed && !terminated && closed == 2);

    reset(); g_machineOutput = 1;
    test_version.dwFileVersionMS = (26u << 16) | 99u;
    test_version.dwFileVersionLS = (99u << 16) | 65535u;
    assert(launch(L"C:\\FL") == 1);
    assert(!creation_flags && !allocations && !remote_threads && !resumed && !terminated && !closed && !g_launchedPid);

    reset(); g_machineOutput = 1;
    test_version.dwFileVersionMS = (24u << 16) | 2u;
    test_version.dwFileVersionLS = (2u << 16) | 4597u;
#ifdef FLIM_EXPERIMENTAL_FL26
    assert(launch(L"C:\\FL") == 0);
    assert(creation_flags == CREATE_SUSPENDED && resumed == 1 && !terminated && profile_hash_calls == 1);
#else
    assert(launch(L"C:\\FL") == 1);
    assert(!creation_flags && !allocations && !remote_threads && !closed);
#endif
    reset(); g_machineOutput = 1; g_allowExperimental = 1;
    test_version.dwFileVersionMS = (24u << 16) | 2u;
    test_version.dwFileVersionLS = (2u << 16) | 4597u;
#ifdef FLIM_EXPERIMENTAL_FL26
    assert(launch(L"C:\\FL") == 0);
    assert(creation_flags == CREATE_SUSPENDED && resumed == 1 && !terminated && profile_hash_calls == 1);
#else
    assert(launch(L"C:\\FL") == 1);
    assert(!creation_flags && !allocations && !remote_threads && !closed && !profile_hash_calls);
#endif
    reset(); g_machineOutput = 1; profile_hash_ok = 0;
    test_version.dwFileVersionMS = (24u << 16) | 2u;
    test_version.dwFileVersionLS = (2u << 16) | 4597u;
    assert(launch(L"C:\\FL") == 1);
    assert(!creation_flags && !allocations && !remote_threads && !closed);
    reset(); g_machineOutput = 1;
    test_version.dwFileVersionMS = (24u << 16) | 2u;
    test_version.dwFileVersionLS = (2u << 16) | 4598u;
    assert(launch(L"C:\\FL") == 1);
    assert(!creation_flags && !allocations && !remote_threads && !closed && !profile_hash_calls);
    reset(); g_machineOutput = 1; profile_result = 1;
    test_version.dwFileVersionMS = (24u << 16) | 2u;
    test_version.dwFileVersionLS = (2u << 16) | 4597u;
    assert(launch(L"C:\\FL") == 0);
    assert(creation_flags == CREATE_SUSPENDED && resumed == 1 && !terminated);

    reset(); profile_result = -1;
    show_fl_info(NULL);
    assert(wcsstr(info_line, L"Fixture profile invalid"));
    assert(launch(L"C:\\FL") == 1);
    assert(!creation_flags && !allocations && !remote_threads && !closed);

    reset(); profile_result = 1;
    test_version.dwFileVersionMS = (25u << 16) | 9u;
    test_version.dwFileVersionLS = (9u << 16) | 65000u;
    show_fl_info(NULL);
    assert(wcsstr(info_line, L"25.9.9.65000 supported."));
    assert(launch(L"C:\\FL") == 0);
    assert(creation_flags == CREATE_SUSPENDED && resumed == 1 && !terminated);

    reset();
    test_version.dwFileVersionMS = (25u << 16) | 9u;
    show_fl_info(NULL);
    assert(wcsstr(info_line, L"support is not verified"));
    printf("Launcher process handling: OK (%zu known builds + 8 failure/fallback scenarios + external-profile status/launch checks, mocked Win32 APIs)\n", i);
    return 0;
}
