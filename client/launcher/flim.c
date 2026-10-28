// flim.exe launcher: start FL suspended, inject flim_engine.dll via classic
// LoadLibraryW remote-thread injection, then resume. On-disk FL is untouched.
// First run (or FL not found, or `flim.exe --setup`): a setup dialog asks for the
// FL Studio folder and a user name; both are saved in %APPDATA%\flim\flim.ini.

#include <windows.h>
#include <shobjidl.h>
#include <stdio.h>
#include "../common/flim_config.h"
#include "../common/flim_profile.h"
#include "../common/flim_res.h"
#pragma comment(lib, "version.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static int g_machineOutput;
static int g_allowExperimental;
static DWORD g_launchedPid;

static void stdout_text(const char *text) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    size_t left = strlen(text);
    if (!output || output == INVALID_HANDLE_VALUE) return;
    while (left) {
        DWORD written = 0;
        if (!WriteFile(output, text, (DWORD)left, &written, NULL) || !written) return;
        text += written; left -= written;
    }
}

static void stdout_json_string(const wchar_t *text) {
    char utf8[4096], escaped[8192];
    size_t i, o = 0;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, utf8, sizeof(utf8), NULL, NULL))
        strcpy_s(utf8, sizeof(utf8), "Unable to encode native status");
    for (i = 0; utf8[i] && o + 7 < sizeof(escaped); i++) {
        unsigned char c = (unsigned char)utf8[i];
        if (c == '\\' || c == '"') { escaped[o++] = '\\'; escaped[o++] = (char)c; }
        else if (c < 32) o += sprintf_s(escaped + o, sizeof(escaped) - o, "\\u%04x", c);
        else escaped[o++] = (char)c;
    }
    escaped[o] = 0;
    stdout_text("\""); stdout_text(escaped); stdout_text("\"");
}

