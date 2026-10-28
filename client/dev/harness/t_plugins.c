#include <assert.h>
#include <windows.h>
static int dialogs, dialog_result = IDCANCEL;
static int WINAPI test_plugin_dialog(HWND parent, LPCWSTR text, LPCWSTR caption, UINT flags) {
    (void)parent;
    assert(wcsstr(text, L"Notes and other supported edits") && wcsstr(text, L"Install or update"));
    assert(wcsstr(caption, L"unavailable plugins") && (flags & MB_RETRYCANCEL));
    dialogs++; return dialog_result;
}
#define MessageBoxW test_plugin_dialog
#include "../../engine/flim_engine.c"
#undef MessageBoxW

static unsigned char fixture[4096], tlv[2048];
static size_t off, toff;
static void record(unsigned tag, const void *p, size_t n) {
    fp_u32(tlv + toff, tag); fp_u32(tlv + toff + 4, (unsigned)n); fp_u32(tlv + toff + 8, 0);
    memcpy(tlv + toff + 12, p, n); toff += 12 + n;
}
static void event(unsigned id, const void *p, size_t n) {
    fixture[off++] = (unsigned char)id; off += fp_var(fixture + off, n);
    memcpy(fixture + off, p, n); off += n;
}
static size_t preset(int format, int path, int duplicate) {
    static const unsigned char id[16] = {7, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    unsigned char flags[16] = {99, 8, 7};
    off = 22; toff = 4; fp_u32(tlv, 8);
    memcpy(fixture, "FLhd", 4); fp_u32(fixture + 4, 6); memset(fixture + 8, 0, 6);
    memcpy(fixture + 14, "FLdt", 4);
    event(201, L"Fruity Wrapper", sizeof(L"Fruity Wrapper"));
    event(203, L"Example version 9", sizeof(L"Example version 9"));
    record(50, flags, sizeof(flags)); record(format == 2 ? 51 : 52, id, format == 2 ? 4 : 16);
    record(54, "Example version 9", 17);
    if (path) record(55, "Z:\\Sender\\Example.vst3", 23);
    if (duplicate == 1) record(55, "C:\\Duplicate", 12);
    if (duplicate == 2) record(54, "Conflicting name", 16);
    if (duplicate == 3) memset(tlv + 4 + 12 + sizeof(flags) + 12, 0, format == 2 ? 4 : 16);
    record(53, "opaque sound data", 17);
    event(213, tlv, toff); fp_u32(fixture + 18, (unsigned)(off - 22));
    return off;
}

int main(void) {
    FlimPlugin s, adapted, other;
    unsigned char *copy, *local;
    size_t n = preset(3, 1, 0), m, localn, i;
    char reason[320], temp[MAX_PATH], first[MAX_PATH], second[MAX_PATH];
    assert(flim_plugin_read(fixture, n, &s) && s.format == 3 && s.id[0] == 7);
    for (i = 0; i < n; i++) assert(!flim_plugin_read(fixture, i, &other));
    copy = flim_plugin_path(fixture, n, "C:\\My plugins\\Example-8.vst3", 4096, &m);
    assert(copy && flim_plugin_read(copy, m, &adapted) && flim_plugin_same(&s, &adapted));
    assert(!strcmp(adapted.path, "C:\\My plugins\\Example-8.vst3"));
    assert(!memcmp(copy + adapted.payloadAt, fixture + s.payloadAt, s.pathAt));
    assert(!memcmp(copy + adapted.payloadAt + adapted.pathAt + adapted.pathSize,
                   fixture + s.payloadAt + s.pathAt + s.pathSize, s.payloadSize - s.pathAt - s.pathSize));
    assert(!flim_plugin_path(fixture, n, "C:\\TooBig", 10, &localn)); free(copy);
    /* A hostile 64-bit TLV length cannot wrap into the payload. */
    fixture[s.payloadAt + 12] = 0xff; assert(!flim_plugin_read(fixture, n, &other));
    assert(!plugin_prepare(fixture, n, U_FX, &local, &localn, reason, sizeof(reason)) && !local);
    assert(!plugin_prepare(fixture, n - 1, U_FX, &local, &localn, reason, sizeof(reason)) && !local);
    n = preset(3, 1, 1); assert(!flim_plugin_read(fixture, n, &other));
    n = preset(3, 1, 2); assert(!flim_plugin_read(fixture, n, &other));
    n = preset(3, 1, 3); assert(!flim_plugin_read(fixture, n, &other));
    n = preset(2, 0, 0); assert(flim_plugin_read(fixture, n, &other) && !flim_plugin_same(&s, &other));
    n = preset(3, 0, 0); copy = flim_plugin_path(fixture, n, "C:\\Local\\Example.vst3", 4096, &m);
    assert(copy && flim_plugin_read(copy, m, &adapted) && !strcmp(adapted.path, "C:\\Local\\Example.vst3")); free(copy);
    n = preset(3, 1, 0); assert(flim_plugin_read(fixture, n, &s));
    InitializeCriticalSection(&g_ulockLock); InitializeCriticalSection(&g_qLock);
    InitializeCriticalSection(&g_sendLock); InitializeCriticalSection(&g_netTextLock);
    InitializeCriticalSection(&g_outLock);
    GetTempPathA(MAX_PATH, temp); assert(GetTempFileNameA(temp, "vst", 0, first));
    assert(GetTempFileNameA(temp, "vst", 0, second));
    g_pluginCatalog = (PluginEntry *)calloc(2, sizeof(PluginEntry)); assert(g_pluginCatalog);
    memcpy(g_pluginCatalog[0].id, s.id, 16); g_pluginCatalog[0].format = 3; g_pluginCatalog[0].unit = U_FX;
    g_pluginCatalog[0].path = _strdup(first); g_pluginCatalogN = 1; g_pluginCatalogRead = 1;
    g_pluginCatalogFormats[U_FX][1] = 1;
    assert(plugin_prepare(fixture, n, U_FX, &local, &localn, reason, sizeof(reason)) == 1 && local);
    assert(flim_plugin_read(local, localn, &adapted) && !strcmp(adapted.path, first) && flim_plugin_same(&s, &adapted)); free(local);
    /* Same name and different ID must never select a local binary. */
    g_pluginCatalog[0].id[0]++;
    assert(!plugin_prepare(fixture, n, U_FX, &local, &localn, reason, sizeof(reason)) && !local);
    g_pluginCatalog[0].id[0]--;
    g_pluginCatalog[1] = g_pluginCatalog[0]; g_pluginCatalog[1].path = _strdup(second); g_pluginCatalogN = 2;
    assert(!plugin_prepare(fixture, n, U_FX, &local, &localn, reason, sizeof(reason)) && strstr(reason, "several"));
    assert(plugin_path_same("C:\\Plugins\\Sub\\..\\Example.dll", "c:/plugins/Example.dll"));
    assert(!plugin_path_same("C:\\Plugins\\A.dll", "C:\\Plugins\\B.dll"));
    {
        char alias[MAX_PATH]; size_t k;
        strcpy_s(alias, sizeof(alias), first);
        for (k = 0; alias[k]; k++) if (alias[k] == '\\') alias[k] = '/';
        free(g_pluginCatalog[1].path); g_pluginCatalog[1].path = _strdup(alias);
        assert(plugin_prepare(fixture, n, U_FX, &local, &localn, reason, sizeof(reason)) && local); free(local);
        copy = flim_plugin_path(fixture, n, alias, 4096, &m);
        assert(plugin_prepare(copy, m, U_FX, &local, &localn, reason, sizeof(reason)) && !local); free(copy);
        free(g_pluginCatalog[1].path); g_pluginCatalog[1].path = _strdup(second);
    }
    /* Exact scanned paths disambiguate duplicate IDs. */
    copy = flim_plugin_path(fixture, n, second, 4096, &m);
    assert(plugin_prepare(copy, m, U_FX, &local, &localn, reason, sizeof(reason)) && !local); free(copy);
    free(g_pluginCatalog[1].path); g_pluginCatalogN = 1;
    free(g_pluginCatalog[0].path); g_pluginCatalog[0].path = _strdup("");
    assert(plugin_prepare(fixture, n, U_FX, &local, &localn, reason, sizeof(reason)) && !local);
    plugin_catalog_clear();
    g_pluginCatalogRead = 1; // Unscanned databases must defer to FL's resolver.
    assert(plugin_prepare(fixture, n, U_FX, &local, &localn, reason, sizeof(reason)) && !local);
    g_chU[0].unavailable = 1; plug_agreed_set(U_CH, 0, 0, fixture, n); plugins_changed();
    assert(g_pluginMissing == 1 && plug_param_blocked(REC_PLUG_FIRST) && !plug_param_blocked(0));
    /* A late join receives original state, then the newest remote controls, never
       the unavailable host's stale local plugin values. No real socket is opened. */
    g_compatReady = 1; g_ws = (HINTERNET)1; g_outBusy = 1;
    g_outEvt = CreateEventW(NULL, FALSE, FALSE, NULL);
    pend_put(REC_PLUG_FIRST, 77);
    g_nslot = 2;
    g_slots[0].id = REC_PLUG_FIRST; g_slots[0].last = 999;
    g_slots[1].id = 0; g_slots[1].last = 123;
    send_project(fixture, n, NULL);
    assert(!g_compatFault && g_outCount == 3 && strstr(g_outHead->msg, "project.load"));
    assert(strstr(g_outHead->next->msg, "plug.state") && g_outHead->next->blobn == n && !memcmp(g_outHead->next->blob, fixture, n));
    assert(strstr(g_outTail->msg, "state.full") && strstr(g_outTail->msg, "[32768,77]") && strstr(g_outTail->msg, "[0,123]") && !strstr(g_outTail->msg, "999"));
    while (g_outHead) { OutItem *next = g_outHead->next; free(g_outHead->msg); free(g_outHead->blob); free(g_outHead); g_outHead = next; }
    g_outTail = NULL; g_outCount = 0; g_outBytes = 0; g_outBusy = 0; g_ws = NULL; g_npend = 0;
    CloseHandle(g_outEvt); g_outEvt = NULL;
    plugins_dialog(NULL); assert(dialogs == 0 && g_qh == g_qt);
    dialog_result = IDRETRY;
    bar_proc(NULL, WM_KEYDOWN, VK_RETURN, 0);
    bar_proc(NULL, WM_CONTEXTMENU, 0, -1);
    assert(dialogs == 0 && g_qh == g_qt);
    g_qh = g_qt;
    strcpy_s(g_tmpDir, sizeof(g_tmpDir), temp);
    g_chU[0].retryStamp = plugin_retry_stamp(fixture, n, U_CH);
    plugins_retry(); assert(g_qh == g_qt && !dialogs); // An unchanged failed installation is never loaded again.
    g_pluginMissing = 1; g_compatReady = 1;
    plugins_auto_tick(10000); plugins_auto_tick(10001);
    assert(g_qh != g_qt && (g_qh + 1) % QCAP == g_qt && g_q[g_qh].type == T_PLUGRETRY);
    g_qh = g_qt;
    g_pluginCatalog = (PluginEntry *)calloc(1, sizeof(PluginEntry)); assert(g_pluginCatalog);
    memcpy(g_pluginCatalog[0].id, s.id, 16); g_pluginCatalog[0].unit = U_CH; g_pluginCatalog[0].format = 3;
    g_pluginCatalog[0].path = _strdup(first); g_pluginCatalogN = 1;
    assert(plugin_retry_stamp(fixture, n, U_CH) != g_chU[0].retryStamp);
    assert(DeleteFileA(first) && DeleteFileA(second));
    plug_agreed_reset(1, 1); plugin_catalog_clear();
    puts("Stable VST IDs/paths, opaque state, bounds, ambiguous installs, retained late-join state/control order, no retry dialog, debounced automatic checks and installation fingerprints: OK");
    return 0;
}
