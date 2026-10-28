#include <assert.h>
#include <stdlib.h>
#include <wchar.h>
static int fail_allocation;
static void *test_calloc(size_t n, size_t size) {
    if (fail_allocation) return NULL;
    return calloc(n, size);
}
#define calloc test_calloc
#include "../../engine/flim_engine.c"
#undef calloc
#undef FLPROJ_LOAD_SLOT

static uint64_t image[4], holder[3];
static void *form_pointer, *holder_pointer, *project_vmt[6], **project_object;
static char form;
static int begins, ends, loads, names, writes, fault_load, fault_end, fault_name, fail_name_memory;
static char load_result;
static const wchar_t path[] = L"flim-owned-project-load-test.flp";

static void test_begin(void *self) { assert(self == &form); begins++; }
static void test_end(void *self) {
    assert(self == &form); ends++;
    if (fault_end) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, NULL);
}
static char test_load(void *self, const wchar_t *file, int flags) {
    assert(self == &project_object && !wcscmp(file, path) && flags == 0);
    loads++;
    if (fault_load) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, NULL);
    if (fail_name_memory) fail_allocation = 1;
    return load_result;
}
static void test_name(void *self, const wchar_t *a, const wchar_t *b, int recent) {
    assert(self == &form && !wcscmp(a, path) && a == b && recent == 1);
    if (fault_name) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, NULL);
    names++;
}
static uint32_t test_rec(uint32_t id, uint64_t value, uint32_t flags) {
    (void)id; (void)value; (void)flags; writes++; return 1;
}
static uint64_t function_va(void *fn) {
    return (uint64_t)(uintptr_t)fn - (uint64_t)(uintptr_t)image + IMG_BASE;
}
static QItem fixture(void) {
    QItem item = {0};
    memset(&g_fl, 0, sizeof(g_fl)); memset(image, 0, sizeof(image));
    form_pointer = &form; holder_pointer = holder; project_object = project_vmt;
    image[1] = (uint64_t)(uintptr_t)&form_pointer;
    image[2] = (uint64_t)(uintptr_t)&holder_pointer;
    holder[2] = (uint64_t)(uintptr_t)&project_object;
    project_vmt[5] = test_load;
    g_flBase = (char *)image; g_fl.MAINOBJ = IMG_BASE + 8; g_fl.FLPROJ = IMG_BASE + 16;
    g_fl.FLPROJ_LOAD_SLOT = 40;
    g_fl.BUSYBEGIN = function_va(test_begin); g_fl.BUSYEND = function_va(test_end);
    g_fl.SETPROJ = function_va(test_name);
    g_compatFault = 0; g_compatReady = 1; g_projBusy = g_projRemote = 1;
    g_alert[0] = 0; g_ws = NULL; g_qh = g_qt = 0; g_npend = 0;
    g_rec = test_rec;
    begins = ends = loads = names = writes = fault_load = fault_end = fail_allocation = fault_name = fail_name_memory = 0;
    load_result = 1;
    item.path = (char *)path; item.mode = 1;
    return item;
}
static void stopped(void) {
    assert(g_compatFault && !g_compatReady && !names);
    assert(strstr(g_alert, "stopped sharing"));
    drain_queue_mainthread();
    assert(!writes);
}
int main(void) {
    QItem item;
    InitializeCriticalSection(&g_sendLock); InitializeCriticalSection(&g_netTextLock);
    InitializeCriticalSection(&g_qLock); InitializeCriticalSection(&g_lifecycleLock);
    item = fixture(); do_projload(&item);
    assert(begins == 1 && loads == 1 && ends == 1 && names == 1 && !g_compatFault);
    assert(g_projBusy && g_projRemote && g_compatReady);
    item = fixture(); item.mode = 0; do_projload(&item);
    assert(loads == 1 && !names && !g_compatFault);
    item = fixture(); load_result = 0; enqueue_apply(0, 123); do_projload(&item);
    assert(begins == 1 && loads == 1 && ends == 1); stopped();
    item = fixture(); fault_load = 1; enqueue_apply(0, 123); do_projload(&item);
    assert(begins == 1 && loads == 1 && ends == 1); stopped();
    item = fixture(); fault_end = 1; do_projload(&item);
    assert(loads == 1 && ends == 1); stopped();
    item = fixture(); form_pointer = NULL; do_projload(&item);
    assert(!begins && !loads && !ends); stopped();
    item = fixture(); item.path = NULL; do_projload(&item);
    assert(!begins && !loads && !ends); stopped();
    item = fixture(); fail_allocation = 1; do_projload(&item); fail_allocation = 0;
    assert(!begins && !loads && !ends); stopped();
    item = fixture(); fault_name = 1; do_projload(&item);
    assert(loads == 1 && ends == 1); stopped();
    item = fixture(); fail_name_memory = 1; do_projload(&item); fail_allocation = 0;
    assert(loads == 1 && ends == 1); stopped();
    { wchar_t long_path[MAX_PATH + 1];
      wmemset(long_path, L'a', MAX_PATH); long_path[MAX_PATH] = 0;
      item = fixture(); item.path = (char *)long_path; do_projload(&item);
      assert(!begins && !loads && !ends); stopped(); }
    puts("Project load failures: OK (refusal, fault cleanup, memory, unchanged save target, queued edits blocked)");
    return 0;
}
