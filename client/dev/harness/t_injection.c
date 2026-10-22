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
static BOOL WINAPI test_close(HANDLE h) { assert(h); closed++; return TRUE; }

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
#include "../../launcher/flim.c"

static void reset(void) {
    memset(&test_version, 0, sizeof(test_version));
    test_version.dwSignature = 0xFEEF04BD;
    test_version.dwFileVersionMS = (25u << 16) | 2u;
    test_version.dwFileVersionLS = (3u << 16) | 5171u;
    allocations = remote_threads = resumed = terminated = released = closed = 0;
    alloc_fails = write_fails = thread_fails = resume_fails = 0;
    long_module_path = 0;
    wait_result = WAIT_OBJECT_0; load_result = 1; creation_flags = 0;
}

int main(void) {
    reset();
    assert(launch(L"C:\\FL") == 0);
    assert(creation_flags == CREATE_SUSPENDED && allocations == 1 && remote_threads == 1);
    assert(resumed == 1 && !terminated && released == 1 && closed == 3);

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
    test_version.dwFileVersionMS = (26u << 16) | 1u;
    test_version.dwFileVersionLS = (6u << 16) | 5639u;
    assert(launch(L"C:\\FL") == 0);
    assert(!creation_flags && !allocations && !remote_threads && !resumed && !terminated && closed == 2);
    puts("Launcher process handling: OK (9 scenarios, mocked Win32 APIs)");
    return 0;
}
