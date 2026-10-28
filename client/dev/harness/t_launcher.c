// Read-only checks: no FL process is started and no DLL is loaded into FL.
#include <assert.h>
#include "../../launcher/flim.c"

int main(void) {
    wchar_t dir[MAX_PATH * 2], why[256], ver[64];
    LaunchOptions options;
    wchar_t *desktop[] = { L"flim.exe", L"--desktop", L"--fl-dir", L"C:\\Program Files\\Image-Line\\FL Studio 2026" };
    wchar_t *inspect[] = { L"flim.exe", L"--inspect-json", L"C:\\FL" };
    wchar_t *missing[] = { L"flim.exe", L"--fl-dir" };
    wchar_t *relative[] = { L"flim.exe", L"--desktop", L"--fl-dir", L"..\\FL" };
    wchar_t *remote[] = { L"flim.exe", L"--inspect-json", L"\\\\remote\\FL" };
    wchar_t *duplicate[] = { L"flim.exe", L"--fl-dir", L"C:\\FL", L"--fl-dir", L"C:\\Other" };
    wchar_t *mixed[] = { L"flim.exe", L"--desktop", L"--inspect-json", L"C:\\FL" };
    wchar_t *unknown[] = { L"flim.exe", L"--disable-compatibility" };
    wchar_t *experimental[] = { L"flim.exe", L"--desktop", L"--allow-experimental", L"--fl-dir", L"C:\\FL" };
    wchar_t *experimental_inspect[] = { L"flim.exe", L"--allow-experimental", L"--inspect-json", L"C:\\FL" };
    wchar_t *experimental_plain[] = { L"flim.exe", L"--allow-experimental", L"--fl-dir", L"C:\\FL" };
    wchar_t *experimental_duplicate[] = { L"flim.exe", L"--desktop", L"--allow-experimental", L"--allow-experimental", L"--fl-dir", L"C:\\FL" };
    assert(parse_options(4, desktop, &options) && options.desktop && !options.inspect && !wcscmp(options.flDir, desktop[3]));
    assert(parse_options(3, inspect, &options) && options.inspect && !options.desktop);
    assert(!parse_options(2, missing, &options));
    assert(!parse_options(4, relative, &options));
    assert(!parse_options(3, remote, &options));
    assert(!parse_options(5, duplicate, &options));
    assert(!parse_options(4, mixed, &options));
    assert(!parse_options(2, unknown, &options));
    assert(parse_options(5, experimental, &options) && options.desktop && options.allowExperimental);
    assert(!parse_options(4, experimental_inspect, &options));
    assert(!parse_options(4, experimental_plain, &options));
    assert(!parse_options(6, experimental_duplicate, &options));
    assert(!parse_options(2, desktop, &options));
    assert(parse_options(1, desktop, &options) && !options.desktop && !options.flDir[0]);
    assert(known_build(L"25.2.3.5171"));
    assert(known_build(L"25.1.6.4997"));
    assert(!known_build(L"26.99.99.99999"));
#ifdef FLIM_EXPERIMENTAL_FL26
    assert(desktop_known_build(L"24.2.2.4597"));
#else
    assert(!desktop_known_build(L"24.2.2.4597"));
#endif
    assert(desktop_known_build(L"25.2.3.5171"));
    dir[0] = 0;
    assert(!check_fl_dir(dir, why, 256, ver, 64));
    assert(why[0] && !ver[0]);
    wcscpy_s(dir, MAX_PATH * 2, L"Z:\\flim-test-nonexistent-folder");
    assert(!check_fl_dir(dir, why, 256, ver, 64));
    discover_fl_dir(dir, MAX_PATH * 2);
    if (dir[0]) {
        assert(check_fl_dir(dir, why, 256, ver, 64));
        wprintf(L"Discovered: %s (build %s, verified support: %s)\n", dir, ver, known_build(ver) ? L"yes" : L"no");
    }
    puts("Launcher validation: OK");
    return 0;
}
