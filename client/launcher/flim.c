// flim.exe launcher: start FL suspended, inject flim_engine.dll via classic
// LoadLibraryW remote-thread injection, then resume. On-disk FL is untouched.
// First run (or FL not found, or `flim.exe --setup`): a setup dialog asks for the
// FL Studio folder and a user name; both are saved in %APPDATA%\flim\flim.ini.

#include <windows.h>
#include <shobjidl.h>
#include <stdio.h>
#include "../common/flim_config.h"
#include "../common/flim_res.h"
#pragma comment(lib, "version.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static void fail_box(HWND owner, const wchar_t *fmt, ...) {
    wchar_t msg[1024];
    va_list ap; va_start(ap, fmt); vswprintf_s(msg, 1024, fmt, ap); va_end(ap);
    MessageBoxW(owner, msg, L"flim", MB_OK | MB_ICONERROR);
}

// File version a.b.c.d of an exe/dll; 0 if it has none.
static int file_version(const wchar_t *file, WORD v[4]) {
    DWORD h = 0, n = GetFileVersionInfoSizeW(file, &h);
    VS_FIXEDFILEINFO *fi = NULL;
    UINT fl = 0;
    int ok = 0;
    void *buf;
    if (!n) return 0;
    buf = malloc(n);
    if (!buf) return 0;
    if (GetFileVersionInfoW(file, 0, n, buf) && VerQueryValueW(buf, L"\\", (void **)&fi, &fl) &&
        fi && fl >= sizeof(*fi) && fi->dwSignature == 0xFEEF04BD) {
        v[0] = HIWORD(fi->dwFileVersionMS); v[1] = LOWORD(fi->dwFileVersionMS);
        v[2] = HIWORD(fi->dwFileVersionLS); v[3] = LOWORD(fi->dwFileVersionLS);
        ok = 1;
    }
    free(buf);
    return ok;
}

// Accepts the install folder or the FL64.exe path itself; normalizes `dir` to the folder.
// A valid install can still be unsupported; only known_build permits injection.
static int check_fl_dir(wchar_t *dir, wchar_t *why, int whycap, wchar_t *ver, int vercap) {
    wchar_t f[MAX_PATH * 2];
    WORD v[4];
    size_t n;
    while ((n = wcslen(dir)) > 0 && (dir[n - 1] == L' ' || dir[n - 1] == L'"')) dir[n - 1] = 0;
    while (dir[0] == L' ' || dir[0] == L'"') memmove(dir, dir + 1, wcslen(dir) * sizeof(wchar_t));
    n = wcslen(dir);
    if (n > 4 && _wcsicmp(dir + n - 4, L".exe") == 0) { wchar_t *s = wcsrchr(dir, L'\\'); if (s) *s = 0; }
    while ((n = wcslen(dir)) > 3 && dir[n - 1] == L'\\') dir[n - 1] = 0;
    ver[0] = 0;
    if (!dir[0]) { swprintf_s(why, whycap, L"Pick the FL Studio folder."); return 0; }
    {
        DWORD a = GetFileAttributesW(dir);
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
            swprintf_s(why, whycap, L"That folder doesn't exist."); return 0;
        }
    }
    swprintf_s(f, MAX_PATH * 2, L"%s\\FL64.exe", dir);
    if (GetFileAttributesW(f) == INVALID_FILE_ATTRIBUTES) { swprintf_s(why, whycap, L"No FL64.exe in that folder."); return 0; }
    if (!file_version(f, v)) { swprintf_s(why, whycap, L"FL64.exe has no version info - not FL Studio?"); return 0; }
    swprintf_s(ver, vercap, L"%u.%u.%u.%u", v[0], v[1], v[2], v[3]);
    swprintf_s(f, MAX_PATH * 2, L"%s\\FLEngine_x64.dll", dir);
    if (GetFileAttributesW(f) == INVALID_FILE_ATTRIBUTES) { swprintf_s(why, whycap, L"No FLEngine_x64.dll in that folder."); return 0; }
    if (!file_version(f, v)) { swprintf_s(why, whycap, L"FLEngine_x64.dll has no valid version info."); return 0; }
    swprintf_s(ver, vercap, L"%u.%u.%u.%u", v[0], v[1], v[2], v[3]);
    why[0] = 0;
    return 1;
}

static int known_build(const wchar_t *ver) {
    size_t i;
    for (i = 0; i < sizeof(FLIM_FL_BUILDS) / sizeof(FLIM_FL_BUILDS[0]); i++)
        if (wcscmp(ver, FLIM_FL_BUILDS[i]) == 0) return 1;
    return 0;
}

