#include <assert.h>
#include <windows.h>
static INT_PTR WINAPI toolbar_dialog(HINSTANCE, LPCWSTR, HWND, DLGPROC, LPARAM);
static int WINAPI toolbar_message(HWND, LPCWSTR, LPCWSTR, UINT);
#define DialogBoxParamW toolbar_dialog
#define MessageBoxW toolbar_message
#include "../../engine/flim_engine.c"
#undef DialogBoxParamW
#undef MessageBoxW

static unsigned dialogs;
static INT_PTR WINAPI toolbar_dialog(HINSTANCE a, LPCWSTR b, HWND c, DLGPROC d, LPARAM e) {
    (void)a; (void)b; (void)c; (void)d; (void)e; dialogs++; return IDCANCEL;
}
static int WINAPI toolbar_message(HWND a, LPCWSTR b, LPCWSTR c, UINT d) {
    (void)a; (void)b; (void)c; (void)d; dialogs++; return IDCANCEL;
}

int main(void) {
    RECT bar, row; int state, x;
    wchar_t text[256];
    InitializeCriticalSection(&g_netTextLock);
    g_netRow = CreateWindowExW(0, L"STATIC", L"Owned toolbar test", WS_POPUP, 0, 0, 1000, 41, NULL, NULL, GetModuleHandleW(NULL), NULL);
    assert(g_netRow);
    g_netBar = CreateWindowExW(0, L"STATIC", L"flim", WS_CHILD, 0, 0, 1, 26, g_netRow, NULL, GetModuleHandleW(NULL), NULL);
    assert(g_netBar);
    bar_layout(); assert(g_barShowVersion);
    for (state = NET_OFF; state <= NET_ERR; state++) {
        g_netState = state;
        for (x = 0; x < 500; x += 10) {
            bar_proc(g_netBar, WM_LBUTTONDOWN, 0, MAKELPARAM(x, 10));
            bar_proc(g_netBar, WM_LBUTTONUP, 0, MAKELPARAM(x, 10));
        }
        bar_proc(g_netBar, WM_FLIM_HOSTED, 0, 0);
        assert(!dialogs && g_netState == state);
    }
    wcscpy_s(g_netText, 256, L"Connected: two people");
    bar_proc(g_netBar, WM_FLIM_STATUS, 0, 0);
    assert(GetWindowTextW(g_netBar, text, 256) && !wcscmp(text, g_netText));
    SetWindowPos(g_netRow, NULL, 0, 0, 250, 41, SWP_NOACTIVATE | SWP_NOZORDER);
    bar_layout(); GetWindowRect(g_netBar, &bar); GetWindowRect(g_netRow, &row);
    assert(!g_barShowVersion && bar.left >= row.left && bar.right <= row.right);
    SetWindowPos(g_netRow, NULL, 0, 0, 60, 41, SWP_NOACTIVATE | SWP_NOZORDER);
    bar_layout(); assert(!(GetWindowLongPtrW(g_netBar, GWL_STYLE) & WS_VISIBLE));
    SetWindowPos(g_netRow, NULL, 0, 0, 1000, 41, SWP_NOACTIVATE | SWP_NOZORDER);
    bar_layout(); assert(g_barShowVersion && (GetWindowLongPtrW(g_netBar, GWL_STYLE) & WS_VISIBLE));
    DestroyWindow(g_netRow); g_netBar = g_netRow = NULL;
    DeleteCriticalSection(&g_netTextLock);
    puts("Status-only toolbar: no connection dialogs, accessible text, compact layout, narrow-row hiding and recovery: OK");
    return 0;
}
