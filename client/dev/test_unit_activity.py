"""Compile the engine's activity loop in isolation; no FL process or proprietary fixture."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


CLIENT = Path(__file__).resolve().parents[1]

REFERENCE = r"""
for (i = 0; i < nc && nc < 2000; i++) {
    int unit, a, b, j, cnt = 0;
    Unit *u;
    if (!unit_of_id(g_changes[i].s->id, &unit, &a, &b) || !(u = unit_ptr(unit, a, b))) continue;
    u->lastAct = now;
    for (j = 0; j < nc; j++) {
        int u2, a2, b2;
        if (unit_of_id(g_changes[j].s->id, &u2, &a2, &b2) && u2 == unit && a2 == a && b2 == b) cnt++;
    }
    if (cnt >= BURST_PARAMS) u->burst = 1;
}
"""

HARNESS = r"""
static uint32_t seed = 0x79bdf123;
static uint32_t random32(void) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
}
static uint32_t fxid(int track, int slot) {
    return 0x70000000u + ((((uint32_t)track << 6) | slot) << 16) + REC_PLUG_FIRST;
}
static void fill(int n, int mode) {
    int i;
    for (i = 0; i < n; i++) {
        uint32_t r = random32();
        slots[i].id = mode == 0 ? 0x00038000u + i :
            mode == 1 ? (i < 7 ? 0x00038000u : fxid(3, 0)) :
            r % 3 == 0 ? ((r % MAX_CH) << 16) | 0x8000u | (r & 0x7fff) :
            r % 3 == 1 ? fxid((r >> 8) % MAX_MIX, r % MAX_FX_SLOTS) : r;
        g_changes[i].s = &slots[i];
    }
}
static void check(int n, DWORD now) {
    static Unit expectedCh[MAX_CH], expectedFx[MAX_MIX][MAX_FX_SLOTS];
    int i, j;
    for (i = 0; i < MAX_CH; i++) { g_chU[i].lastAct = i + 10; g_chU[i].burst = i % 2; }
    for (i = 0; i < MAX_MIX; i++) for (j = 0; j < MAX_FX_SLOTS; j++) {
        g_fxU[i][j].lastAct = i + j + 30; g_fxU[i][j].burst = (i + j) % 2;
    }
    activity_old(n, now);
    memcpy(expectedCh, g_chU, sizeof(g_chU)); memcpy(expectedFx, g_fxU, sizeof(g_fxU));
    for (i = 0; i < MAX_CH; i++) { g_chU[i].lastAct = i + 10; g_chU[i].burst = i % 2; }
    for (i = 0; i < MAX_MIX; i++) for (j = 0; j < MAX_FX_SLOTS; j++) {
        g_fxU[i][j].lastAct = i + j + 30; g_fxU[i][j].burst = (i + j) % 2;
    }
    activity_current(n, now);
    if (memcmp(expectedCh, g_chU, sizeof(g_chU)) || memcmp(expectedFx, g_fxU, sizeof(g_fxU))) {
        fprintf(stderr, "activity differs: n=%d now=%lu seed=%u\n", n, now, seed); exit(1);
    }
}
static int order_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y);
}
static double measure(void (*fn)(int, DWORD), int n, int iterations) {
    LARGE_INTEGER begin, end, frequency;
    int i;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin);
    for (i = 0; i < iterations; i++) fn(n, (DWORD)i + 1000);
    QueryPerformanceCounter(&end);
    return 1000.0 * (end.QuadPart - begin.QuadPart) / frequency.QuadPart;
}
int main(int argc, char **argv) {
    int sizes[] = {0, 1, 7, 8, 9, 15, 32, 1999, 2000, 2001};
    int i, mode, cases = 0;
    FlimUnitBurstCount counts[3] = {{1, 99}, {42, 50}, {0xffffffffu, 17}};
    uint32_t epoch = 0xffffffffu;
    assert(flim_unit_bursts_begin(counts, 3, &epoch) == 1);
    for (i = 0; i < 3; i++) { assert(counts[i].epoch == 0); assert(counts[i].count == 0); }
    assert(flim_unit_bursts_hit(&counts[0], epoch) == 1);
    assert(flim_unit_bursts_hit(&counts[0], epoch) == 2);
    assert(flim_unit_bursts_begin(counts, 3, &epoch) == 2);
    assert(flim_unit_bursts_hit(&counts[0], epoch) == 1);
    for (mode = 0; mode < 3; mode++) for (i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        fill(sizes[i], mode); check(sizes[i], 12345); cases++;
    }
    {
        uint32_t boundaries[] = {0, 0x00ff0001, 0x01000001, 0x70000001, fxid(0, 0),
            fxid(MAX_MIX - 1, MAX_FX_SLOTS - 1), fxid(0, MAX_FX_SLOTS), fxid(MAX_MIX, 0), 0xf0000000, 0xffffffff};
        for (i = 0; i < sizeof(boundaries)/sizeof(boundaries[0]); i++) {
            slots[i].id = boundaries[i]; g_changes[i].s = &slots[i];
        }
        check(sizeof(boundaries)/sizeof(boundaries[0]), 0xffffffffu); cases++;
    }
    for (i = 0; i < 200; i++) { int n = random32() % 2002; fill(n, 2); check(n, random32()); cases++; }
    memset(g_chU, 0, sizeof(g_chU)); fill(8, 0);
    activity_current(7, 100); assert(g_chU[3].burst == 0 && g_chU[3].lastAct == 100);
    activity_current(7, 200); assert(g_chU[3].burst == 0 && g_chU[3].lastAct == 200);
    activity_current(8, 300); assert(g_chU[3].burst == 1 && g_chU[3].lastAct == 300);
    activity_current(1, 400); assert(g_chU[3].burst == 1 && g_chU[3].lastAct == 400);
    fill(2001, 0); activity_current(2000, 500); activity_current(2001, 600);
    assert(g_chU[3].lastAct == 400 && g_chU[3].burst == 1);
    printf("PASS %d reference comparisons + repeated-tick, sticky-burst, >=2000 skip, and epoch-wrap checks\n", cases);
    if (argc > 1 && !strcmp(argv[1], "--benchmark")) {
        int ncases[] = {32, 1999}, iterations[] = {20000, 20};
        int k, run;
        for (k = 0; k < 2; k++) {
            double old[5], current[5]; fill(ncases[k], 2);
            for (run = 0; run < 5; run++) {
                old[run] = measure(activity_old, ncases[k], iterations[k]);
                current[run] = measure(activity_current, ncases[k], iterations[k]);
            }
            qsort(old, 5, sizeof(double), order_double); qsort(current, 5, sizeof(double), order_double);
            printf("n=%d iterations=%d median_ms original=%.4f current=%.4f ratio=%.2f\n",
                ncases[k], iterations[k], old[2], current[2], old[2] / current[2]);
        }
    }
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", action="store_true")
    args = parser.parse_args()
    source = (CLIENT / "engine" / "flim_engine.c").read_text(encoding="utf-8")
    constants = []
    for name in ("MAX_CH", "MAX_MIX", "MAX_FX_SLOTS", "BURST_PARAMS", "REC_PLUG_FIRST", "IS_MUTE_ID"):
        constants.append(re.search(rf"^#define {name}(?:\([^\n]*?\))?\s+[^\n]+", source, re.M).group())
    mapping = "\n".join(re.search(rf"static (?:int|Unit \*)\s*{name}\([^\n]+\{{.*?^\}}", source, re.M | re.S).group()
                        for name in ("unit_ptr", "unit_of_id"))
    body = source.split("// per-unit activity: remember it, and flag preset-sized bursts", 1)[1]
    body = body.split("schedule_units(now);", 1)[0]
    prefix = "\n".join(constants) + r"""
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "flim_unit_bursts.h"
enum { U_CH, U_FX };
typedef struct { DWORD lastAct; int burst; } Unit;
typedef struct { uint32_t id; } Slot;
typedef struct { Slot *s; } Change;
static Unit g_chU[MAX_CH], g_fxU[MAX_MIX][MAX_FX_SLOTS];
static Slot slots[2002];
static Change g_changes[2002];
"""
    program = (prefix + mapping + "\nstatic __declspec(noinline) void activity_old(int nc, DWORD now) { int i;\n" +
               REFERENCE + "\n}\nstatic __declspec(noinline) void activity_current(int nc, DWORD now) { int i;\n" +
               body + "\n}\n" + HARNESS)
    with tempfile.TemporaryDirectory(prefix="flim-unit-activity-") as temp:
        scratch = Path(temp)
        c_file, exe = scratch / "activity.c", scratch / "activity.exe"
        c_file.write_text(program, encoding="utf-8")
        subprocess.run(["cl", "/nologo", "/O2", "/W3", f"/I{CLIENT / 'common'}", str(c_file),
                        f"/Fo{scratch / 'activity.obj'}", f"/Fe{exe}"], cwd=scratch, check=True)
        subprocess.run([str(exe)] + (["--benchmark"] if args.benchmark else []), check=True)


if __name__ == "__main__":
    main()