static void discover_fl_dir(wchar_t *out, int cap) {
    static const wchar_t *roots[] = { L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)" };
    WORD best[4] = { 0 };
    int bestKnown = -1;
    size_t i;
    out[0] = 0;
    for (i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        wchar_t root[MAX_PATH], mask[MAX_PATH * 2];
        WIN32_FIND_DATAW data;
        HANDLE search;
        DWORD n = GetEnvironmentVariableW(roots[i], root, MAX_PATH);
        if (!n || n >= MAX_PATH) continue;
        swprintf_s(mask, MAX_PATH * 2, L"%s\\Image-Line\\FL Studio*", root);
        search = FindFirstFileW(mask, &data);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            wchar_t dir[MAX_PATH * 2], engine[MAX_PATH * 2], why[256], ver[64];
            WORD version[4];
            int known, newer = 0, j;
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            swprintf_s(dir, MAX_PATH * 2, L"%s\\Image-Line\\%s", root, data.cFileName);
            if (!check_fl_dir(dir, why, 256, ver, 64)) continue;
            swprintf_s(engine, MAX_PATH * 2, L"%s\\FLEngine_x64.dll", dir);
            if (!file_version(engine, version)) continue;
            known = known_build(ver);
            for (j = 0; j < 4; j++) {
                if (version[j] != best[j]) { newer = version[j] > best[j]; break; }
            }
            if (known > bestKnown || (known == bestKnown && newer)) {
                wcscpy_s(out, cap, dir);
                memcpy(best, version, sizeof(best));
                bestKnown = known;
            }
        } while (FindNextFileW(search, &data));
        FindClose(search);
    }
}

static void browse_folder(HWND dlg) {
    IFileOpenDialog *fd = NULL;
    IShellItem *it = NULL;
    wchar_t *path = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&fd)))
        return;
    fd->lpVtbl->SetOptions(fd, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    fd->lpVtbl->SetTitle(fd, L"Select the FL Studio folder (contains FL64.exe)");
    if (SUCCEEDED(fd->lpVtbl->Show(fd, dlg)) && SUCCEEDED(fd->lpVtbl->GetResult(fd, &it))) {
        if (SUCCEEDED(it->lpVtbl->GetDisplayName(it, SIGDN_FILESYSPATH, &path))) {
            SetDlgItemTextW(dlg, IDC_FLDIR, path);
            CoTaskMemFree(path);
        }
        it->lpVtbl->Release(it);
    }
    fd->lpVtbl->Release(fd);
}

static void show_fl_info(HWND dlg) {
    wchar_t dir[MAX_PATH * 2], why[256], ver[64], line[320];
    GetDlgItemTextW(dlg, IDC_FLDIR, dir, MAX_PATH * 2);
    if (check_fl_dir(dir, why, 256, ver, 64))
        swprintf_s(line, 320, known_build(ver) ? L"\x2714 FL Studio %s supported." : L"FL Studio %s found; flim support is not verified.", ver);
    else swprintf_s(line, 320, L"\x2716 %s", why);
    SetDlgItemTextW(dlg, IDC_FLINFO, line);
}

static INT_PTR CALLBACK setup_proc(HWND dlg, UINT m, WPARAM wp, LPARAM lp) {
    (void)lp;
    switch (m) {
    case WM_INITDIALOG: {
        wchar_t v[MAX_PATH * 2];
        HICON ic = LoadIconW(GetModuleHandleW(NULL), L"IDI_FLIM");
        if (ic) { SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)ic); SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)ic); }
        flimcfg_get(L"fl_dir", v, MAX_PATH * 2);
        if (!v[0]) discover_fl_dir(v, MAX_PATH * 2);
        SetDlgItemTextW(dlg, IDC_FLDIR, v);
        flimcfg_get(L"name", v, MAX_PATH * 2);
        SetDlgItemTextW(dlg, IDC_NAME, v);
        SendDlgItemMessageW(dlg, IDC_NAME, EM_LIMITTEXT, 64, 0);
        show_fl_info(dlg);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE: browse_folder(dlg); return TRUE;
        case IDC_FLDIR: if (HIWORD(wp) == EN_CHANGE) show_fl_info(dlg); return TRUE;
        case IDOK: {
            wchar_t dir[MAX_PATH * 2], why[256], ver[64], raw[128], name[FLIM_NAME_MAX + 1];
            GetDlgItemTextW(dlg, IDC_FLDIR, dir, MAX_PATH * 2);
            if (!check_fl_dir(dir, why, 256, ver, 64)) {
                fail_box(dlg, L"%s", why);
                SetFocus(GetDlgItem(dlg, IDC_FLDIR));
                return TRUE;
            }
            GetDlgItemTextW(dlg, IDC_NAME, raw, 128);
            if (!flim_sanitize_name(raw, name, FLIM_NAME_MAX + 1)) {
                fail_box(dlg, L"Enter a name with at least one letter or digit.");
                SetFocus(GetDlgItem(dlg, IDC_NAME));
                return TRUE;
            }
            flimcfg_set(L"fl_dir", dir);
            flimcfg_set(L"name", name);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
        }
        break;
    }
    return FALSE;
}

static int config_ok(void) {
    wchar_t dir[MAX_PATH * 2], name[128], why[256], ver[64];
    flimcfg_get(L"fl_dir", dir, MAX_PATH * 2);
    flimcfg_get(L"name", name, 128);
    return name[0] && check_fl_dir(dir, why, 256, ver, 64);
}