static void fail_box(HWND owner, const wchar_t *fmt, ...) {
    wchar_t msg[1024];
    va_list ap; va_start(ap, fmt); vswprintf_s(msg, 1024, fmt, ap); va_end(ap);
    if (g_machineOutput) {
        stdout_text("{\"error\":"); stdout_json_string(msg); stdout_text("}\n");
    } else MessageBoxW(owner, msg, L"flim", MB_OK | MB_ICONERROR);
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

static int desktop_known_build(const wchar_t *ver) {
    return known_build(ver);
}

static int supported_install(const wchar_t *dir, const wchar_t *ver, wchar_t *why, int cap) {
    wchar_t engine[MAX_PATH * 2], build[32]; FlBuild row;
    int result;
    swprintf_s(engine, MAX_PATH * 2, L"%s\\FLEngine_x64.dll", dir);
    result = flim_profile_load(NULL, engine, ver, &row, build, why, cap);
    if (result) return result;
    if (!wcscmp(ver, L"24.2.2.4597")) {
#ifdef FLIM_EXPERIMENTAL_FL26
        char hash[65] = {0};
        HANDLE file = CreateFileW(engine, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        int matched = file != INVALID_HANDLE_VALUE && flim_profile_sha256(file, hash) &&
            !strcmp(hash, "5e5d083bd0f0fcbc8d948570cf4c27c7e9349e6851478c7a9c05a11509ab8f32");
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (matched) {
            why[0] = 0;
            return 1;
        }
        swprintf_s(why, cap, L"FL 24.2.2.4597 does not match the supported engine file. It was not enabled.");
#else
        swprintf_s(why, cap, L"FL 24.2.2.4597 requires the FL 24/25/26 client build or a verified compatibility profile.");
#endif
        return -1;
    }
    return g_machineOutput ? desktop_known_build(ver) : known_build(ver);
}

static int local_absolute_dir(const wchar_t *dir) {
    return dir && ((dir[0] >= L'A' && dir[0] <= L'Z') || (dir[0] >= L'a' && dir[0] <= L'z')) &&
           dir[1] == L':' && (dir[2] == L'\\' || dir[2] == L'/');
}

static int inspect_install(const wchar_t *input) {
    wchar_t dir[MAX_PATH * 2], why[256], version[64];
    int supported = 0;
    if (!local_absolute_dir(input) || wcslen(input) >= MAX_PATH * 2) {
        fail_box(NULL, L"Select an absolute local FL Studio installation folder."); return 1;
    }
    wcscpy_s(dir, MAX_PATH * 2, input);
    if (check_fl_dir(dir, why, 256, version, 64)) {
        supported = supported_install(dir, version, why, 256);
        if (!supported) wcscpy_s(why, 256, L"This FL build needs a verified compatibility profile.");
    }
    stdout_text("{\"path\":"); stdout_json_string(dir);
    stdout_text(",\"version\":"); stdout_json_string(version);
    stdout_text(supported == 2 ? ",\"experimental\":true" : ",\"experimental\":false");
    stdout_text(supported > 0 ? ",\"supported\":true,\"reason\":" : ",\"supported\":false,\"reason\":");
    stdout_json_string(why); stdout_text("}\n");
    return 0;
}

static void discover_fl_dir(wchar_t *out, int cap) {
    static const wchar_t *roots[] = { L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)" };
    wchar_t searched[3][MAX_PATH] = {{0}};
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
        {
            size_t j;
            for (j = 0; j < i; j++) if (!_wcsicmp(root, searched[j])) break;
            if (j < i) continue;
            wcscpy_s(searched[i], MAX_PATH, root);
        }
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
            known = supported_install(dir, ver, why, 256);
            if (known == 2) known = 0;
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
    if (check_fl_dir(dir, why, 256, ver, 64)) {
        int supported = supported_install(dir, ver, why, 256);
        if (supported < 0) swprintf_s(line, 320, L"\x2716 %s", why);
        else if (supported == 2) swprintf_s(line, 320, L"FL Studio %s test only: live sharing is unverified.", ver);
        else swprintf_s(line, 320, supported ? L"\x2714 FL Studio %s supported." : L"FL Studio %s found; flim support is not verified.", ver);
    } else swprintf_s(line, 320, L"\x2716 %s", why);
    SetDlgItemTextW(dlg, IDC_FLINFO, line);
}

#define FLIM_SETUP_CHECK_TIMER 1
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
        KillTimer(dlg, FLIM_SETUP_CHECK_TIMER);
        show_fl_info(dlg);
        return TRUE;
    }
    case WM_TIMER:
        if (wp == FLIM_SETUP_CHECK_TIMER) {
            KillTimer(dlg, FLIM_SETUP_CHECK_TIMER);
            show_fl_info(dlg);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        KillTimer(dlg, FLIM_SETUP_CHECK_TIMER);
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE: browse_folder(dlg); return TRUE;
        case IDC_FLDIR:
            if (HIWORD(wp) == EN_CHANGE) {
                SetDlgItemTextW(dlg, IDC_FLINFO, L"Checking compatibility...");
                KillTimer(dlg, FLIM_SETUP_CHECK_TIMER);
                if (!SetTimer(dlg, FLIM_SETUP_CHECK_TIMER, 350, NULL)) show_fl_info(dlg);
            }
            return TRUE;
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
    known = supported_install(dirCopy, ver, why, 256);
    if (known < 0) { fail_box(NULL, L"%s", why); return 1; }
    if (known == 2 && g_machineOutput && !g_allowExperimental) {
        fail_box(NULL, L"This build requires explicit experimental testing before starting this room.");
        return 1;
    }
    {
        if (!known) {
            wchar_t msg[768];
            if (g_machineOutput) {
                fail_box(NULL, L"FL Studio %s has no verified compatibility profile. It was not launched.", ver);
                return 1;
            }
            swprintf_s(msg, 768,
                L"This FL Studio is version %s. Built-in profiles: %s.\n\n"
                L"Additional FL 25/26 builds need a reviewed profile in flim\'s profiles folder. This engine has no verified profile. FL will start normally without flim.\n\n"
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
    g_launchedPid = pi.dwProcessId;
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

typedef struct {
    int setup, desktop, inspect, allowExperimental;
    wchar_t flDir[MAX_PATH * 2];
} LaunchOptions;

static int parse_options(int argc, wchar_t **argv, LaunchOptions *options) {
    int i;
    ZeroMemory(options, sizeof(*options));
    for (i = 1; i < argc; i++) {
        if (!_wcsicmp(argv[i], L"--setup") || !_wcsicmp(argv[i], L"/setup")) options->setup = 1;
        else if (!_wcsicmp(argv[i], L"--desktop")) options->desktop = 1;
        else if (!_wcsicmp(argv[i], L"--allow-experimental")) {
            if (options->allowExperimental) return 0;
            options->allowExperimental = 1;
        }
        else if (!_wcsicmp(argv[i], L"--fl-dir") || !_wcsicmp(argv[i], L"--inspect-json")) {
            if (options->flDir[0] || i + 1 == argc || !argv[i + 1][0] || wcslen(argv[i + 1]) >= MAX_PATH * 2) return 0;
            options->inspect = !_wcsicmp(argv[i], L"--inspect-json");
            wcscpy_s(options->flDir, MAX_PATH * 2, argv[++i]);
        } else return 0;
    }
    if ((options->desktop || options->inspect) && (options->setup || !local_absolute_dir(options->flDir))) return 0;
    if (options->desktop && options->inspect) return 0;
    if (options->allowExperimental && !options->desktop) return 0;
    return 1;
}

static int environment_name_valid(void) {
    wchar_t raw[128], name[FLIM_NAME_MAX + 1];
    DWORD n = GetEnvironmentVariableW(L"FLIM_NAME", raw, 128);
    return n && n < 128 && flim_sanitize_name(raw, name, FLIM_NAME_MAX + 1);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    int argc = 0, i, parsed, result;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t dir[MAX_PATH * 2];
    LaunchOptions options;
    (void)prev; (void)cmd; (void)show;
    if (!argv) return 1;
    for (i = 1; i < argc; i++) if (!_wcsicmp(argv[i], L"--desktop") || !_wcsicmp(argv[i], L"--inspect-json")) g_machineOutput = 1;
    parsed = parse_options(argc, argv, &options);
    LocalFree(argv);
    if (!parsed) { fail_box(NULL, L"Invalid launcher options or FL Studio folder."); return 1; }
    g_allowExperimental = options.allowExperimental;
    if (options.inspect) return inspect_install(options.flDir);
    if (options.desktop && !environment_name_valid()) {
        fail_box(NULL, L"Enter a name before opening FL Studio."); return 1;
    }
    if (options.desktop) {
        char status[96];
        result = launch(options.flDir);
        if (!result) {
            sprintf_s(status, sizeof(status), "{\"launched\":true,\"pid\":%lu}\n", g_launchedPid);
            stdout_text(status);
        }
        return result;
    }

    SetProcessDPIAware();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (options.setup || (!config_ok() && !(options.flDir[0] && environment_name_valid()))) {
        if (DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SETUP), NULL, setup_proc, 0) != IDOK) return 1;
    }
    CoUninitialize();
    if (options.flDir[0]) return launch(options.flDir);
    flimcfg_get(L"fl_dir", dir, MAX_PATH * 2);
    return launch(dir);
}
