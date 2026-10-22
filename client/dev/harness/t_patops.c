// Synthetic local memory only: verify moved VMT slots without starting FL.
#define FLIM_EXPERIMENTAL_FL26 1
#include <assert.h>
#include "../../engine/flim_engine.c"

int main(void) {
    unsigned char *image;
    void **vmt;
    size_t i, j;
    const int wire[PATOP_N] = { 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0x100 };
    const uint32_t slots26[PATOP_N] = { 0xe8, 0xf8, 0x108, 0x110, 0x118, 0x120 };
    image = (unsigned char *)VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(image);
    g_flBase = (char *)image;
    for (i = 0; i < sizeof(k_flBuilds) / sizeof(k_flBuilds[0]); i++) {
        int experimental = !wcscmp(k_flBuilds[i].build, L"26.1.6.5639");
        memset(image, 0, 8192);
        g_fl = k_flBuilds[i];
        g_fl.FLPROJ = IMG_BASE + 0x100;
        g_fl.MSGBOX = IMG_BASE + 0x200;
        g_fl.msgbox = 1;
        *(void **)(image + 0x100) = image + 0x110;
        *(void **)(image + 0x110) = image + 0x300;
        *(void **)(image + 0x310) = image + 0x380;
        *(void **)(image + 0x380) = image + 0x400;
        vmt = (void **)(image + 0x400);
        for (j = 0; j < 64; j++) vmt[j] = (void *)(uintptr_t)(0x100000 + j * 16);
        memcpy(image + 0x200, "\x48\x83\xec\x28\xb0\x03\x4d\x89\xc1\x41\x89\xc0", 12);
        assert(fl_project() == image + 0x380);
        assert(FLPROJ_LOAD_SLOT == (experimental ? 0x40 : 0x38) / 8);
        patops_install();
        for (j = 0; j < 64; j++) {
            int k, matched = 0;
            for (k = 0; k < PATOP_N; k++) {
                uint32_t slot = experimental ? slots26[k] : (uint32_t)wire[k];
                assert(PATOP_SLOT[k] == wire[k]);
                assert(patop_vmt_slot(k) == slot);
                if (j * sizeof(void *) == slot) {
                    assert(vmt[j] == (void *)PATOP_HOOKS[k]);
                    assert((void *)g_patOrig[k] == (void *)(uintptr_t)(0x100000 + j * 16));
                    matched = 1;
                }
            }
            if (!matched) assert(vmt[j] == (void *)(uintptr_t)(0x100000 + j * 16));
        }
    }
    VirtualFree(image, 0, MEM_RELEASE);
    puts("Pattern VMT hooks: OK (all builds, synthetic memory; wire IDs unchanged)");
    return 0;
}