static int launch(const wchar_t *flDir) {
    wchar_t fl[MAX_PATH * 2], dll[MAX_PATH], why[256], ver[64], dirCopy[MAX_PATH * 2];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE th = NULL;
    void *remote = NULL;
    SIZE_T nbytes, written = 0;
    DWORD wait, loaded = 0, error, dllLen;
    int known, result = 1;

    wcscpy_s(dirCopy, MAX_PATH * 2, flDir);
    if (!check_fl_dir(dirCopy, why, 256, ver, 64)) { fail_box(NULL, L"%s", why); return 1; }
    known = known_build(ver);
    {
        if (!known) {
            wchar_t msg[768];
            swprintf_s(msg, 768,
                L"This FL Studio is version %s. flim supports these builds: %s.\n\n"
                L"This build has no verified address table. FL will start normally without loading flim.\n\n"
                L"Start FL Studio anyway?",
                ver, FLIM_FL_BUILDS_TEXT);
            if (MessageBoxW(NULL, msg, L"flim", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 1;
        }
    }
    swprintf_s(fl, MAX_PATH * 2, L"%s\\FL64.exe", dirCopy);

    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (!known) {
        if (!CreateProcessW(fl, NULL, NULL, NULL, FALSE, 0, NULL, dirCopy, &si, &pi)) {
            fail_box(NULL, L"Could not start FL Studio (error %lu):\n%s", GetLastError(), fl);
            return 1;
        }
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 0;
    }

    // engine DLL sits next to flim.exe
    dllLen = GetModuleFileNameW(NULL, dll, MAX_PATH);
    if (!dllLen || dllLen >= MAX_PATH) {
        fail_box(NULL, L"The flim executable path is too long or unavailable."); return 1;
    }
    wchar_t *slash = wcsrchr(dll, L'\\');
    if (!slash || (size_t)(slash + 1 - dll) + wcslen(L"flim_engine.dll") >= MAX_PATH) {
        fail_box(NULL, L"The flim engine path is too long. Move flim to a shorter folder path."); return 1;
    }
    wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - dll), L"flim_engine.dll");
    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) {
        fail_box(NULL, L"flim_engine.dll was not found next to flim.exe:\n%s", dll);
        return 1;
    }

    // FL's working directory = its install folder, as when started from its own shortcut
    if (!CreateProcessW(fl, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, dirCopy, &si, &pi)) {
        fail_box(NULL, L"Could not start FL Studio (error %lu):\n%s", GetLastError(), fl);
        return 1;
    }

    nbytes = (wcslen(dll) + 1) * sizeof(wchar_t);
    remote = VirtualAllocEx(pi.hProcess, NULL, nbytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(pi.hProcess, remote, dll, nbytes, &written) || written != nbytes) {
        fail_box(NULL, L"Could not load the flim engine into FL (alloc/write error %lu).", GetLastError());
        goto failed;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE loadlib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    th = loadlib ? CreateRemoteThread(pi.hProcess, NULL, 0, loadlib, remote, 0, NULL) : NULL;
    if (!th) {
        fail_box(NULL, L"Could not load the flim engine into FL (CreateRemoteThread error %lu).", GetLastError());
        goto failed;
    }
    wait = WaitForSingleObject(th, 15000);
    if (wait != WAIT_OBJECT_0) {
        error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        fail_box(NULL, L"Loading the flim engine did not complete (error %lu). FL Studio was not resumed.", error);
        goto failed;
    }
    if (!GetExitCodeThread(th, &loaded) || !loaded) {
        fail_box(NULL, L"Windows could not load flim_engine.dll. FL Studio was not resumed.");
        goto failed;
    }
    VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
    remote = NULL;
    if (ResumeThread(pi.hThread) == (DWORD)-1) {
        fail_box(NULL, L"Could not resume FL Studio (error %lu).", GetLastError());
        goto failed;
    }
    result = 0;
    goto cleanup;
failed:
    // A timed-out LoadLibrary thread may still be reading remote; terminate before reclaiming it.
    if (!TerminateProcess(pi.hProcess, 1))
        fail_box(NULL, L"Could not close the failed FL Studio process (PID %lu, error %lu).", pi.dwProcessId, GetLastError());
cleanup:
    if (th) CloseHandle(th);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return result;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    int argc = 0, i, setup = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t dir[MAX_PATH * 2], flDir[MAX_PATH * 2] = L"";
    (void)prev; (void)cmd; (void)show;
    for (i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"--setup") == 0 || _wcsicmp(argv[i], L"/setup") == 0) setup = 1;
        // dev / tests: start the FL in another folder this once (flim.ini keeps its own)
        else if (_wcsicmp(argv[i], L"--fl-dir") == 0 && i + 1 < argc) wcscpy_s(flDir, MAX_PATH * 2, argv[++i]);
    }
    LocalFree(argv);

    SetProcessDPIAware();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (setup || !config_ok()) {
        if (DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SETUP), NULL, setup_proc, 0) != IDOK) return 1;
    }
    CoUninitialize();
    if (flDir[0]) return launch(flDir);
    flimcfg_get(L"fl_dir", dir, MAX_PATH * 2);
    return launch(dir);
}
