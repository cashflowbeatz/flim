#include <assert.h>
#include <windows.h>

static BOOL WINAPI startup_enum(WNDENUMPROC callback, LPARAM data);
static DWORD WINAPI startup_thread(HWND window, LPDWORD pid);
static int WINAPI startup_class(HWND window, LPSTR name, int capacity);
static HHOOK WINAPI startup_hook(int kind, HOOKPROC callback, HINSTANCE module, DWORD thread);
static BOOL WINAPI startup_unhook(HHOOK hook);
static BOOL WINAPI startup_post(HWND window, UINT message, WPARAM wp, LPARAM lp);
static void WINAPI startup_sleep(DWORD milliseconds);

#define EnumWindows startup_enum
#define GetWindowThreadProcessId startup_thread
#define GetClassNameA startup_class
#define SetWindowsHookExW startup_hook
#define UnhookWindowsHookEx startup_unhook
#define PostMessageW startup_post
#define Sleep startup_sleep
#include "../../engine/flim_engine.c"
#undef EnumWindows
#undef GetWindowThreadProcessId
#undef GetClassNameA
#undef SetWindowsHookExW
#undef UnhookWindowsHookEx
#undef PostMessageW
#undef Sleep

static unsigned scans, sleeps, hooks, unhooks, posts, formQueries;
static int missing, vanished, failMessages, failCalls;
static HWND form = (HWND)(UINT_PTR)3;

static BOOL WINAPI startup_enum(WNDENUMPROC callback, LPARAM data) {
    scans++;
    assert(callback((HWND)(UINT_PTR)1, data)); /* another process's main form */
    assert(callback((HWND)(UINT_PTR)2, data)); /* our early TApplication window */
    if (!missing) assert(!callback(form, data));
    return TRUE;
}

static DWORD WINAPI startup_thread(HWND window, LPDWORD pid) {
    DWORD owner = GetCurrentProcessId() + (window == (HWND)(UINT_PTR)1);
    if (vanished && window == form && ++formQueries > 1) owner++;
    if (pid) *pid = owner;
    return 41;
}

static int WINAPI startup_class(HWND window, LPSTR name, int capacity) {
    const char *value = window == (HWND)(UINT_PTR)2 ? "TApplication" : "TFruityLoopsMainForm";
    strcpy_s(name, capacity, value);
    return (int)strlen(value);
}

static HHOOK WINAPI startup_hook(int kind, HOOKPROC callback, HINSTANCE module, DWORD thread) {
    hooks++;
    assert(module == NULL && thread == 41);
    assert((kind == WH_GETMESSAGE && callback == getMsgHook) || (kind == WH_CALLWNDPROC && callback == callWndHook));
    if ((kind == WH_GETMESSAGE && failMessages) || (kind == WH_CALLWNDPROC && failCalls)) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return NULL;
    }
    return (HHOOK)(UINT_PTR)(kind == WH_GETMESSAGE ? 10 : 11);
}

static BOOL WINAPI startup_unhook(HHOOK hook) {
    assert(hook == (HHOOK)(UINT_PTR)10 || hook == (HHOOK)(UINT_PTR)11);
    unhooks++; return TRUE;
}

static BOOL WINAPI startup_post(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    assert(window == form && message == WM_NULL && !wp && !lp);
    posts++; return TRUE;
}

static void WINAPI startup_sleep(DWORD milliseconds) { assert(milliseconds == 100); sleeps++; }

static void reset(void) {
    scans = sleeps = hooks = unhooks = posts = formQueries = 0;
    missing = vanished = failMessages = failCalls = 0;
    g_hook = g_hookCwp = NULL; g_mainWnd = g_mainForm = NULL;
    g_hinst = (HINSTANCE)(UINT_PTR)0xdead;
}

int main(void) {
    reset();
    assert(install_mainthread_hook() == 0);
    assert(scans == 1 && sleeps == 0 && hooks == 2 && !unhooks && posts == 1);
    assert(g_mainWnd == form && g_mainForm == form && g_hook && g_hookCwp);

    reset(); failMessages = 1;
    assert(install_mainthread_hook() == -1 && hooks == 2 && unhooks == 1 && !posts);
    assert(!g_hook && !g_hookCwp && !g_mainWnd);

    reset(); failCalls = 1;
    assert(install_mainthread_hook() == -1 && hooks == 2 && unhooks == 1 && !posts);
    assert(!g_hook && !g_hookCwp && !g_mainWnd);

    reset(); failMessages = failCalls = 1;
    assert(install_mainthread_hook() == -1 && hooks == 2 && !unhooks && !posts);

    reset(); missing = 1;
    assert(install_mainthread_hook() == -1 && scans == 600 && sleeps == 600 && !hooks && !posts);

    reset(); vanished = 1;
    assert(install_mainthread_hook() == -1 && !hooks && !posts);
    InitializeCriticalSection(&g_netTextLock);
    assert(parse_host("https://relay", &g_netUrl));
    g_netBar = form;
    strcpy_s(g_session, sizeof(g_session), "12345678");
    strcpy_s(g_hostPw, sizeof(g_hostPw), "test-invite");
    hosted_ready();
    assert(!posts && !g_hostPw[0] && strstr(g_hostedText, "12345678"));
    strcpy_s(g_session, sizeof(g_session), "03");
    hosted_ready();
    assert(!posts && strstr(g_hostedText, "03"));
    DeleteCriticalSection(&g_netTextLock);
    puts("Main-form discovery, same-process hooks, failure cleanup, bounded startup and nonblocking hosting: OK");
    return 0;
}
