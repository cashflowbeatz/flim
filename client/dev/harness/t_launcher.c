// Read-only checks: no FL process is started and no DLL is loaded into FL.
#include <assert.h>
#include "../../launcher/flim.c"

int main(void) {
    wchar_t dir[MAX_PATH * 2], why[256], ver[64];
    assert(known_build(L"25.2.3.5171"));
    assert(known_build(L"25.1.6.4997"));
    assert(!known_build(L"26.99.99.99999"));
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
