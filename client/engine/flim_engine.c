// flim engine (native, injected into FL). v3: params + plugin state + channels.
//  - joins a named room on flim-server (first in = leader)
//  - DETECT: polls params through FL's ProcessRECEvent (f4e910, REC_GetValue)
//            and sends param.set ops keyed by REC event id. Watched: channel
//            vol/pan/on-off/route, channel plugin params, mixer vol/pan/stereo/mute,
//            mixer effect params; rescanned every few seconds for new channels/plugins
//  - PLUGINS: each channel / mixer FX slot is a "unit"; its full state (FL .fst via
//            FL's own preset saver) is sent on plugin add/replace, preset-sized param
//            bursts, or hash changes found by a slow sweep (e.g. Serum wavetables), and
//            applied with FL's preset loader. Channel add/delete are replayed too.
//  - JOIN: a member sends state.req; the leader answers with plug.full + state.full
//  - APPLY: inbound param.set is queued and executed on FL's main thread
//           (WH_GETMESSAGE hook) via ProcessRECEvent(REC_UpdateValue|REC_UpdateControl)
//  - LOCKS: a param is locked while we tweak it, released after a short idle;
//           params locked by a peer are snapped back if tweaked locally
//  - NET: WinHTTP WebSocket (ws:// or wss://). Not connected at start: the Connect
//         button in FL's toolbar asks for a server URL + password (remembered in
//         %APPDATA%\flim\flim.ini, see common/flim_config.h), with a status text beside it.
// Config: user name from flim.ini (set by flim.exe). Env overrides for dev/tests:
//         FLIM_NAME, FLIM_ROOM (default FLIM), FLIM_AUTOCONNECT=<url> (+ FLIM_PASSWORD) connects
//         at startup; legacy FLIM_SERVER=host:port means FLIM_AUTOCONNECT=ws://host:port.
// Logs to %TEMP%\flim_engine.log.

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <shlobj.h>
#include "../common/flim_config.h"
#include "../common/flim_res.h"
#include <bcrypt.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

#define FL_MODULE    L"FLEngine_x64.dll"
#define IMG_BASE     0x400000      // Ghidra image base; runtime addr = module + (VA - IMG_BASE)
#define REC_RVA      0xb4e910      // f4e910: ProcessRECEvent(eventId, value, flags)
#define MUTE_RVA     (0x1176be0 - IMG_BASE) // setTrackEnabled(track, enabled: 1/0, -1 toggle)
#define MIXARR_VA    0x1496718     // global: pointer to mixer track array (stride 0x1474)
#define MIXCNT_VA    0x14980b8     // global: pointer to mixer track count (int)
#define MAINFORM_VA  0x1497368     // global: pointer to FL main form pointer
#define MIX_STRIDE   0x1474
#define MIX_ENABLED  0x18          // byte: 1 = enabled (unmuted)

#define REC_UpdateValue   0x01
#define REC_GetValue      0x02
#define REC_UpdateControl 0x10
#define REC_NONE          0xffffffffu  // returned for a nonexistent target

#define MAX_CH       256
#define NSUB         4             // channel params: 0=vol 1=pan 7=on/off 8=mixer route
#define MAX_SLOTS    65536
#define HASH_SIZE    (MAX_SLOTS * 2)
#define MAX_PLUG_PARAMS 4096
#define MAX_MIX      512
#define MAX_FX_SLOTS 10
#define RESCAN_MS    2000

// REC id layout (FL ProcessRECEvent):
//   channel param        (ch << 16) | sub              sub: 0 vol, 1 pan, 7 on/off, 8 route
//   channel plugin param (ch << 16) | (0x8000 + p)
//   mixer track param    0x70000000 + (t << 22) + sub  sub: 0x1fc0 vol, 0x1fc1 pan, 0x1fc2 stereo
//   mixer effect param   0x70000000 + (((t << 6) | slot) << 16) + 0x8000 + p
// Mixer mute is not a REC param; it gets a pseudo id MUTE_ID(t) handled separately.
#define REC_PLUG_FIRST 0x8000
#define MIX_ID(t, sub)        (0x70000000u + ((uint32_t)(t) << 22) + (sub))
#define FX_ID(t, slot, p)     (0x70000000u + ((((uint32_t)(t) << 6) | (slot)) << 16) + REC_PLUG_FIRST + (p))
#define MUTE_ID(t)            (0xF0000000u | (uint32_t)(t))
#define IS_MUTE_ID(id)        (((id) & 0xFFF00000u) == 0xF0000000u)
#define POLL_MS      30
#define LOCK_IDLE_MS 600           // release a param lock after this much idle

typedef uint32_t (*rec_t)(uint32_t id, uint64_t value, uint32_t flags);

static const uint32_t SUBS[NSUB] = { 0, 1, 7, 8 };
static const uint32_t MIX_SUBS[] = { 0x1fc0, 0x1fc1, 0x1fc2 };

typedef void (*setenabled_t)(int track, int enabled);
static void retry_pending(void);
static void send_full_state(void);
static void send_notes_full(void);
static void notes_rebaseline(void);
static void proj_begin(const char *why);
static void clips_rebaseline(void);
static int  unit_before_send(const char *key, int isPat, int idx, const unsigned char *d, unsigned n);
static void unit_agreed(int isPat, int idx, const unsigned char *d, unsigned n);
static void unit_revert(const char *key);
static void units_capture_all(void);
static void hint_if_held(const char *key);
static int  closing_now(void);
static int b64val(char c);
static int  plug_lock_send(int unit, int a, int b, char *key, int cap);
static void plug_agreed_set(int unit, int a, int b, const unsigned char *d, size_t n);
static void plug_agreed_reset(int channels, int fx);
static void plug_revert_state(int unit, int a, int b);
static void plug_undo(const char *key, int apply);
static void meta_rebaseline(void);
static wchar_t *dstr_keep(const wchar_t *s);
static int int_after(const char *p, const char *end, const char *key, int *out);
static void ws_send(const char *msg);
static void ws_send_samples(char *msg, const unsigned char *blob, size_t n);
static void samples_rewrite(unsigned char **d, size_t *n);

typedef struct {
    uint32_t id;
    uint32_t last;          // last value seen/applied (REC_NONE = unknown/absent)
    volatile LONG gen;      // bumped on every apply; poller discards racing reads
    int  lockedByMe;
    DWORD lastTouch;        // tick of our last local change
    int  lockedByPeer;
    int  hasUndo;           // plugin param changed while its plugin lock was still requested:
    uint32_t undo;          //   value to restore if the lock is denied
} Slot;

static Slot   g_slots[MAX_SLOTS];
static volatile LONG g_nslot = 0;
static int    g_hash[HASH_SIZE];   // slot index + 1; 0 = empty
static setenabled_t g_setEnabled = NULL;
static char  *g_flBase = NULL;
static int    g_chanPlugN[MAX_CH];
static int    g_fxN[MAX_MIX][MAX_FX_SLOTS];
static volatile LONG g_isLeader = 0;
static rec_t  g_rec = NULL;
static HINTERNET g_ws = NULL;    // the open WebSocket (NULL = not connected); guarded by g_sendLock
static CRITICAL_SECTION g_sendLock, g_slotLock, g_qLock;
static char   g_myId[64];
static HWND   g_mainWnd = NULL;
static HHOOK  g_hook = NULL;
static HINSTANCE g_hinst = NULL;

// ---- plugin / structure sync (FL functions, Ghidra VAs) ----
#define SAVEPRESET_VA 0xe71f20     // SavePresetFile(UnicodeString file, mask, obj, flags) -> 0 ok
#define LOADPRESET_VA 0xe72110     // LoadPresetFile(UnicodeString file, mask, obj, 0) -> 0 ok (replaces plugin)
#define HASPLUGIN_VA  0x12204f0    // HasPlugin(obj) -> bool
#define GETPLUGIN_VA  0x1222030    // GetPlugin(obj, &intf) -> plugin type; intf is AddRef'd (must release!)
#define INTFCLEAR_VA  0x41a770     // Delphi _IntfClear(&intf): nil it and Release
#define ADDCHAN_VA    0xf1ceb0     // AddChannel(-1, 0, 1) -> new channel obj (appended)
#define DELCHAN_VA    0xf1d6c0     // DeleteChannel(index, 0, flags); flags 1|2 = no confirmation dialog
#define DELCHAN_FLAGS 3            // bit 2 = "already confirmed" (FL's menu sets it after its own dialog)
#define RESOLVER_VA   0xf1e370     // ResolveChannel(index, 0, &obj)
#define CHANLIST_VA   0x1498140    // global: pointer to channel list (items +8, count +0x10)
#define MIX_SLOTS_OFF 0x1324       // mixer track: 10 effect slot object pointers
#define MOVECHAN_VA   0xf4a780     // ReorderChannels(**MOVEOBJ, mode, **MOVEARG, flags): FL's Alt+Up/Down + sorts
#define MOVEOBJ_VA    0x1497460    //   first arg (pointer global, deref twice)
#define MOVEARG_VA    0x1499868    //   third arg (int, deref twice; -1 seen)
#define MOVE_UP       4            //   mode 4 = move selected up, 5 = down, 1..3 = sorts
#define MOVE_FLAGS    1            //   bit 1 = close undo group; bit 2 = take selection from the rack UI (not wanted)
#define CHANFILT_VA   0x14961d0    // global: channel list of the rack's current filter group (same layout)
#define CH_SELECTED   0x35c        // channel object: selected byte (what ReorderChannels moves)
// ---- notes (per pattern: a flat array of 24-byte records, same layout as FLP event 224) ----
#define PATARR_VA     0x1498920    // global: pointer to the static pattern table (stride 0xC0)
#define PAT_STRIDE    0xC0
#define PAT_NOTES     0x28         // pattern record: note array object (NULL until the pattern has had notes)
#define MAX_PAT       1000         // FL caps pattern numbers at 999
#define NOTES_DATA    0x08         // note array: record pointer
#define NOTES_COUNT   0x14         //             record count (u32); element size (0x18) at +0x10
#define NOTE_SIZE     24
#define NOTE_SEL_OFS  0x13         // byte: low bits = note color, bit 0x80 = SELECTED (client-only, never synced)
#define NOTE_SEL_BIT  0x80
#define NOTEARR_VA    0x11cb490    // NoteArray(pattern, create) -> note array object
#define NOTEDEL_VA    0xf665d0     // DeleteNote(arr, index, 0)
#define NOTEADD_VSLOT 1            // arr->vmt[1](arr, &rec): add a note (vmt[2] fills a default record)
#define NOTELOCK_VA   0x1497f30    // global: CRITICAL_SECTION* FL holds while editing notes (audio thread reads them)
#define GRIDREFRESH_VA 0x11d04f0   // RefreshGrid(channel->grid at +0x7bc)
#define CH_GRID       0x7bc
#define MASK_CHAN     32           // preset mask for channels (what FL's undo uses)
#define MASK_FX       48           // preset mask for mixer slots (32 faults on a slot)

// ---- project files (FL functions / globals, Ghidra VAs) ----
#define FLPROJ_VA     0x149a508    // global -> holder; *(holder + 0x10) = TFLProject object
#define FLPROJ_LOAD_SLOT (0x38 / 8) // TFLProject vmt +0x38: LoadFile(self, path, flags) -> bool; flags bit 0 = "save changes?" prompt
#define MAINOBJ_VA    0x1496fb8    // global -> var -> main form object (what FL's own open/save code passes)
#define FORM_BUSY     0x3344       // main form: busy object, non-NULL while an .flp load runs
#define SETPROJ_VA    0x10cb1e0    // SetProjectFile(form, path, path, addRecent): title, Ctrl+S target, recent list
#define SAVEPROJ_VA   0x10cdf80    // SaveProjectTo(path, 0) -> bool: FL's project writer; current name unchanged
#define BUSYBEGIN_VA  0x10c1e70    // FL's open handler wraps an .flp load in these two
#define BUSYEND_VA    0x10c1ec0
#define PROJNAME_VA   0x156f2a0    // UnicodeString global: current project file ('' = untitled)
#define WM_FL_OPENPROJ 0x566       // FL posts this to its main form to open a project (dialog, browser, drop)
// ---- playlist clips (current arrangement) ----
#define ARRS_VA       0x18209d8    // global -> arrangement pointer array (count as int64 at -8)
#define ARRCUR_VA     0x148d1dc    // int global: current arrangement index
#define ARR_CLIPS     0x14         // arrangement: clip array object (same array family as notes)
#define CLIPS_DATA    0x08
#define CLIPS_COUNT   0x14
#define CLIP_SIZE     0x50         // first 32 bytes = FLP event 233 record
#define CLIP_DEL_OFS  0x04         // -1 = deleted (then purge)
#define CLIP_SEL_OFS  0x13         // bit 0x80 = SELECTED (client-only)
#define CLIP_SEL_BIT  0x80
#define CLIP_ID_OFS   0x20         // per-instance clip id (FL renumbers on add)
#define CLIPADD_VSLOT   1          // arr->vmt[1](arr, &rec) -> index (kept sorted by position)
#define CLIPPURGE_VSLOT 3          // arr->vmt[3](arr): drop records marked deleted, free their resources
#define PLFORM_VA     0x14993e8    // global -> var -> playlist form
#define PLREFRESH_VA  0xd40250     // RefreshPlaylist(form): what FL's clip-properties dialog calls after an edit
#define HINT_VA       0x10e49f0    // ShowHint(form, UnicodeString text, 0): FL's hint bar (top left)
#define PROJ_FX_MASS  3            // this many effects gone in one scan = project close, not a delete

typedef int   (*presetfile_t)(const wchar_t *file, int mask, void *obj, int flags);
typedef char  (*hasplugin_t)(void *obj);
typedef void *(*getplugin_t)(void *obj, void **inst);
typedef void  (*intfclear_t)(void **intf);
typedef void *(*addchan_t)(int a, int b, int c);
typedef void  (*delchan_t)(unsigned idx, char force, uint64_t flags);
typedef int   (*resolver_t)(int idx, char useGlobal, void **out);
typedef void  (*movechan_t)(void *obj, uint8_t mode, int arg, uint64_t flags);
typedef void *(*notearr_t)(int pattern, char create);
typedef void  (*notedel_t)(void *arr, int index, char flag);
typedef void  (*noteadd_t)(void *arr, const void *rec);
typedef void  (*gridrefresh_t)(void *grid);
typedef char  (*projload_t)(void *self, const wchar_t *path, uint8_t flags);
typedef void  (*setproj_t)(void *form, const wchar_t *p1, const wchar_t *p2, uint8_t addRecent);
typedef char  (*saveproj_t)(const wchar_t *path, uint8_t flag);
typedef void  (*formfn_t)(void *form);
typedef int   (*clipadd_t)(void *arr, const void *rec);
typedef void  (*clippurge_t)(void *arr);
typedef void  (*hint_t)(void *form, const wchar_t *text, uint8_t flag);

static presetfile_t g_savePreset, g_loadPreset;
static hasplugin_t  g_hasPlugin;
static getplugin_t  g_getPlugin;
static intfclear_t g_intfClear;
static addchan_t    g_addChannel;
static delchan_t    g_delChannel;
static resolver_t   g_resolve;
static movechan_t   g_moveChans;
static notearr_t    g_noteArr;
static notedel_t    g_noteDel;
static gridrefresh_t g_gridRefresh;

// ---- main-thread task queue ----
// Param writes are cheap and run back to back; anything that calls into FL's
// preset/channel code is "heavy" and runs one per message-loop tick (a plugin
// often needs the message loop to run between load and the next call).
enum { T_PARAM, T_SNAP, T_LOAD, T_ADDCH, T_DELCH, T_STRUCT, T_FULL, T_ORDER, T_FXCLEAR, T_FXSYNC,
       T_NOTESNAP, T_NOTES, T_NOTESYNC, T_PROJLOAD, T_PROJDONE, T_PROJSNAP,
       T_CLIPSNAP, T_CLIPS, T_UNITREVERT, T_HINT, T_METASNAP, T_META };
enum { U_CH, U_FX };
enum { SNAP_BASELINE, SNAP_IF_DIFF, SNAP_FORCE, SNAP_ADD, SNAP_COLLECT };
#define QCAP 16384
typedef struct {
    int type;
    uint32_t id, v;          // T_PARAM
    int unit, a, b;          // unit kind + (channel) or (track, slot)
    int mode;                // T_SNAP mode / T_LOAD: 1 = skip if identical
    char *path;              // T_LOAD / T_ADDCH: inbound .fst file (owned; deleted after use)
    int *order;              // T_ORDER: [n, from0..from(n-1)]; T_FXSYNC/T_NOTESYNC: bitmap;
                             // T_NOTES: note records, it->mode = count (owned)
} QItem;
static void run_task_inner(const QItem *it);
static QItem g_q[QCAP];
static int g_qh = 0, g_qt = 0;

static void logmsg(const char *fmt, ...) {
    char path[MAX_PATH];
    GetTempPathA(MAX_PATH, path);
    strcat_s(path, MAX_PATH, "flim_engine.log");
    FILE *f = NULL;
    if (fopen_s(&f, path, "a") == 0 && f) {
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(f, "%02d:%02d:%02d.%03d [%lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId());
        va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
        fputc('\n', f); fclose(f);
    }
}

static unsigned hash_id(uint32_t id) { return (id * 2654435761u) % HASH_SIZE; }

static Slot *slot_by_id(uint32_t id) {
    Slot *r = NULL;
    unsigned h = hash_id(id);
    EnterCriticalSection(&g_slotLock);
    while (g_hash[h]) {
        if (g_slots[g_hash[h] - 1].id == id) { r = &g_slots[g_hash[h] - 1]; break; }
        h = (h + 1) % HASH_SIZE;
    }
    LeaveCriticalSection(&g_slotLock);
    return r;
}

static int mix_count(void) {
    int n;
    __try { n = **(int **)(g_flBase + (MIXCNT_VA - IMG_BASE)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return n < 0 ? 0 : (n > MAX_MIX ? MAX_MIX : n);
}

// ProcessRECEvent dereferences FL's main form (*(*0x1497368) + 0xf30) first thing;
// it is null until FL has finished creating its UI. Calling REC before that crashes FL.
static int fl_ready(void) {
    __try {
        char **pl = *(char ***)(g_flBase + (MAINFORM_VA - IMG_BASE));
        if (!pl || !*pl) return 0;
        return *(void **)(*pl + 0xf30) != NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Current value of any synced id (REC param or pseudo id). REC_NONE if absent.
// SEH-guarded so a bad moment inside FL (e.g. mid project load) can't take FL down.
static uint32_t read_value(uint32_t id) {
    __try {
        if (IS_MUTE_ID(id)) {
            int t = (int)(id & 0xFFFFF);
            if (t >= mix_count()) return REC_NONE;
            char *arr = *(char **)(g_flBase + (MIXARR_VA - IMG_BASE));
            return arr[(size_t)t * MIX_STRIDE + MIX_ENABLED] ? 1u : 0u;
        }
        return g_rec(id, 0, REC_GetValue);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return REC_NONE;
    }
}

// Must run on FL's main thread.
static uint32_t write_value(uint32_t id, uint32_t v) {
    __try {
        if (IS_MUTE_ID(id)) {
            g_setEnabled((int)(id & 0xFFFFF), v ? 1 : 0);
            return read_value(id);
        }
        return g_rec(id, v, REC_UpdateValue | REC_UpdateControl);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logmsg("APPLY faulted id=0x%08x", id);
        return REC_NONE;
    }
}

// Register an id for syncing; returns 1 if it exists in FL (value != REC_NONE).
static int add_slot(uint32_t id) {
    if (slot_by_id(id)) return 1;
    uint32_t v = read_value(id);
    if (v == REC_NONE) return 0;
    EnterCriticalSection(&g_slotLock);
    if (g_nslot < MAX_SLOTS) {
        Slot *s = &g_slots[g_nslot];
        memset(s, 0, sizeof(*s));
        s->id = id;
        s->last = v;
        unsigned h = hash_id(id);
        while (g_hash[h]) h = (h + 1) % HASH_SIZE;
        g_hash[h] = g_nslot + 1;
        InterlockedIncrement(&g_nslot);   // publish after the slot is filled
    }
    LeaveCriticalSection(&g_slotLock);
    return 1;
}

// Discover channels, plugins and mixer slots; extend param ranges that grew.
// Cheap enough to run every few seconds (only probes past known ranges).
static void rescan(void) {
    LONG before = g_nslot;
    int c, k, t, sl;
    for (c = 0; c < MAX_CH; c++) {
        if (read_value((uint32_t)c << 16) == REC_NONE) continue;   // no such channel
        for (k = 0; k < NSUB; k++) add_slot(((uint32_t)c << 16) | SUBS[k]);
        while (g_chanPlugN[c] < MAX_PLUG_PARAMS &&
               add_slot(((uint32_t)c << 16) | (REC_PLUG_FIRST + g_chanPlugN[c])))
            g_chanPlugN[c]++;
    }
    int nm = mix_count();
    for (t = 0; t < nm; t++) {
        for (k = 0; k < (int)(sizeof(MIX_SUBS) / sizeof(MIX_SUBS[0])); k++) add_slot(MIX_ID(t, MIX_SUBS[k]));
        add_slot(MUTE_ID(t));
        for (sl = 0; sl < MAX_FX_SLOTS; sl++)
            while (g_fxN[t][sl] < MAX_PLUG_PARAMS && add_slot(FX_ID(t, sl, g_fxN[t][sl])))
                g_fxN[t][sl]++;
    }
    if (g_nslot != before)
        logmsg("rescan: watching %ld params (+%ld), mixer tracks=%d", g_nslot, g_nslot - before, nm);
}

// ---------------- websocket ----------------
static void b64enc(const unsigned char *in, int n, char *out) {
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, o = 0;
    for (i = 0; i < n; i += 3) {
        int a = in[i], b = (i + 1 < n) ? in[i + 1] : 0, c = (i + 2 < n) ? in[i + 2] : 0;
        out[o++] = T[a >> 2];
        out[o++] = T[((a & 3) << 4) | (b >> 4)];
        out[o++] = (i + 1 < n) ? T[((b & 15) << 2) | (c >> 6)] : '=';
        out[o++] = (i + 2 < n) ? T[c & 63] : '=';
    }
    out[o] = 0;
}

// One text message, now. WinHTTP allows one send at a time per socket: g_sendLock serializes.
// Everything else calls ws_send (samples section), which keeps ops behind ones waiting for samples.
static void ws_send_now(const char *msg) {
    DWORD e = 0;
    EnterCriticalSection(&g_sendLock);
    if (g_ws) e = WinHttpWebSocketSend(g_ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)msg, (DWORD)strlen(msg));
    LeaveCriticalSection(&g_sendLock);
    if (e) logmsg("send failed (%lu)", e);
}

// Server address as typed in the Connect dialog:
//   wss://host[:port][/path], ws://..., https:// / http:// (same as wss / ws), or host[:port].
// A bare host means wss on 443; localhost, 127.x or an explicit non-443 port mean plain ws.
typedef struct { int secure; wchar_t host[256]; INTERNET_PORT port; wchar_t path[512]; char shown[300]; } NetUrl;

static int parse_url(const char *in, NetUrl *u) {
    char buf[800], pathA[512] = "/", *p, *h, *slash, *colon;
    int scheme = -1;                                    // 1 = TLS, 0 = plain, -1 = not given
    size_t n;
    memset(u, 0, sizeof(*u));
    while (*in == ' ' || *in == '\t') in++;
    strncpy_s(buf, sizeof(buf), in, _TRUNCATE);
    n = strlen(buf);
    while (n && (unsigned char)buf[n - 1] <= ' ') buf[--n] = 0;
    p = buf;
    if (_strnicmp(p, "wss://", 6) == 0) { scheme = 1; p += 6; }
    else if (_strnicmp(p, "https://", 8) == 0) { scheme = 1; p += 8; }
    else if (_strnicmp(p, "ws://", 5) == 0) { scheme = 0; p += 5; }
    else if (_strnicmp(p, "http://", 7) == 0) { scheme = 0; p += 7; }
    else if (strstr(p, "://")) return 0;
    h = p;
    slash = strchr(h, '/');
    if (slash) { strncpy_s(pathA, sizeof(pathA), slash, _TRUNCATE); *slash = 0; }
    colon = strrchr(h, ':');
    if (colon) {
        char *e; long port = strtol(colon + 1, &e, 10);
        if (*e || port <= 0 || port > 65535) return 0;
        u->port = (INTERNET_PORT)port; *colon = 0;
    }
    if (!*h) return 0;
    for (p = h; *p; p++) if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '-' || *p == '_')) return 0;
    if (scheme < 0) {
        int local = _stricmp(h, "localhost") == 0 || strncmp(h, "127.", 4) == 0;
        scheme = ((u->port && u->port != 443) || local) ? 0 : 1;
        if (local && !u->port) u->port = 8080;
    }
    u->secure = scheme;
    if (!u->port) u->port = scheme ? 443 : 80;
    MultiByteToWideChar(CP_UTF8, 0, h, -1, u->host, 256);
    MultiByteToWideChar(CP_UTF8, 0, pathA, -1, u->path, 512);
    strcpy_s(u->shown, sizeof(u->shown), h);              // status text: host[:port][/path]
    if (u->port != (scheme ? 443 : 80)) sprintf_s(u->shown + strlen(u->shown), sizeof(u->shown) - strlen(u->shown), ":%u", u->port);
    if (pathA[1]) strncat_s(u->shown, sizeof(u->shown), pathA, _TRUNCATE);
    return 1;
}

typedef struct { HINTERNET ses, con, ws; } NetConn;

static void net_close_handles(NetConn *c) {
    if (c->ws) WinHttpCloseHandle(c->ws);
    if (c->con) WinHttpCloseHandle(c->con);
    if (c->ses) WinHttpCloseHandle(c->ses);
    memset(c, 0, sizeof(*c));
}

static void net_error_text(DWORD e, const NetUrl *u, char *out, int cap) {
    switch (e) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: strcpy_s(out, cap, "server not found (check the URL)"); break;
    case ERROR_WINHTTP_CANNOT_CONNECT:    strcpy_s(out, cap, "nothing answers at that address"); break;
    case ERROR_WINHTTP_TIMEOUT:           strcpy_s(out, cap, "timed out"); break;
    case ERROR_WINHTTP_SECURE_FAILURE:
        strcpy_s(out, cap, u->secure ? "secure connection failed (a local server needs ws://)" : "secure connection failed");
        break;
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
        strcpy_s(out, cap, u->secure ? "bad server response" : "bad server response (try wss://)");
        break;
    default: sprintf_s(out, cap, "network error %lu", e);
    }
}

// Open a WebSocket to the server (blocking). Returns 1 with c->ws set, or 0 + err.
static int ws_open(const NetUrl *u, NetConn *c, char *err, int errcap) {
    HINTERNET req = NULL;
    DWORD status = 0, sl = sizeof(status);
    memset(c, 0, sizeof(*c));
    c->ses = WinHttpOpen(L"flim/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!c->ses)   // before Windows 8.1
        c->ses = WinHttpOpen(L"flim/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!c->ses) goto fail;
    WinHttpSetTimeouts(c->ses, 0, 15000, 30000, 30000);          // resolve, connect, send, handshake reply
    c->con = WinHttpConnect(c->ses, u->host, u->port, 0);
    if (!c->con) goto fail;
    req = WinHttpOpenRequest(c->con, L"GET", u->path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             u->secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) goto fail;
    if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) goto fail;
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)) goto fail;
    if (!WinHttpReceiveResponse(req, NULL)) goto fail;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &sl, WINHTTP_NO_HEADER_INDEX);
    if (status != 101) { sprintf_s(err, errcap, "not a flim server (HTTP %lu)", status); goto out; }
    c->ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    if (!c->ws) goto fail;
    WinHttpCloseHandle(req);      // idle connections are fine: the server pings every 15 s
    return 1;
fail:
    net_error_text(GetLastError(), u, err, errcap);
out:
    if (req) WinHttpCloseHandle(req);
    net_close_handles(c);
    return 0;
}

// ---------------- main-thread task queue ----------------
static void enqueue(const QItem *it) {
    EnterCriticalSection(&g_qLock);
    if (it->type == T_LOAD) {
        // a newer state for a unit that still has a load queued replaces it (latest wins)
        int i;
        for (i = g_qh; i != g_qt; i = (i + 1) % QCAP) {
            QItem *q = &g_q[i];
            if (q->type == T_LOAD && q->unit == it->unit && q->a == it->a && q->b == it->b) {
                DeleteFileA(q->path); free(q->path);
                q->path = it->path; q->mode = it->mode;
                LeaveCriticalSection(&g_qLock);
                logmsg("LOAD coalesced unit=%d %d/%d", it->unit, it->a, it->b);
                return;
            }
        }
    }
    int nt = (g_qt + 1) % QCAP;
    if (nt != g_qh) { g_q[g_qt] = *it; g_qt = nt; }
    else logmsg("queue full, dropped task type %d", it->type);
    LeaveCriticalSection(&g_qLock);
    if (g_mainWnd) PostMessageW(g_mainWnd, WM_NULL, 0, 0); // wake the main thread
}

static void enqueue_apply(uint32_t id, uint32_t v) {
    QItem it; memset(&it, 0, sizeof(it));
    it.type = T_PARAM; it.id = id; it.v = v;
    enqueue(&it);
}

static void enqueue_unit(int type, int unit, int a, int b, int mode, char *path) {
    QItem it; memset(&it, 0, sizeof(it));
    it.type = type; it.unit = unit; it.a = a; it.b = b; it.mode = mode; it.path = path;
    enqueue(&it);
}

// ---------------- plugin / structure sync ----------------
// A "unit" is one channel or one mixer effect slot. Its full plugin state
// (identity + preset/chunk) travels as an .fst produced by FL's own preset
// saver and is applied with FL's preset loader on the other side.
typedef struct {
    void *inst;              // plugin instance seen last (identity; NULL = none)
    uint64_t hash;           // baseline state hash (0 = unknown)
    DWORD lastAct;           // last local param activity (0 = idle)
    int burst;               // a preset-sized param burst happened in this activity
    DWORD identAt;           // identity changed locally at (send after settle)
    DWORD addAt;             // channel appended locally at (send chan.add after settle)
    DWORD suppressUntil;     // remote state being applied: ignore local param changes
    DWORD absorbUntil;       // identity changes until then come from our own apply
    DWORD baselineAt;        // take a silent baseline at this tick
    DWORD baseAt;            // when the current hash baseline was taken
} Unit;

#define BURST_PARAMS     8       // params of one unit changing in one poll tick = preset load
#define SETTLE_ACT_MS    1000    // quiet time after param activity before re-hashing
#define SETTLE_IDENT_MS  1500    // let a new/replaced plugin finish loading before saving it
#define SUPPRESS_MS      600     // after applying remote state: late async param updates from the plugin
#define ABSORB_MS        2000    // identity changes this soon after our own apply are ours
#define REBASE_MS        1500    // silent re-baseline of a unit this long after a remote apply
#define INPUT_RECENT_MS  5000    // "someone is using FL": gate for sweeps and state sends
#define SWEEP_MS         700     // one round-robin state check per this interval
#define STRUCT_MS        500     // channel list / plugin identity scan interval

static Unit g_chU[MAX_CH];
static Unit g_fxU[MAX_MIX][MAX_FX_SLOTS];
static void *g_chKnown[MAX_CH];
static int   g_nChKnown = -1;
static volatile LONG g_structGen = 0;     // bumped when channel indices shift
static volatile DWORD g_lastInput = 0;    // last local mouse/keyboard input (main thread queue)
static char  g_tmpDir[MAX_PATH];
static char  g_fxDeletePreset[MAX_PATH];  // <FL>\Data\System\(delete) effect.fst: what FL's own Delete loads into a slot
static volatile LONG g_tmpSeq = 0;

static Unit *unit_ptr(int unit, int a, int b) {
    if (unit == U_CH) return (a >= 0 && a < MAX_CH) ? &g_chU[a] : NULL;
    return (a >= 0 && a < MAX_MIX && b >= 0 && b < MAX_FX_SLOTS) ? &g_fxU[a][b] : NULL;
}

// Unit a REC id belongs to (for activity tracking); returns 0 if none.
static int unit_of_id(uint32_t id, int *unit, int *a, int *b) {
    if (IS_MUTE_ID(id)) return 0;
    if (id < 0x70000000u) { *unit = U_CH; *a = (int)(id >> 16); *b = 0; return *a < MAX_CH; }
    if ((id & 0xFFFF) < REC_PLUG_FIRST) return 0;            // mixer track vol/pan/stereo
    uint32_t ts = (id - 0x70000000u) >> 16;
    *unit = U_FX; *a = (int)(ts >> 6); *b = (int)(ts & 63);
    return *a < MAX_MIX && *b < MAX_FX_SLOTS;
}

// Delphi x64 UnicodeString with refcount -1 (a "literal"): FL copies rather than
// references it, so we can free it after the call.
static wchar_t *dstr_new(const char *s) {
    int n = (int)strlen(s), i;
    unsigned char *m = (unsigned char *)calloc(1, 16 + (n + 1) * 2);
    *(uint16_t *)(m + 4) = 1200; *(uint16_t *)(m + 6) = 2;
    *(int32_t *)(m + 8) = -1;    *(int32_t *)(m + 12) = n;
    wchar_t *w = (wchar_t *)(m + 16);
    for (i = 0; i < n; i++) w[i] = (unsigned char)s[i];
    return w;
}
static void dstr_free(wchar_t *w) { if (w) free((unsigned char *)w - 16); }

static void *chan_obj(int i) {
    void *o = NULL;
    __try { if (!g_resolve(i, 0, &o)) o = NULL; } __except (EXCEPTION_EXECUTE_HANDLER) { o = NULL; }
    return o;
}
static void *fx_obj(int t, int s) {
    __try {
        char *arr = *(char **)(g_flBase + (MIXARR_VA - IMG_BASE));
        return *(void **)(arr + (size_t)t * MIX_STRIDE + MIX_SLOTS_OFF + (size_t)s * 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}
static void *unit_obj(int unit, int a, int b) { return unit == U_CH ? chan_obj(a) : fx_obj(a, b); }
static int   unit_mask(int unit) { return unit == U_CH ? MASK_CHAN : MASK_FX; }

// Identity token of the plugin in obj (NULL = none). GetPlugin hands out an
// AddRef'd interface; it must be released or the plugin can never be destroyed
// (deleting its channel then hangs inside the plugin's teardown).
static void *plugin_inst(void *obj) {
    void *intf = NULL, *id = NULL;
    if (!obj) return NULL;
    __try {
        if (g_hasPlugin(obj)) g_getPlugin(obj, &intf);
        id = intf;
        if (intf) g_intfClear(&intf);
    } __except (EXCEPTION_EXECUTE_HANDLER) { id = NULL; }
    return id;
}

static int chan_list(void ***items) {
    __try {
        char *list = **(char ***)(g_flBase + (CHANLIST_VA - IMG_BASE));
        *items = *(void ***)(list + 8);
        int n = *(int *)(list + 0x10);
        return n < 0 ? 0 : (n > MAX_CH ? MAX_CH : n);
    } __except (EXCEPTION_EXECUTE_HANDLER) { *items = NULL; return 0; }
}

static int chan_list_filtered(void ***items) {
    __try {
        char *list = **(char ***)(g_flBase + (CHANFILT_VA - IMG_BASE));
        *items = *(void ***)(list + 8);
        int n = *(int *)(list + 0x10);
        return n < 0 ? 0 : (n > MAX_CH ? MAX_CH : n);
    } __except (EXCEPTION_EXECUTE_HANDLER) { *items = NULL; return 0; }
}

static uint64_t fnv64(const unsigned char *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h ? h : 1;
}

// Hash of a saved .fst, skipping the 14-byte "FLhd" header: it embeds the
// project's channel count, which would make every unit look changed on add/delete.
static uint64_t state_hash(const unsigned char *d, size_t n) {
    if (n > 14 && memcmp(d, "FLhd", 4) == 0) return fnv64(d + 14, n - 14);
    return fnv64(d, n);
}

static unsigned char *read_file(const char *path, size_t *n) {
    FILE *f = NULL;
    if (fopen_s(&f, path, "rb") != 0 || !f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc(sz > 0 ? sz : 1);
    *n = fread(buf, 1, sz > 0 ? sz : 0, f);
    fclose(f);
    return buf;
}

static char *tmp_path(const char *tag) {
    char *p = (char *)malloc(MAX_PATH);
    sprintf_s(p, MAX_PATH, "%s\\%s_%ld.fst", g_tmpDir, tag, InterlockedIncrement(&g_tmpSeq));
    return p;
}

// Save a unit's full state with FL's preset saver. Main thread only.
// Returns malloc'd bytes (caller frees) or NULL (empty slot / failure).
static unsigned char *unit_save(int unit, int a, int b, size_t *n) {
    void *obj = unit_obj(unit, a, b);
    if (!obj) return NULL;
    if (unit == U_FX && !plugin_inst(obj)) return NULL;     // never save an empty slot
    char path[MAX_PATH];
    sprintf_s(path, sizeof(path), "%s\\snap.fst", g_tmpDir);
    DeleteFileA(path);
    wchar_t *w = dstr_new(path);
    int r = -1;
    __try { r = g_savePreset(w, unit_mask(unit), obj, 1); }
    __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("SAVE faulted unit=%d %d/%d", unit, a, b); r = -1; }
    dstr_free(w);
    if (r != 0) return NULL;
    return read_file(path, n);
}

static void b64enc(const unsigned char *in, int n, char *out);

// Serialize {"unit":"ch","ch":i | "unit":"fx","trk":t,"slot":s, "data":"<b64>"} fields.
static char *unit_json_fields(int unit, int a, int b, const unsigned char *data, size_t n) {
    size_t cap = 96 + (n + 2) / 3 * 4 + 1;
    char *j = (char *)malloc(cap);
    int o = unit == U_CH ? sprintf_s(j, cap, "\"unit\":\"ch\",\"ch\":%d,\"data\":\"", a)
                         : sprintf_s(j, cap, "\"unit\":\"fx\",\"trk\":%d,\"slot\":%d,\"data\":\"", a, b);
    b64enc(data, (int)n, j + o);
    strcat_s(j, cap, "\"");
    return j;
}

// key: the plugin lock the op is sent under (NULL = none); the server drops it if a peer holds it.
static void send_unit(const char *kind, int unit, int a, int b, const unsigned char *data, size_t n, const char *key) {
    char *f = unit_json_fields(unit, a, b, data, n);
    size_t cap = strlen(f) + 128;
    char *m = (char *)malloc(cap);
    if (key) sprintf_s(m, cap, "{\"t\":\"op\",\"op\":{\"kind\":\"%s\",\"key\":\"%s\",%s}}", kind, key, f);
    else sprintf_s(m, cap, "{\"t\":\"op\",\"op\":{\"kind\":\"%s\",%s}}", kind, f);
    ws_send_samples(m, data, n);         // lists (and uploads) the samples the state uses
    free(f);
    logmsg("PLUG sent %s unit=%s %d/%d (%zu bytes)", kind, unit == U_CH ? "ch" : "fx", a, b, n);
}

static void do_snap(const QItem *it) {
    Unit *u = unit_ptr(it->unit, it->a, it->b);
    size_t n = 0;
    if (!u) return;
    unsigned char *d = unit_save(it->unit, it->a, it->b, &n);
    if (!d) return;
    uint64_t h = state_hash(d, n);
    DWORD now = GetTickCount();
    // Provenance: a state goes out only if the local user did something since this
    // unit's baseline. Some plugins (Serum) never save byte-identical state twice;
    // without this gate that drift ping-pongs full reloads between the peers.
    int userSince = g_lastInput && (LONG)(g_lastInput - u->baseAt) > 0;
    int send = 0;
    char key[32];
    switch (it->mode) {
    case SNAP_IF_DIFF:
        if (u->hash && h != u->hash) {
            if (userSince) send = 1;
            else logmsg("PLUG drift absorbed unit=%s %d/%d (no local input)", it->unit == U_CH ? "ch" : "fx", it->a, it->b);
        }
        break;
    case SNAP_FORCE:   send = 1; break;   // identity changed: always
    case SNAP_ADD:     send_unit("chan.add", it->unit, it->a, it->b, d, n, NULL); break;
    default: break;
    }
    if (send) {
        if (!plug_lock_send(it->unit, it->a, it->b, key, sizeof(key))) {
            // someone else is editing this plugin: put back the state everyone agreed on
            free(d);
            plug_revert_state(it->unit, it->a, it->b);
            return;
        }
        send_unit("plug.state", it->unit, it->a, it->b, d, n, key);
    }
    plug_agreed_set(it->unit, it->a, it->b, d, n);
    u->hash = h;
    u->baseAt = now;
    free(d);
}

static void do_load(const QItem *it) {
    Unit *u = unit_ptr(it->unit, it->a, it->b);
    void *obj = unit_obj(it->unit, it->a, it->b);
    if (!u || !obj) { logmsg("LOAD: no target unit=%d %d/%d", it->unit, it->a, it->b); return; }
    size_t n = 0;
    unsigned char *in = read_file(it->path, &n);
    uint64_t hin = in ? state_hash(in, n) : 0;
    if (in) plug_agreed_set(it->unit, it->a, it->b, in, n);   // what a plugin-lock revert restores
    free(in);
    if (it->mode == 1) {                         // join: skip units that are already identical
        size_t m = 0;
        unsigned char *own = unit_save(it->unit, it->a, it->b, &m);
        uint64_t hown = own ? state_hash(own, m) : 0;
        free(own);
        if (hown && hown == hin) { u->hash = hown; return; }
    }
    u->identAt = 0; u->lastAct = 0; u->burst = 0;
    wchar_t *w = dstr_new(it->path);
    int r = -1;
    __try { r = g_loadPreset(w, unit_mask(it->unit), obj, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("LOAD faulted unit=%d %d/%d", it->unit, it->a, it->b); }
    dstr_free(w);
    u->inst = plugin_inst(obj);                 // ours: the structure scan must not report it back
    // The load moved this unit's params; take them as the new truth right away
    // (instead of ignoring local edits for seconds), plus a short window for
    // plugins that update params asynchronously after a state load.
    {
        int i, k = 0, un, ua, ub;
        for (i = 0; i < g_nslot; i++) {
            Slot *s = &g_slots[i];
            if (!unit_of_id(s->id, &un, &ua, &ub) || un != it->unit || ua != it->a || ub != it->b) continue;
            EnterCriticalSection(&g_slotLock);
            s->last = read_value(s->id);
            InterlockedIncrement(&s->gen);
            LeaveCriticalSection(&g_slotLock);
            k++;
        }
        DWORD now = GetTickCount();
        u->suppressUntil = now + SUPPRESS_MS;
        u->absorbUntil = now + ABSORB_MS;
        u->baseAt = now;
        u->baselineAt = now + REBASE_MS;
        (void)k;
    }
    logmsg("PLUG applied unit=%s %d/%d ret=%d", it->unit == U_CH ? "ch" : "fx", it->a, it->b, r);
}

static void units_shift_after_delete(int idx, int n) {
    int i;
    for (i = idx; i + 1 < n && i + 1 < MAX_CH; i++) g_chU[i] = g_chU[i + 1];
    if (n - 1 >= 0 && n - 1 < MAX_CH) memset(&g_chU[n - 1], 0, sizeof(Unit));
    for (i = idx; i < MAX_CH; i++) {
        g_chanPlugN[i] = 0;          // param ranges re-probed by rescan
        g_chU[i].hash = 0;           // saved state is index-dependent: re-baseline, don't send
    }
    plug_agreed_reset(1, 0);
    InterlockedIncrement(&g_structGen);
}

static void known_from_list(void) {
    void **items; int n = chan_list(&items), i;
    for (i = 0; i < n; i++) g_chKnown[i] = items[i];
    g_nChKnown = n;
}

static void do_addch(const QItem *it) {
    void **items; int n = chan_list(&items);
    if (n != it->a) { logmsg("chan.add: expected index %d but have %d channels; skipped", it->a, n); return; }
    void *obj = NULL;
    __try { obj = g_addChannel(-1, 0, 1); } __except (EXCEPTION_EXECUTE_HANDLER) { obj = NULL; }
    if (!obj) { logmsg("chan.add: AddChannel failed"); return; }
    known_from_list();
    memset(&g_chU[it->a], 0, sizeof(Unit));
    g_chU[it->a].absorbUntil = GetTickCount() + SUPPRESS_MS + SETTLE_IDENT_MS;
    logmsg("CHAN added index %d (remote)", it->a);
    enqueue_unit(T_LOAD, U_CH, it->a, 0, 0, it->path);    // load its plugin on a later tick
}

static void do_delch(const QItem *it) {
    void **items; int n = chan_list(&items);
    if (it->a < 0 || it->a >= n) { logmsg("chan.del: index %d out of range (%d)", it->a, n); return; }
    __try { g_delChannel((unsigned)it->a, 0, DELCHAN_FLAGS); } __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("chan.del faulted"); }
    units_shift_after_delete(it->a, n);
    known_from_list();
    notes_rebaseline();
    logmsg("CHAN deleted index %d (remote)", it->a);
}

// Channels changed places: carry each channel's unit bookkeeping (plugin identity etc.)
// along to its new index. Saved channel state embeds the index, so moved channels
// re-baseline their hash silently; their param ranges are re-probed and every param
// is re-read without being sent (g_structGen) -- the values moved with the channels.
static void units_permute(const int *from, int n) {
    static Unit old[MAX_CH];
    int i;
    memcpy(old, g_chU, sizeof(Unit) * (size_t)n);
    for (i = 0; i < n; i++) {
        g_chU[i] = old[from[i]];
        if (from[i] != i) {
            g_chU[i].hash = 0; g_chU[i].identAt = 0; g_chU[i].lastAct = 0; g_chU[i].burst = 0;
            g_chanPlugN[i] = 0;
        }
    }
    plug_agreed_reset(1, 0);
    InterlockedIncrement(&g_structGen);
}

// Remove the effect from a mixer slot the way FL's slot menu "Delete" does: load the
// "(delete) effect" preset into it. Main thread.
static void do_fxclear(const QItem *it) {
    Unit *u = unit_ptr(U_FX, it->a, it->b);
    void *obj = fx_obj(it->a, it->b);
    if (!u || !obj || !plugin_inst(obj)) return;              // already empty
    wchar_t *w = dstr_new(g_fxDeletePreset);
    int r = -1;
    __try { r = g_loadPreset(w, MASK_FX, obj, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("FX clear faulted %d/%d", it->a, it->b); }
    dstr_free(w);
    memset(u, 0, sizeof(Unit));
    plug_agreed_set(U_FX, it->a, it->b, NULL, 0);
    u->inst = plugin_inst(obj);
    u->absorbUntil = GetTickCount() + ABSORB_MS;
    logmsg("PLUG cleared fx %d/%d (remote) ret=%d%s", it->a, it->b, r, u->inst ? " BUT SLOT STILL OCCUPIED" : "");
}

// Join: slots the leader has empty (absent from plug.full) must be empty here too.
static void do_fxsync(const QItem *it) {
    const unsigned char *keep = (const unsigned char *)it->order;
    int t, s, n = 0, nm = mix_count();
    for (t = 0; t < nm; t++)
        for (s = 0; s < MAX_FX_SLOTS; s++) {
            if (keep[t * MAX_FX_SLOTS + s] || !plugin_inst(fx_obj(t, s))) continue;
            enqueue_unit(T_FXCLEAR, U_FX, t, s, 0, NULL);
            n++;
        }
    if (n) logmsg("PLUG join: clearing %d effect slots the leader has empty", n);
}

static int index_of(void **list, int n, void *o) {
    int i;
    for (i = 0; i < n; i++) if (list[i] == o) return i;
    return -1;
}

// Remote reorder: rebuild the peer's order with FL's own "move selected up", one
// channel at a time (select only it, move, repeat), then restore our selection.
// Works on the rack's filter-group list like FL does; channels outside the group
// must stay where they are.
static void do_order(const QItem *it) {
    int n = it->order[0], i, j, steps = 0;
    const int *from = it->order + 1;
    void **items, **filt;
    static void *M[MAX_CH], *T[MAX_CH], *D[MAX_CH];
    static unsigned char sel[MAX_CH];
    int nm = chan_list(&items);
    if (!items || nm != n || g_nChKnown != n) { logmsg("chan.order: have %d channels (known %d), peer %d; skipped", nm, g_nChKnown, n); return; }
    {
        unsigned char seen[MAX_CH] = { 0 };
        for (i = 0; i < n; i++) {
            if (from[i] < 0 || from[i] >= n || seen[from[i]]) { logmsg("chan.order: not a permutation; skipped"); return; }
            seen[from[i]] = 1;
        }
    }
    for (i = 0; i < n; i++) { M[i] = items[i]; T[i] = items[from[i]]; }
    int nf = chan_list_filtered(&filt), nd = 0;
    if (!filt) return;
    for (i = 0; i < n; i++) {
        int inF = index_of(filt, nf, M[i]) >= 0;
        if (!inF && T[i] != M[i]) { logmsg("chan.order: channel %d is outside the rack's filter group; skipped", i); return; }
        if (index_of(filt, nf, T[i]) >= 0) D[nd++] = T[i];
    }
    if (nd != nf) { logmsg("chan.order: filter list mismatch (%d vs %d); skipped", nd, nf); return; }
    void *obj = NULL; int arg = -1;
    __try {
        obj = **(void ***)(g_flBase + (MOVEOBJ_VA - IMG_BASE));
        arg = **(int **)(g_flBase + (MOVEARG_VA - IMG_BASE));
        for (i = 0; i < n; i++) sel[i] = *((unsigned char *)M[i] + CH_SELECTED);
        for (i = 0; i < nd; i++) {
            nf = chan_list_filtered(&filt);
            j = index_of(filt, nf, D[i]);
            while (j > i) {
                int k, j2;
                for (k = 0; k < nf; k++) *((unsigned char *)filt[k] + CH_SELECTED) = (unsigned char)(k == j);
                g_moveChans(obj, MOVE_UP, arg, MOVE_FLAGS);
                steps++;
                nf = chan_list_filtered(&filt);
                j2 = index_of(filt, nf, D[i]);
                if (j2 != j - 1) { logmsg("chan.order: move did not take (%d -> %d); stopping", j, j2); i = nd; break; }
                j = j2;
            }
        }
        for (i = 0; i < n; i++) *((unsigned char *)M[i] + CH_SELECTED) = sel[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("chan.order faulted after %d moves", steps); }
    nm = chan_list(&items);
    int ok = nm == n;
    for (i = 0; ok && i < n; i++) ok = items[i] == T[i];
    if (ok) units_permute(from, n);
    else {
        // out of step with the peer's order: keep our bookkeeping consistent with what FL has
        for (i = 0; i < nm; i++) { g_chU[i].hash = 0; g_chU[i].inst = plugin_inst(items[i]); g_chanPlugN[i] = 0; }
        plug_agreed_reset(1, 0);
        InterlockedIncrement(&g_structGen);
    }
    known_from_list();
    notes_rebaseline();
    logmsg("CHAN reordered (remote): %d moves, %s", steps, ok ? "order matches" : "ORDER MISMATCH");
}

// Compare FL's channel list and plugin instances with what we knew.
static void do_struct(void) {
    void **items; int n = chan_list(&items), i, t, s;
    DWORD now = GetTickCount();
    if (!items) return;
    if (g_nChKnown < 0) {
        known_from_list();
        for (i = 0; i < n; i++) g_chU[i].inst = plugin_inst(items[i]);
        for (t = 0; t < mix_count(); t++)
            for (s = 0; s < MAX_FX_SLOTS; s++) g_fxU[t][s].inst = plugin_inst(fx_obj(t, s));
        return;
    }
    if (g_nChKnown > 0) {
        // Every known channel gone (project load/new/close): not a series of deletes.
        int k, j, common = 0;
        for (k = 0; k < g_nChKnown && !common; k++)
            for (j = 0; j < n; j++) if (items[j] == g_chKnown[k]) { common = 1; break; }
        if (!common) { proj_begin("channel list replaced"); return; }
    }
    if (closing_now()) return;
    int prefixSame = 1;
    for (i = 0; i < n && i < g_nChKnown; i++) if (items[i] != g_chKnown[i]) { prefixSame = 0; break; }
    if (n > g_nChKnown && prefixSame) {
        for (i = g_nChKnown; i < n; i++) {
            memset(&g_chU[i], 0, sizeof(Unit));
            g_chU[i].addAt = now;
            logmsg("CHAN appended locally: index %d", i);
        }
        known_from_list();
    } else if (n != g_nChKnown || !prefixSame) {
        // find deleted channels (known objects no longer in the list), highest index first
        int k, j, deleted = 0;
        for (k = g_nChKnown - 1; k >= 0; k--) {
            for (j = 0; j < n && items[j] != g_chKnown[k]; j++) {}
            if (j == n) {
                char m[96];
                sprintf_s(m, sizeof(m), "{\"t\":\"op\",\"op\":{\"kind\":\"chan.del\",\"ch\":%d}}", k);
                ws_send(m);
                logmsg("CHAN deleted locally: index %d (sent)", k);
                units_shift_after_delete(k, g_nChKnown - deleted);
                deleted++;
            }
        }
        if (!deleted && n == g_nChKnown) {
            // Reorder (move up/down, sort): send the permutation; the peer replays it
            // with FL's own move, so channels keep their state and params as they travel.
            int from[MAX_CH], moved = 0, ok = 1;
            char m[64 + MAX_CH * 5];
            int len = sprintf_s(m, sizeof(m), "{\"t\":\"op\",\"op\":{\"kind\":\"chan.order\",\"n\":%d,\"from\":[", n);
            for (i = 0; i < n; i++) {
                for (j = 0; j < n && g_chKnown[j] != items[i]; j++) {}
                if (j == n) { ok = 0; break; }
                from[i] = j;
                if (j != i) moved++;
                len += sprintf_s(m + len, sizeof(m) - len, "%s%d", i ? "," : "", j);
            }
            if (ok) {
                strcat_s(m, sizeof(m), "]}}");
                ws_send(m);
                units_permute(from, n);
                logmsg("CHAN list reordered locally: %d channels moved (sent chan.order)", moved);
            } else logmsg("CHAN list reordered with unknown channels; not synced");
            known_from_list();
        } else if (!deleted) logmsg("CHAN list changed in an unsupported way (count %d -> %d)", g_nChKnown, n);
        known_from_list();
        notes_rebaseline();          // the peer replays the same structural change
        InterlockedIncrement(&g_structGen);
    }
    // plugin identity (add/replace) per channel and mixer slot
    for (i = 0; i < n; i++) {
        void *inst = plugin_inst(items[i]);
        Unit *u = &g_chU[i];
        if (inst != u->inst) {
            u->inst = inst;
            if (now < u->absorbUntil || u->addAt) continue;
            if (inst) { u->identAt = now; logmsg("PLUG identity changed: ch %d", i); }
        }
    }
    int nm = mix_count(), gone = 0;
    for (t = 0; t < nm; t++)
        for (s = 0; s < MAX_FX_SLOTS; s++)
            if (g_fxU[t][s].inst && !plugin_inst(fx_obj(t, s)) && now >= g_fxU[t][s].absorbUntil) gone++;
    if (gone >= PROJ_FX_MASS) { proj_begin("many effects removed at once"); return; }
    for (t = 0; t < nm; t++)
        for (s = 0; s < MAX_FX_SLOTS; s++) {
            Unit *u = &g_fxU[t][s];
            void *inst = plugin_inst(fx_obj(t, s));
            if (inst == u->inst) continue;
            void *was = u->inst;
            u->inst = inst;
            if (now < u->absorbUntil) continue;
            if (inst) { u->identAt = now; logmsg("PLUG identity changed: fx %d/%d", t, s); }
            else if (was) {
                char m[112];
                sprintf_s(m, sizeof(m), "{\"t\":\"op\",\"op\":{\"kind\":\"fx.clear\",\"trk\":%d,\"slot\":%d}}", t, s);
                ws_send(m);
                u->identAt = 0; u->lastAct = 0; u->hash = 0;
                plug_agreed_set(U_FX, t, s, NULL, 0);
                logmsg("PLUG removed from fx %d/%d (sent fx.clear)", t, s);
            }
        }
}

// Leader: every unit's state, then the param values (join / project load).
static void do_full(void) {
    void **items; int n = chan_list(&items), i, t, s, cnt = 0;
    size_t cap = 256, len;
    char *m = (char *)malloc(cap);
    len = sprintf_s(m, cap, "{\"t\":\"op\",\"op\":{\"kind\":\"plug.full\",\"nch\":%d,\"items\":[", n);
    for (i = 0; i < n + mix_count() * MAX_FX_SLOTS; i++) {
        int unit = i < n ? U_CH : U_FX;
        int a = i < n ? i : (i - n) / MAX_FX_SLOTS, b = i < n ? 0 : (i - n) % MAX_FX_SLOTS;
        size_t dn = 0;
        unsigned char *d = unit_save(unit, a, b, &dn);
        if (!d) continue;
        Unit *u = unit_ptr(unit, a, b);
        if (u) u->hash = state_hash(d, dn);
        char *f = unit_json_fields(unit, a, b, d, dn);
        free(d);
        size_t fl = strlen(f);
        if (len + fl + 8 > cap) { cap = (len + fl + 8) * 2; m = (char *)realloc(m, cap); }
        len += sprintf_s(m + len, cap - len, "%s{%s}", cnt ? "," : "", f);
        free(f);
        cnt++;
    }
    if (len + 8 > cap) { cap = len + 8; m = (char *)realloc(m, cap); }
    strcat_s(m, cap, "]}}");
    ws_send(m);
    free(m);
    logmsg("PLUG full state sent: %d channels, %d units", n, cnt);
    (void)t; (void)s;
    send_notes_full();
    send_full_state();
}

// ---------------- notes ----------------
// Unit = one pattern (all channels' notes in it). Detection hashes every pattern's
// note array from the poll thread (SEH-guarded, no lock: a torn read just shows up as
// a change that settles); snapshots and applies run on the main thread under FL's own
// note lock, so the audio thread never sees a half-replaced pattern.
static uint64_t g_patHash[MAX_PAT];       // last state sent or applied (0 = no notes)
static uint64_t g_patSeen[MAX_PAT];       // last state the scanner saw
static DWORD    g_patChangedAt[MAX_PAT];  // when g_patSeen last moved (0 = settled/handled)
static volatile DWORD g_patSuppress[MAX_PAT];
static volatile LONG g_notesReady = 0;    // baseline taken

#define NOTES_SCAN_MS   100
#define NOTES_SETTLE_MS 250     // a drag rewrites notes every frame: send once it stops
#define NOTES_BULK      8       // this many patterns changing at once = project load, not an edit

// Hash of note records with the selection bit masked out: selecting is not an edit.
static uint64_t notes_fnv(const unsigned char *p, unsigned n) {
    uint64_t h = 1469598103934665603ull;
    size_t i, len = (size_t)n * NOTE_SIZE;
    for (i = 0; i < len; i++) {
        unsigned char b = p[i];
        if (i % NOTE_SIZE == NOTE_SEL_OFS) b &= (unsigned char)~NOTE_SEL_BIT;
        h ^= b; h *= 1099511628211ull;
    }
    return h ? h : 1;
}

static void notes_clear_sel(unsigned char *d, unsigned n) {
    unsigned i;
    for (i = 0; i < n; i++) d[(size_t)i * NOTE_SIZE + NOTE_SEL_OFS] &= (unsigned char)~NOTE_SEL_BIT;
}

static char *pat_notes_arr(int p) {
    char *tbl = *(char **)(g_flBase + (PATARR_VA - IMG_BASE));
    return *(char **)(tbl + (size_t)p * PAT_STRIDE + PAT_NOTES);
}

static uint64_t notes_hash(int p) {
    __try {
        char *a = pat_notes_arr(p);
        if (!a) return 0;
        unsigned n = *(unsigned *)(a + NOTES_COUNT);
        if (!n) return 0;
        if (n > 1000000) return 1;
        return notes_fnv(*(unsigned char **)(a + NOTES_DATA), n);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

static CRITICAL_SECTION *note_lock(void) { return *(CRITICAL_SECTION **)(g_flBase + (NOTELOCK_VA - IMG_BASE)); }

// Copy of pattern p's note records (main thread, under FL's note lock). *n = count.
static unsigned char *notes_copy(int p, unsigned *n) {
    unsigned char *buf = NULL;
    *n = 0;
    EnterCriticalSection(note_lock());
    __try {
        char *a = pat_notes_arr(p);
        unsigned c = a ? *(unsigned *)(a + NOTES_COUNT) : 0;
        if (c && c <= 1000000) {
            buf = (unsigned char *)malloc((size_t)c * NOTE_SIZE);
            memcpy(buf, *(unsigned char **)(a + NOTES_DATA), (size_t)c * NOTE_SIZE);
            *n = c;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { free(buf); buf = NULL; *n = 0; }
    LeaveCriticalSection(note_lock());
    return buf;
}

// Take the current note state of every pattern as already known (no sends).
static void notes_rebaseline(void) {
    int p;
    for (p = 1; p < MAX_PAT; p++) { g_patSeen[p] = g_patHash[p] = notes_hash(p); g_patChangedAt[p] = 0; }
    clips_rebaseline();          // audio clips reference channels: same structural events
    meta_rebaseline();           // channel names/colors move with their channels
    units_capture_all();         // and it is the state a lock conflict reverts to
}

static void redraw_editors(void);

// Replace pattern p's notes with recs[0..n). Main thread. Selection is local: incoming
// selection bits are dropped, and notes this user had selected stay selected when an
// identical note (ignoring the bit) is still there after the replace.
static void notes_apply(int p, const unsigned char *recs, unsigned n) {
    unsigned i, j, nold = 0;
    int k;
    unsigned char *in = n ? (unsigned char *)malloc((size_t)n * NOTE_SIZE) : NULL;
    unsigned char *old = NULL;
    if (in) { memcpy(in, recs, (size_t)n * NOTE_SIZE); notes_clear_sel(in, n); }
    unit_agreed(1, p, in, n);
    EnterCriticalSection(note_lock());
    __try {
        char *a = (char *)g_noteArr(p, 1);
        if (a) {
            noteadd_t add = (noteadd_t)(*(void ***)a)[NOTEADD_VSLOT];
            unsigned c = *(unsigned *)(a + NOTES_COUNT);
            if (c && c <= 1000000 && in) {
                old = (unsigned char *)malloc((size_t)c * NOTE_SIZE);
                memcpy(old, *(unsigned char **)(a + NOTES_DATA), (size_t)c * NOTE_SIZE);
                nold = c;
            }
            for (j = 0; j < nold; j++) {
                unsigned char *o = old + (size_t)j * NOTE_SIZE;
                if (!(o[NOTE_SEL_OFS] & NOTE_SEL_BIT)) continue;
                o[NOTE_SEL_OFS] &= (unsigned char)~NOTE_SEL_BIT;
                for (i = 0; i < n; i++) {
                    unsigned char *r = in + (size_t)i * NOTE_SIZE;
                    if (!(r[NOTE_SEL_OFS] & NOTE_SEL_BIT) && memcmp(r, o, NOTE_SIZE) == 0) { r[NOTE_SEL_OFS] |= NOTE_SEL_BIT; break; }
                }
            }
            for (k = (int)*(unsigned *)(a + NOTES_COUNT) - 1; k >= 0; k--) g_noteDel(a, k, 0);
            for (i = 0; i < n; i++) add(a, in + (size_t)i * NOTE_SIZE);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("NOTES apply faulted pat %d", p); }
    LeaveCriticalSection(note_lock());
    free(old); free(in);
    {
        void **items; int nc = chan_list(&items);
        __try {
            for (k = 0; k < nc; k++) {
                void *grid = *(void **)((char *)items[k] + CH_GRID);
                if (grid) g_gridRefresh(grid);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("NOTES grid refresh faulted"); }
    }
    redraw_editors();
    g_patSeen[p] = g_patHash[p] = notes_hash(p);
    g_patChangedAt[p] = 0;
    g_patSuppress[p] = GetTickCount() + NOTES_SETTLE_MS;
}

static char *notes_json_item(int p, const unsigned char *d, unsigned n) {
    size_t cap = 48 + ((size_t)n * NOTE_SIZE + 2) / 3 * 4 + 1;
    char *j = (char *)malloc(cap);
    int o = sprintf_s(j, cap, "\"pat\":%d,\"data\":\"", p);
    b64enc(d ? d : (const unsigned char *)"", (int)(n * NOTE_SIZE), j + o);
    strcat_s(j, cap, "\"");
    return j;
}

static void do_notesnap(const QItem *it) {
    int p = it->a;
    unsigned n;
    unsigned char *d = notes_copy(p, &n);
    uint64_t h = n ? notes_fnv(d, n) : 0;
    if (d) notes_clear_sel(d, n);
    if (h != g_patHash[p]) {
        char key[16];
        sprintf_s(key, sizeof(key), "pat:%d", p);
        if (!unit_before_send(key, 1, p, d, n)) { free(d); unit_revert(key); return; }
        char *f = notes_json_item(p, d, n);
        size_t cap = strlen(f) + 96;
        char *m = (char *)malloc(cap);
        sprintf_s(m, cap, "{\"t\":\"op\",\"op\":{\"kind\":\"notes.set\",\"key\":\"%s\",%s}}", key, f);
        ws_send(m);
        free(m); free(f);
        g_patHash[p] = h;
        logmsg("NOTES sent pattern %d (%u notes)", p, n);
    }
    free(d);
}

static void do_notes(const QItem *it) {
    if (g_patSeen[it->a] != g_patHash[it->a]) {   // our unsent edit is about to be replaced
        char key[16]; sprintf_s(key, sizeof(key), "pat:%d", it->a); hint_if_held(key);
    }
    notes_apply(it->a, (const unsigned char *)it->order, (unsigned)it->mode);
    logmsg("NOTES applied pattern %d (%d notes)", it->a, it->mode);
}

// Join: patterns the leader has no notes in must be empty here too.
static void do_notesync(const QItem *it) {
    const unsigned char *keep = (const unsigned char *)it->order;
    int p, n = 0;
    for (p = 1; p < MAX_PAT; p++) {
        if (keep[p] || !notes_hash(p)) continue;
        notes_apply(p, NULL, 0);
        n++;
    }
    if (n) logmsg("NOTES join: cleared %d patterns the leader has empty", n);
}

// Leader: every pattern that has notes (join / project load).
static void send_notes_full(void) {
    size_t cap = 256, len;
    char *m = (char *)malloc(cap);
    int p, cnt = 0;
    len = sprintf_s(m, cap, "{\"t\":\"op\",\"op\":{\"kind\":\"notes.full\",\"items\":[");
    for (p = 1; p < MAX_PAT; p++) {
        unsigned n;
        if (!notes_hash(p)) continue;
        unsigned char *d = notes_copy(p, &n);
        g_patSeen[p] = g_patHash[p] = n ? notes_fnv(d, n) : 0;
        if (!n) { free(d); continue; }
        notes_clear_sel(d, n);
        char *f = notes_json_item(p, d, n);
        free(d);
        size_t fl = strlen(f);
        if (len + fl + 8 > cap) { cap = (len + fl + 8) * 2; m = (char *)realloc(m, cap); }
        len += sprintf_s(m + len, cap - len, "%s{%s}", cnt ? "," : "", f);
        free(f);
        cnt++;
    }
    if (len + 8 > cap) { cap = len + 8; m = (char *)realloc(m, cap); }
    strcat_s(m, cap, "]}}");
    ws_send(m);
    free(m);
    logmsg("NOTES full state sent: %d patterns", cnt);
}

// Poll thread: find patterns whose notes changed locally and have settled.
static void scan_notes(DWORD now) {
    static DWORD last = 0;
    static int pend[MAX_PAT];
    int p, np = 0, i;
    if (now - last < NOTES_SCAN_MS) return;
    last = now;
    if (!g_notesReady) { notes_rebaseline(); g_notesReady = 1; return; }
    for (p = 1; p < MAX_PAT; p++) {
        uint64_t h = notes_hash(p);
        if (h != g_patSeen[p]) { g_patSeen[p] = h; g_patChangedAt[p] = now; }
        if (g_patChangedAt[p] && now - g_patChangedAt[p] >= NOTES_SETTLE_MS) {
            g_patChangedAt[p] = 0;
            if (g_patSeen[p] != g_patHash[p] && (LONG)(now - g_patSuppress[p]) >= 0) pend[np++] = p;
        }
    }
    if (np >= NOTES_BULK) {
        // project load/new: never broadcast as edits (the param bulk guard syncs the whole project)
        for (i = 0; i < np; i++) g_patHash[pend[i]] = g_patSeen[pend[i]];
        logmsg("NOTES bulk change in %d patterns: not sent", np);
        return;
    }
    for (i = 0; i < np; i++) enqueue_unit(T_NOTESNAP, 0, pend[i], 0, 0, NULL);
}

static unsigned char *b64dec(const char *s, size_t n, size_t *outn) {
    unsigned char *out = (unsigned char *)malloc(n / 4 * 3 + 3);
    size_t o = 0, i;
    int acc = 0, bits = 0;
    for (i = 0; i < n; i++) {
        int v = b64val(s[i]);
        if (v < 0) continue;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (unsigned char)((acc >> bits) & 0xff); }
    }
    *outn = o;
    return out;
}

// Parse one {"pat":P,"data":"..."} starting at p; queue its apply. Returns the end or NULL.
static const char *parse_notes_item(const char *p, unsigned char *keep) {
    int pat;
    const char *q = strstr(p, "\"pat\":");
    if (!q) return NULL;
    pat = atoi(q + 6);
    const char *d = strstr(q, "\"data\":\"");
    if (!d) return NULL;
    d += 8;
    const char *e = strchr(d, '"');
    if (!e) return NULL;
    if (pat >= 1 && pat < MAX_PAT) {
        size_t n;
        unsigned char *recs = b64dec(d, (size_t)(e - d), &n);
        QItem it; memset(&it, 0, sizeof(it));
        it.type = T_NOTES; it.a = pat; it.mode = (int)(n / NOTE_SIZE); it.order = (int *)recs;
        enqueue(&it);
        if (keep) keep[pat] = 1;
    }
    return e + 1;
}

// ---------------- playlist clips ----------------
// Unit = the clip list of one arrangement (records = FLP event 233 layout + runtime tail).
// Same scheme as notes: hash from the poll thread, copy/replace on the main thread under
// FL's note lock, then FL's own playlist refresh (what its clip-properties dialog calls).
// Hashes ignore the selection bit and the per-instance clip id, and are order-independent
// (FL orders clips at the same position by insertion, which differs between peers).
static uint64_t g_clipHash = 0, g_clipSeen = 0;
static DWORD    g_clipChangedAt = 0;
static volatile DWORD g_clipSuppress = 0;
static int      g_clipArr = -1;                  // arrangement the baseline belongs to

static int clip_cmp(const void *a, const void *b) { return memcmp(a, b, CLIP_SIZE); }

static char *arr_clips(int arr) {
    char *arrs = *(char **)(g_flBase + (ARRS_VA - IMG_BASE));
    if (!arrs || arr < 0 || arr >= (int)*(int64_t *)(arrs - 8)) return NULL;
    char *a = *(char **)(arrs + (size_t)arr * 8);
    return a ? *(char **)(a + ARR_CLIPS) : NULL;
}
static int cur_arr(void) {
    __try { return *(int *)(g_flBase + (ARRCUR_VA - IMG_BASE)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Masked, sorted copy -> hash. d may be modified (it is a private copy).
static uint64_t clips_fnv(unsigned char *d, unsigned n) {
    unsigned i;
    if (!n) return 0;
    for (i = 0; i < n; i++) {
        unsigned char *r = d + (size_t)i * CLIP_SIZE;
        r[CLIP_SEL_OFS] &= (unsigned char)~CLIP_SEL_BIT;
        memset(r + CLIP_ID_OFS, 0, 4);
    }
    qsort(d, n, CLIP_SIZE, clip_cmp);
    return fnv64(d, (size_t)n * CLIP_SIZE);
}

// Called from the poll thread and the main thread: private buffer per call.
static uint64_t clips_hash(int arr) {
    unsigned char *buf = NULL;
    uint64_t h = 0;
    __try {
        char *ca = arr_clips(arr);
        unsigned n = ca ? *(unsigned *)(ca + CLIPS_COUNT) : 0;
        if (n > 200000) h = 1;
        else if (n) {
            buf = (unsigned char *)malloc((size_t)n * CLIP_SIZE);
            memcpy(buf, *(unsigned char **)(ca + CLIPS_DATA), (size_t)n * CLIP_SIZE);
            h = clips_fnv(buf, n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { h = 1; }
    free(buf);
    return h;
}

static void clips_rebaseline(void) {
    g_clipArr = cur_arr();
    g_clipSeen = g_clipHash = clips_hash(g_clipArr);
    g_clipChangedAt = 0;
}

// Copy of an arrangement's clips (main thread, under FL's note lock).
static unsigned char *clips_copy(int arr, unsigned *n) {
    unsigned char *buf = NULL;
    *n = 0;
    EnterCriticalSection(note_lock());
    __try {
        char *ca = arr_clips(arr);
        unsigned c = ca ? *(unsigned *)(ca + CLIPS_COUNT) : 0;
        if (c && c <= 200000) {
            buf = (unsigned char *)malloc((size_t)c * CLIP_SIZE);
            memcpy(buf, *(unsigned char **)(ca + CLIPS_DATA), (size_t)c * CLIP_SIZE);
            *n = c;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { free(buf); buf = NULL; *n = 0; }
    LeaveCriticalSection(note_lock());
    return buf;
}

static void playlist_refresh(void) {
    __try {
        void *form = **(void ***)(g_flBase + (PLFORM_VA - IMG_BASE));
        if (form) ((formfn_t)(g_flBase + (PLREFRESH_VA - IMG_BASE)))(form);
    } __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("CLIPS refresh faulted"); }
}

// Replace an arrangement's clips with recs[0..n) (main thread). Selection stays local:
// clips this user had selected stay selected if an identical clip is still there.
static void clips_apply(int arr, const unsigned char *recs, unsigned n) {
    unsigned i, j, nold = 0;
    unsigned char *in = n ? (unsigned char *)malloc((size_t)n * CLIP_SIZE) : NULL;
    unsigned char *old = NULL;
    for (i = 0; i < n; i++) {
        memcpy(in + (size_t)i * CLIP_SIZE, recs + (size_t)i * CLIP_SIZE, CLIP_SIZE);
        in[(size_t)i * CLIP_SIZE + CLIP_SEL_OFS] &= (unsigned char)~CLIP_SEL_BIT;
    }
    unit_agreed(0, arr, in, n);
    EnterCriticalSection(note_lock());
    __try {
        char *ca = arr_clips(arr);
        if (ca) {
            void **vmt = *(void ***)ca;
            clipadd_t add = (clipadd_t)vmt[CLIPADD_VSLOT];
            clippurge_t purge = (clippurge_t)vmt[CLIPPURGE_VSLOT];
            unsigned c = *(unsigned *)(ca + CLIPS_COUNT);
            unsigned char *d = *(unsigned char **)(ca + CLIPS_DATA);
            if (c && c <= 200000) {
                old = (unsigned char *)malloc((size_t)c * CLIP_SIZE);
                memcpy(old, d, (size_t)c * CLIP_SIZE);
                nold = c;
            }
            for (j = 0; j < nold; j++) {
                unsigned char *o = old + (size_t)j * CLIP_SIZE;
                if (!(o[CLIP_SEL_OFS] & CLIP_SEL_BIT)) continue;
                o[CLIP_SEL_OFS] &= (unsigned char)~CLIP_SEL_BIT;
                for (i = 0; i < n; i++) {
                    unsigned char *r = in + (size_t)i * CLIP_SIZE;
                    if (!(r[CLIP_SEL_OFS] & CLIP_SEL_BIT) && memcmp(r, o, CLIP_ID_OFS) == 0 &&
                        memcmp(r + CLIP_ID_OFS + 4, o + CLIP_ID_OFS + 4, CLIP_SIZE - CLIP_ID_OFS - 4) == 0) {
                        r[CLIP_SEL_OFS] |= CLIP_SEL_BIT; break;
                    }
                }
            }
            // FL's own delete: mark every clip deleted, then purge (also frees their resources)
            for (j = 0; j < c && c <= 200000; j++) *(int32_t *)(d + (size_t)j * CLIP_SIZE + CLIP_DEL_OFS) = -1;
            if (c) purge(ca);
            for (i = 0; i < n; i++) add(ca, in + (size_t)i * CLIP_SIZE);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("CLIPS apply faulted arr %d", arr); }
    LeaveCriticalSection(note_lock());
    free(old); free(in);
    playlist_refresh();
    if (arr == g_clipArr) { g_clipSeen = g_clipHash = clips_hash(arr); g_clipChangedAt = 0; }
    g_clipSuppress = GetTickCount() + NOTES_SETTLE_MS;
}

static void do_clipsnap(const QItem *it) {
    unsigned n;
    unsigned char *d = clips_copy(it->a, &n);
    unsigned char *h = n ? (unsigned char *)malloc((size_t)n * CLIP_SIZE) : NULL;
    uint64_t hv;
    unsigned i;
    if (h) memcpy(h, d, (size_t)n * CLIP_SIZE);
    hv = n ? clips_fnv(h, n) : 0;
    free(h);
    if (it->a == g_clipArr && hv != g_clipHash) {
        char key[16];
        for (i = 0; i < n; i++) d[(size_t)i * CLIP_SIZE + CLIP_SEL_OFS] &= (unsigned char)~CLIP_SEL_BIT;
        sprintf_s(key, sizeof(key), "clips:%d", it->a);
        if (!unit_before_send(key, 0, it->a, d, n)) { free(d); unit_revert(key); return; }
        size_t cap = 96 + ((size_t)n * CLIP_SIZE + 2) / 3 * 4;
        char *m = (char *)malloc(cap);
        int o = sprintf_s(m, cap, "{\"t\":\"op\",\"op\":{\"kind\":\"clips.set\",\"key\":\"%s\",\"arr\":%d,\"data\":\"", key, it->a);
        b64enc(d ? d : (const unsigned char *)"", (int)(n * CLIP_SIZE), m + o);
        strcat_s(m, cap, "\"}}");
        ws_send(m);
        free(m);
        g_clipHash = hv;
        logmsg("CLIPS sent arrangement %d (%u clips)", it->a, n);
    }
    free(d);
}

static void do_clips(const QItem *it) {
    if (it->a == g_clipArr && g_clipSeen != g_clipHash) {
        char key[16]; sprintf_s(key, sizeof(key), "clips:%d", it->a); hint_if_held(key);
    }
    clips_apply(it->a, (const unsigned char *)it->order, (unsigned)it->mode);
    logmsg("CLIPS applied arrangement %d (%d clips)", it->a, it->mode);
}

// Poll thread: the current arrangement's clips changed locally and settled?
static void scan_clips(DWORD now) {
    static DWORD last = 0;
    int arr;
    uint64_t h;
    if (now - last < NOTES_SCAN_MS) return;
    last = now;
    arr = cur_arr();
    if (arr != g_clipArr) { clips_rebaseline(); return; }       // switched arrangement: new baseline
    h = clips_hash(arr);
    if (h != g_clipSeen) { g_clipSeen = h; g_clipChangedAt = now; }
    if (g_clipChangedAt && now - g_clipChangedAt >= NOTES_SETTLE_MS) {
        g_clipChangedAt = 0;
        if (g_clipSeen != g_clipHash && (LONG)(now - g_clipSuppress) >= 0) enqueue_unit(T_CLIPSNAP, 0, arr, 0, 0, NULL);
    }
}

// Inbound clips.set (recv thread).
static void on_clips_set(const char *m) {
    int arr = -1;
    const char *q = strstr(m, "\"arr\":"), *d = strstr(m, "\"data\":\"");
    if (!q || !d) return;
    arr = atoi(q + 6);
    d += 8;
    const char *e = strchr(d, '"');
    if (!e) return;
    size_t n;
    unsigned char *recs = b64dec(d, (size_t)(e - d), &n);
    QItem it; memset(&it, 0, sizeof(it));
    it.type = T_CLIPS; it.a = arr; it.mode = (int)(n / CLIP_SIZE); it.order = (int *)recs;
    enqueue(&it);
}

// ---------------- names, colors, mixer routing ("meta") ----------------
// Small per-entity state: mixer track name + color, mixer track routing (sends on/off +
// level), channel name + color, pattern name + color + length, playlist track name + color
// (every arrangement). Same scheme as notes: hashes from the poll thread, sent once settled,
// applied on the main thread with FL's own setters (the ones its Python API calls). Joins and
// project loads carry all of this in the .flp, so only live edits travel. No locks (renames
// are rare); if both sides change the same thing at once, the leader's value wins.
#define MIXNAME_VA    0x11b9ce0    // SetMixerTrackName(track, name, 1): also renames linked playlist tracks
#define MIXCOLOR_VA   0x11bcd60    // SetMixerTrackColor(track record, color)
#define UIUPDATE_VA   0xb69db0     // UIUpdate(what, 0); 0x18 = mixer (what FL calls after a mixer color)
#define MIXOBJ_VA     0x1498208    // global -> var -> mixer object (routing calls take it)
#define SETROUTE_VA   0x119e180    // SetRoute(mixer, src, dst, on, 1) -> int
#define ROUTEUPD_VA   0x119d6d0    // RoutingChanged(mixer): after route / send level edits
#define MIX_NAME      0x0c         // mixer track: UnicodeString name ('' = "Insert N")
#define MIX_COLOR     0x04
#define MIX_ROUTE     0x2e4        // + dst*8: int send level (12800 = 100%); +4: byte, 1 = routed
#define MIX_ROUTE_MAX 502
#define CH_NAME       0x50         // channel object: UnicodeString name
#define CH_COLOR      0x78
#define CH_SETNAME_VSLOT  (0x70 / 8)   // channel vmt: SetName(self, name)
#define CH_SETCOLOR_VSLOT (0x78 / 8)   //              SetColor(self, color)
#define PAT_NAME      0x00         // pattern record: UnicodeString name ('' = "Pattern N")
#define PAT_COLOR     0x08
#define PAT_LENGTH    0x10         // int, -1 = automatic
#define PATNAME_VA    0x11cad70    // SetPatternName(pattern, name)
#define PATCOLOR_VA   0x11cb260    // SetPatternColor(pattern, color): also sets the "custom color" flag
#define PATREFRESH_VA 0xd9a9e0     // what FL calls on (playlist form +0xd00) after a pattern rename
#define PLFORM_PATS   0xd00
#define PL_TRK_STRIDE 0x114        // arrangement object: playlist track t (1..500) at + t*0x114
#define PL_TRK_NAME   0x24
#define PL_TRK_COLOR  0x2c
#define PL_TRK_MAX    500
#define PLTRACK_VA    0x11dec90    // SetPlaylistTrack(arrangement, track, name, color)
#define META_ARR_MAX  8
#define META_MAX      (META_ARR_MAX * 512)
#define META_SCAN_MS  150
#define META_SETTLE_MS 300         // some dialogs rename per keystroke: send once it stops
#define META_BULK     24           // this many entities changing at once = project load / pattern insert
#define META_CONFLICT_MS 1500      // leader: a remote change this soon after our own send of the same thing loses

typedef void (*mixname_t)(int track, const wchar_t *name, uint8_t update);
typedef void (*mixcolor_t)(void *rec, uint32_t color);
typedef void (*uiupdate_t)(int what, uint64_t flag);
typedef int  (*setroute_t)(void *mixer, int src, int dst, int on, int flag);
typedef void (*mixerfn_t)(void *mixer);
typedef void (*chsetname_t)(void *ch, const wchar_t *name);
typedef void (*chsetcolor_t)(void *ch, uint32_t color);
typedef void (*patname_t)(int pat, const wchar_t *name);
typedef void (*patcolor_t)(int pat, uint32_t color);
typedef void (*patrefresh_t)(void *obj, uint8_t a, uint8_t b);
typedef void (*pltrack_t)(void *arr, int track, const wchar_t *name, uint32_t color);

enum { M_MIX, M_ROUTE, M_CHAN, M_PAT, M_PLT, M_NTYPES };
static const char *const M_TAG[M_NTYPES] = { "mx", "rt", "ch", "pt", "pl" };
static uint64_t g_mHash[M_NTYPES][META_MAX];   // last state sent or applied
static uint64_t g_mSeen[M_NTYPES][META_MAX];   // last state the scanner saw
static DWORD    g_mAt[M_NTYPES][META_MAX];     // when g_mSeen last moved (0 = settled/handled)
static DWORD    g_mSentAt[M_NTYPES][META_MAX];
static volatile LONG g_metaReady = 0;

static uint64_t fnv_add(uint64_t h, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
// Length of a Delphi UnicodeString (NULL = '').
static int ustr_len(const wchar_t *s) {
    int n = s ? *(const int32_t *)((const char *)s - 4) : 0;
    return n < 0 || n > 4096 ? 0 : n;
}
static uint64_t fnv_ustr(uint64_t h, const wchar_t *s) {
    int n = ustr_len(s);
    h = fnv_add(h, &n, 4);
    return n ? fnv_add(h, s, (size_t)n * 2) : h;
}
static int ustr_eq(const wchar_t *cur, const wchar_t *w) {
    int n = ustr_len(cur);
    return n == (int)wcslen(w) && (n == 0 || memcmp(cur, w, (size_t)n * 2) == 0);
}

static int meta_cap(int type) {
    switch (type) {
    case M_MIX: case M_ROUTE: return MAX_MIX;
    case M_CHAN: return MAX_CH;
    case M_PAT: return MAX_PAT;
    default: return META_MAX;
    }
}
static char *mix_rec(int t) { return *(char **)(g_flBase + (MIXARR_VA - IMG_BASE)) + (size_t)t * MIX_STRIDE; }
static char *pat_rec(int p) { return *(char **)(g_flBase + (PATARR_VA - IMG_BASE)) + (size_t)p * PAT_STRIDE; }
static int arr_count(void) {
    __try {
        char *arrs = *(char **)(g_flBase + (ARRS_VA - IMG_BASE));
        int n = arrs ? (int)*(int64_t *)(arrs - 8) : 0;
        return n < 0 ? 0 : (n > META_ARR_MAX ? META_ARR_MAX : n);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static char *arr_obj(int arr) {
    char *arrs = *(char **)(g_flBase + (ARRS_VA - IMG_BASE));
    return *(char **)(arrs + (size_t)arr * 8);
}
// i = arrangement * 512 + track (1..500)
static char *pl_track(int i) {
    char *a = arr_obj(i / 512);
    return a ? a + (size_t)(i % 512) * PL_TRK_STRIDE : NULL;
}
static int meta_valid(int type, int i) {
    switch (type) {
    case M_MIX: case M_ROUTE: return i >= 0 && i < mix_count();
    case M_CHAN: return i >= 0 && i < g_nChKnown;
    case M_PAT: return i >= 1 && i < MAX_PAT;
    default: return i % 512 >= 1 && i % 512 <= PL_TRK_MAX && i / 512 < arr_count();
    }
}

// Hash of one entity's synced fields (0 = absent). Any thread: SEH-guarded unlocked reads
// (a torn read is just a change that settles).
static uint64_t meta_hash(int type, int i) {
    uint64_t h = 1469598103934665603ull;
    if (!meta_valid(type, i)) return 0;
    __try {
        switch (type) {
        case M_MIX: {
            char *r = mix_rec(i);
            h = fnv_add(h, r + MIX_COLOR, 4);
            h = fnv_ustr(h, *(wchar_t **)(r + MIX_NAME));
            break; }
        case M_ROUTE: {
            char *r = mix_rec(i);
            int d, nm = mix_count();
            for (d = 0; d < nm && d < MIX_ROUTE_MAX; d++) {
                unsigned char on = (unsigned char)r[MIX_ROUTE + d * 8 + 4];
                h = fnv_add(h, &on, 1);
                if (on) h = fnv_add(h, r + MIX_ROUTE + d * 8, 4);
            }
            break; }
        case M_CHAN: {
            void **items;
            int n = chan_list(&items);
            if (!items || i >= n) return 0;
            h = fnv_add(h, (char *)items[i] + CH_COLOR, 4);
            h = fnv_ustr(h, *(wchar_t **)((char *)items[i] + CH_NAME));
            break; }
        case M_PAT: {
            char *r = pat_rec(i);
            h = fnv_add(h, r + PAT_COLOR, 4);
            h = fnv_add(h, r + PAT_LENGTH, 4);
            h = fnv_ustr(h, *(wchar_t **)(r + PAT_NAME));
            break; }
        default: {
            char *r = pl_track(i);
            if (!r) return 0;
            h = fnv_add(h, r + PL_TRK_COLOR, 4);
            h = fnv_ustr(h, *(wchar_t **)(r + PL_TRK_NAME));
            break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
    return h > 2 ? h : 3;
}

static void meta_rebaseline(void) {
    int t, i;
    for (t = 0; t < M_NTYPES; t++)
        for (i = 0; i < meta_cap(t); i++) { g_mSeen[t][i] = g_mHash[t][i] = meta_hash(t, i); g_mAt[t][i] = 0; }
}

// After applying a remote change: FL's setters touch other entities too (a mixer rename
// renames its linked playlist tracks). Take those as known, except where a local edit is pending.
static void meta_absorb(void) {
    int t, i;
    for (t = 0; t < M_NTYPES; t++)
        for (i = 0; i < meta_cap(t); i++) {
            if (g_mAt[t][i] || g_mSeen[t][i] != g_mHash[t][i]) continue;
            g_mSeen[t][i] = g_mHash[t][i] = meta_hash(t, i);
        }
}

// {"t":"op","op":{"kind":"meta.set","e":"<tag>","i":N, ...fields}}; name = base64 UTF-16LE.
// Main thread. NULL if the entity is gone.
static char *meta_json(int t, int i) {
    size_t cap = 32768;
    char *m = (char *)malloc(cap);
    int o = sprintf_s(m, cap, "{\"t\":\"op\",\"op\":{\"kind\":\"meta.set\",\"e\":\"%s\",\"i\":%d", M_TAG[t], i);
    __try {
        const wchar_t *name = NULL;
        uint32_t color = 0;
        char *r = NULL;
        switch (t) {
        case M_MIX: r = mix_rec(i); color = *(uint32_t *)(r + MIX_COLOR); name = *(wchar_t **)(r + MIX_NAME); break;
        case M_CHAN: {
            void **items;
            int n = chan_list(&items);
            if (!items || i >= n) { free(m); return NULL; }
            r = (char *)items[i]; color = *(uint32_t *)(r + CH_COLOR); name = *(wchar_t **)(r + CH_NAME);
            break; }
        case M_PAT:
            r = pat_rec(i); color = *(uint32_t *)(r + PAT_COLOR); name = *(wchar_t **)(r + PAT_NAME);
            o += sprintf_s(m + o, cap - o, ",\"l\":%d", *(int32_t *)(r + PAT_LENGTH));
            break;
        case M_PLT:
            r = pl_track(i);
            if (!r) { free(m); return NULL; }
            color = *(uint32_t *)(r + PL_TRK_COLOR); name = *(wchar_t **)(r + PL_TRK_NAME);
            break;
        case M_ROUTE: {
            int d, k = 0, nm = mix_count();
            r = mix_rec(i);
            o += sprintf_s(m + o, cap - o, ",\"r\":[");
            for (d = 0; d < nm && d < MIX_ROUTE_MAX; d++)
                if (r[MIX_ROUTE + d * 8 + 4])
                    o += sprintf_s(m + o, cap - o, "%s[%d,%d]", k++ ? "," : "", d, *(int32_t *)(r + MIX_ROUTE + d * 8));
            o += sprintf_s(m + o, cap - o, "]");
            break; }
        }
        if (t != M_ROUTE) {
            int nl = ustr_len(name);
            o += sprintf_s(m + o, cap - o, ",\"c\":%u,\"n\":\"", color);
            b64enc(nl ? (const unsigned char *)name : (const unsigned char *)"", nl * 2, m + o);
            o += (int)strlen(m + o);
            m[o++] = '"'; m[o] = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { free(m); return NULL; }
    sprintf_s(m + o, cap - o, "}}");
    return m;
}

static void do_metasnap(const QItem *it) {
    int t = it->unit, i = it->a;
    uint64_t h = meta_hash(t, i);
    if (!h || h == g_mHash[t][i]) return;
    char *m = meta_json(t, i);
    if (!m) return;
    ws_send(m);
    free(m);
    g_mHash[t][i] = h;
    g_mSentAt[t][i] = GetTickCount();
    logmsg("META sent %s %d", M_TAG[t], i);
}

// Poll thread: find entities that changed locally and settled.
static void scan_meta(DWORD now) {
    static DWORD last = 0;
    static int pend[M_NTYPES * META_MAX];
    int t, i, np = 0;
    if (now - last < META_SCAN_MS) return;
    last = now;
    if (!g_metaReady) { meta_rebaseline(); g_metaReady = 1; return; }
    for (t = 0; t < M_NTYPES; t++)
        for (i = 0; i < meta_cap(t); i++) {
            uint64_t h = meta_hash(t, i);
            if (h != g_mSeen[t][i]) { g_mSeen[t][i] = h; g_mAt[t][i] = now; }
            if (!g_mAt[t][i] || now - g_mAt[t][i] < META_SETTLE_MS) continue;
            g_mAt[t][i] = 0;
            if (g_mSeen[t][i] == g_mHash[t][i]) continue;
            if (!g_mSeen[t][i] || !g_mHash[t][i] ||   // gone / just appeared: structural sync handles it
                (t == M_CHAN && (g_chU[i].addAt || (LONG)(now - g_chU[i].absorbUntil) < 0))) {
                g_mHash[t][i] = g_mSeen[t][i];        // new / remotely loaded channel: its name came with its state
                continue;
            }
            pend[np++] = t * META_MAX + i;
        }
    if (np >= META_BULK) {
        for (i = 0; i < np; i++) g_mHash[pend[i] / META_MAX][pend[i] % META_MAX] = g_mSeen[pend[i] / META_MAX][pend[i] % META_MAX];
        logmsg("META bulk change in %d entities: not sent", np);
        return;
    }
    for (i = 0; i < np; i++) enqueue_unit(T_METASNAP, pend[i] / META_MAX, pend[i] % META_MAX, 0, 0, NULL);
}

// Inbound meta.set: the whole message goes to the main thread.
static void on_meta_set(const char *m) {
    QItem q; memset(&q, 0, sizeof(q));
    q.type = T_META; q.path = _strdup(m);
    enqueue(&q);
}

static wchar_t *meta_name_arg(const wchar_t *w) { return w[0] ? dstr_keep(w) : NULL; }   // '' = nil string

static void do_meta(const QItem *it) {
    const char *m = it->path, *e = strstr(m, "\"e\":\""), *q;
    int t, i = -1, len = 0, hasL, hasC = 0;
    uint32_t color = 0;
    wchar_t *name = NULL;
    DWORD now = GetTickCount();
    if (!e) return;
    for (t = 0; t < M_NTYPES && strncmp(e + 5, M_TAG[t], 2) != 0; t++) {}
    if (t == M_NTYPES || !int_after(m, NULL, "\"i\":", &i) || !meta_valid(t, i)) { logmsg("META %.2s %d: no such target here", e + 5, i); return; }
    if (g_isLeader && g_mSentAt[t][i] && now - g_mSentAt[t][i] < META_CONFLICT_MS) {
        // both changed it at once: the leader's value wins; send ours again so the peer converges
        g_mHash[t][i] = 0;
        enqueue_unit(T_METASNAP, t, i, 0, 0, NULL);
        logmsg("META %s %d: changed here too, keeping ours", M_TAG[t], i);
        return;
    }
    if ((q = strstr(m, "\"n\":\"")) != NULL) {
        const char *qe = strchr(q + 5, '"');
        if (qe) {
            size_t bn;
            unsigned char *b = b64dec(q + 5, (size_t)(qe - q - 5), &bn);
            name = (wchar_t *)calloc(bn / 2 + 1, 2);
            memcpy(name, b, bn / 2 * 2);
            free(b);
        }
    }
    if ((q = strstr(m, "\"c\":")) != NULL) { color = (uint32_t)strtoul(q + 4, NULL, 10); hasC = 1; }
    hasL = int_after(m, NULL, "\"l\":", &len);
    __try {
        switch (t) {
        case M_MIX: {
            char *r = mix_rec(i);
            if (name && !ustr_eq(*(wchar_t **)(r + MIX_NAME), name))
                ((mixname_t)(g_flBase + (MIXNAME_VA - IMG_BASE)))(i, meta_name_arg(name), 1);
            if (hasC && *(uint32_t *)(r + MIX_COLOR) != color) {
                ((mixcolor_t)(g_flBase + (MIXCOLOR_VA - IMG_BASE)))(r, color);
                ((uiupdate_t)(g_flBase + (UIUPDATE_VA - IMG_BASE)))(0x18, 0);
            }
            break; }
        case M_ROUTE: {
            static unsigned char on[MIX_ROUTE_MAX];
            static int lvl[MIX_ROUTE_MAX];
            char *r = mix_rec(i), *end;
            void *mixer = **(void ***)(g_flBase + (MIXOBJ_VA - IMG_BASE));
            int d, nm = mix_count(), changed = 0;
            memset(on, 0, sizeof(on));
            q = strstr(m, "\"r\":[");
            if (!q || !mixer) break;
            q += 5;
            while (*q == '[') {
                int l;
                d = (int)strtol(q + 1, &end, 10);
                if (*end != ',') break;
                l = (int)strtol(end + 1, &end, 10);
                if (*end != ']') break;
                if (d >= 0 && d < MIX_ROUTE_MAX) { on[d] = 1; lvl[d] = l; }
                q = end + 1;
                if (*q == ',') q++;
            }
            for (d = 0; d < nm && d < MIX_ROUTE_MAX; d++) {
                int cur = r[MIX_ROUTE + d * 8 + 4] != 0;
                if (cur != on[d]) { ((setroute_t)(g_flBase + (SETROUTE_VA - IMG_BASE)))(mixer, i, d, on[d], 1); changed = 1; }
                if (on[d] && *(int32_t *)(r + MIX_ROUTE + d * 8) != lvl[d]) { *(int32_t *)(r + MIX_ROUTE + d * 8) = lvl[d]; changed = 1; }
            }
            if (changed) {
                ((mixerfn_t)(g_flBase + (ROUTEUPD_VA - IMG_BASE)))(mixer);
                ((uiupdate_t)(g_flBase + (UIUPDATE_VA - IMG_BASE)))(0x18, 0);
            }
            break; }
        case M_CHAN: {
            char *o = (char *)chan_obj(i);
            void **vmt;
            if (!o) break;
            vmt = *(void ***)o;
            if (name && !ustr_eq(*(wchar_t **)(o + CH_NAME), name)) ((chsetname_t)vmt[CH_SETNAME_VSLOT])(o, meta_name_arg(name));
            if (hasC && *(uint32_t *)(o + CH_COLOR) != color) ((chsetcolor_t)vmt[CH_SETCOLOR_VSLOT])(o, color);
            break; }
        case M_PAT: {
            char *r = pat_rec(i);
            int changed = 0;
            if (name && !ustr_eq(*(wchar_t **)(r + PAT_NAME), name)) { ((patname_t)(g_flBase + (PATNAME_VA - IMG_BASE)))(i, meta_name_arg(name)); changed = 1; }
            if (hasC && *(uint32_t *)(r + PAT_COLOR) != color) { ((patcolor_t)(g_flBase + (PATCOLOR_VA - IMG_BASE)))(i, color); changed = 1; }
            if (hasL && *(int32_t *)(r + PAT_LENGTH) != len) { *(int32_t *)(r + PAT_LENGTH) = len; changed = 1; }
            if (changed) {
                char *form = **(char ***)(g_flBase + (PLFORM_VA - IMG_BASE));
                void *pats = form ? *(void **)(form + PLFORM_PATS) : NULL;
                if (pats) ((patrefresh_t)(g_flBase + (PATREFRESH_VA - IMG_BASE)))(pats, 1, 0);
                playlist_refresh();
            }
            break; }
        case M_PLT: {
            char *r = pl_track(i);
            if (!r) break;
            if ((name && !ustr_eq(*(wchar_t **)(r + PL_TRK_NAME), name)) || (hasC && *(uint32_t *)(r + PL_TRK_COLOR) != color))
                ((pltrack_t)(g_flBase + (PLTRACK_VA - IMG_BASE)))(arr_obj(i / 512), i % 512,
                    name ? meta_name_arg(name) : *(wchar_t **)(r + PL_TRK_NAME),
                    hasC ? color : *(uint32_t *)(r + PL_TRK_COLOR));
            break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("META apply faulted %s %d", M_TAG[t], i); }
    free(name);
    g_mSeen[t][i] = g_mHash[t][i] = meta_hash(t, i);
    g_mAt[t][i] = 0;
    meta_absorb();
    logmsg("META applied %s %d", M_TAG[t], i);
}

// ---------------- window guard ----------------
// Window state is client-only: what is open, where, and in which order is never
// synced. FL's preset loader opens/raises the plugin editor, so around every
// remote-originated heavy op we snapshot FL's floating forms and put things back
// afterwards (and keep doing so for a moment, as FL shows some windows late).
// Local input during that time ends the guard: the user's own actions win.
#define WG_MAX       128
#define WG_GUARD_MS  2000
typedef struct { HWND h; RECT r; int plugin; } WinRec;
typedef struct { int n; WinRec w[WG_MAX]; HWND fg, focus; DWORD at; } WinSnap;
static WinSnap g_wsnap;
static DWORD   g_wguardUntil = 0, g_wguardLast = 0;
static HWND    g_mainForm = NULL;

static int wnd_class_is(HWND h, const char *cls) {
    char c[64]; GetClassNameA(h, c, sizeof(c)); return strcmp(c, cls) == 0;
}
// FL's floating forms (plugin wrappers, channel rack, mixer, playlist, browser...)
static int is_fl_form(HWND h) {
    char c[64]; size_t n;
    GetClassNameA(h, c, sizeof(c)); n = strlen(c);
    return n > 5 && c[0] == 'T' && strcmp(c + n - 4, "Form") == 0 && strcmp(c, "TFruityLoopsMainForm") != 0;
}
static void rect_in_parent(HWND h, RECT *r) {
    GetWindowRect(h, r);
    MapWindowPoints(NULL, GetParent(h), (POINT *)r, 2);
}

typedef struct { HWND list[WG_MAX]; int n; } HwndList;
static BOOL CALLBACK collect_forms(HWND h, LPARAM lp) {
    HwndList *L = (HwndList *)lp;
    if (L->n < WG_MAX && IsWindowVisible(h) && is_fl_form(h)) L->list[L->n++] = h;
    return TRUE;
}
// visible FL forms, topmost first (EnumChildWindows walks siblings in z-order)
static void visible_forms(HwndList *L) {
    L->n = 0;
    if (g_mainForm) EnumChildWindows(g_mainForm, collect_forms, (LPARAM)L);
}

static BOOL CALLBACK redraw_cb(HWND h, LPARAM lp) {
    (void)lp;
    if (wnd_class_is(h, "TEventEditForm")) RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    return TRUE;
}
static void redraw_editors(void) {
    if (g_mainForm) EnumChildWindows(g_mainForm, redraw_cb, 0);
}

static void wguard_begin(void) {
    DWORD now = GetTickCount();
    if (now < g_wguardUntil) { g_wguardUntil = now + WG_GUARD_MS; return; }   // extend, keep first snapshot
    HwndList L; int i;
    visible_forms(&L);
    g_wsnap.n = L.n;
    for (i = 0; i < L.n; i++) {
        g_wsnap.w[i].h = L.list[i];
        g_wsnap.w[i].plugin = wnd_class_is(L.list[i], "TPluginForm");
        rect_in_parent(L.list[i], &g_wsnap.w[i].r);
    }
    g_wsnap.fg = GetForegroundWindow();
    g_wsnap.focus = GetFocus();
    g_wsnap.at = now;
    g_wguardUntil = now + WG_GUARD_MS;
}

static int snap_index(HWND h) {
    int i;
    for (i = 0; i < g_wsnap.n; i++) if (g_wsnap.w[i].h == h) return i;
    return -1;
}

static void wguard_pass(void) {
    DWORD now = GetTickCount();
    HwndList L; int i, j, changedOrder = 0;
    if (!g_wguardUntil || now >= g_wguardUntil) { g_wguardUntil = 0; return; }
    if (g_lastInput && (LONG)(g_lastInput - g_wsnap.at) > 0) { g_wguardUntil = 0; return; }  // user took over
    visible_forms(&L);
    // windows that newly appeared: a replaced plugin's editor takes over its old
    // window's place; anything else the remote op opened gets closed again
    for (i = 0; i < L.n; i++) {
        HWND h = L.list[i];
        if (snap_index(h) >= 0 || !wnd_class_is(h, "TPluginForm")) continue;
        int heir = -1;
        for (j = 0; j < g_wsnap.n; j++)
            if (g_wsnap.w[j].plugin && (!IsWindow(g_wsnap.w[j].h) || !IsWindowVisible(g_wsnap.w[j].h))) { heir = j; break; }
        if (heir >= 0) {
            RECT r = g_wsnap.w[heir].r;
            SetWindowPos(h, NULL, r.left, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            g_wsnap.w[heir].h = h;            // it now stands in for the old one
            logmsg("WINDOW kept replaced plugin editor in place");
        } else {
            SendMessageW(h, WM_CLOSE, 0, 0);  // same as the user closing it
            logmsg("WINDOW closed editor opened by a remote op");
        }
    }
    // positions
    for (i = 0; i < g_wsnap.n; i++) {
        HWND h = g_wsnap.w[i].h; RECT r;
        if (!IsWindow(h) || !IsWindowVisible(h)) continue;
        rect_in_parent(h, &r);
        if (r.left != g_wsnap.w[i].r.left || r.top != g_wsnap.w[i].r.top)
            SetWindowPos(h, NULL, g_wsnap.w[i].r.left, g_wsnap.w[i].r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // stacking order: compare current order of the snapshot windows with the snapshot
    visible_forms(&L);
    {
        int k = 0;
        for (i = 0; i < L.n; i++) {
            int si = snap_index(L.list[i]);
            if (si < 0) continue;
            while (k < g_wsnap.n && !IsWindowVisible(g_wsnap.w[k].h)) k++;
            if (k >= g_wsnap.n || g_wsnap.w[k].h != L.list[i]) { changedOrder = 1; break; }
            k++;
        }
    }
    if (changedOrder) {
        for (i = g_wsnap.n - 1; i >= 0; i--)          // bottom-most first, so the old top ends on top
            if (IsWindow(g_wsnap.w[i].h) && IsWindowVisible(g_wsnap.w[i].h))
                SetWindowPos(g_wsnap.w[i].h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        logmsg("WINDOW restored stacking order");
    }
    if (g_wsnap.fg && GetForegroundWindow() != g_wsnap.fg && IsWindow(g_wsnap.fg)) SetForegroundWindow(g_wsnap.fg);
    if (g_wsnap.focus && GetFocus() != g_wsnap.focus && IsWindow(g_wsnap.focus)) SetFocus(g_wsnap.focus);
    g_wguardLast = now;
}

// ---------------- toolbar: Connect button + connection status ----------------
// Our own child window in FL's top toolbar row (FL's Delphi code doesn't know about it),
// right-aligned in the empty stretch after the meters:   [ Connect ]  * Not connected
// Clicking opens the connect dialog (server URL + password), or offers to disconnect.
enum { NET_OFF, NET_CONNECTING, NET_ON, NET_ERR };
#define WM_FLIM_STATUS  (WM_APP + 0x31)
#define BAR_W_REF       440        // bar size at FL's 41 px toolbar row; scaled with the row
#define BAR_H_REF       26
#define BTN_W_REF       88
#define METERS_W_REF    250        // what FL draws at the left of the "Splitter_1" panel
static HWND     g_netBar = NULL, g_netRow = NULL;
static int      g_btnHover = 0, g_btnDown = 0, g_btnW = BTN_W_REF;
static COLORREF g_toolbarBg = RGB(0x4b, 0x55, 0x5c);
static HFONT    g_barFont = NULL;
static int      g_barFontH = 0;
static volatile LONG g_uiWanted = 0;
static volatile LONG g_netState = NET_OFF;
static CRITICAL_SECTION g_netTextLock;
static wchar_t  g_netText[256] = L"Not connected";
static char     g_myName[96];      // UTF-8, sanitized

static void net_connect(const char *url, const char *pw);
static void net_disconnect(void);

// Any thread: set the status text (UTF-8) and repaint the bar.
static void net_status(int state, const char *fmt, ...) {
    char a[256];
    va_list ap; va_start(ap, fmt); vsprintf_s(a, sizeof(a), fmt, ap); va_end(ap);
    EnterCriticalSection(&g_netTextLock);
    MultiByteToWideChar(CP_UTF8, 0, a, -1, g_netText, 256);
    LeaveCriticalSection(&g_netTextLock);
    InterlockedExchange(&g_netState, state);
    logmsg("NET %s", a);
    if (g_netBar) PostMessageW(g_netBar, WM_FLIM_STATUS, 0, 0);
}

static char g_netShown[300];       // the server as shown in the status ("host[:port]")
static char g_netErr[160];         // why the server ended the current connection, if it said so

// welcome / presence: "Connected to <server> · leader · 3 people"
static void net_show_connected(const char *m) {
    const char *p = strstr(m, "\"peers\":[");
    int n = 0;
    while (p && (p = strstr(p + 1, "\"role\":\"")) != NULL) n++;
    net_status(NET_ON, "Connected to %s \xC2\xB7 %s \xC2\xB7 %d %s", g_netShown, g_isLeader ? "leader" : "member",
               n, n == 1 ? "person" : "people");
}

static void bar_paint(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
    int W = rc.right, H = rc.bottom, st = g_netState;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ oldBmp = SelectObject(mem, bmp), oldFont;
    HBRUSH bg = CreateSolidBrush(g_toolbarBg);
    FillRect(mem, &rc, bg); DeleteObject(bg);
    if (!g_barFont || g_barFontH != H) {
        if (g_barFont) DeleteObject(g_barFont);
        g_barFontH = H;
        g_barFont = CreateFontW(-(H * 14 / BAR_H_REF), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
    oldFont = SelectObject(mem, g_barFont);
    SetBkMode(mem, TRANSPARENT);
    {   // button
        int r = H / 4, on = (st == NET_ON || st == NET_CONNECTING);
        COLORREF face = g_btnDown ? RGB(0x2e, 0x34, 0x38) : g_btnHover ? RGB(0x6a, 0x76, 0x7e) : RGB(0x5a, 0x64, 0x6b);
        HBRUSH fb = CreateSolidBrush(face);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0x37, 0x3e, 0x43));
        HGDIOBJ ob = SelectObject(mem, fb), op = SelectObject(mem, pen);
        RECT br = { 0, 0, g_btnW, H };
        RoundRect(mem, 0, 0, g_btnW, H, r, r);
        SelectObject(mem, ob); SelectObject(mem, op); DeleteObject(fb); DeleteObject(pen);
        SetTextColor(mem, RGB(0xee, 0xf1, 0xf3));
        if (g_btnDown) OffsetRect(&br, 0, 1);
        DrawTextW(mem, on ? L"Disconnect" : L"Connect", -1, &br, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    {   // status dot + text
        COLORREF dot = st == NET_ON ? RGB(0x5c, 0xd6, 0x6b) : st == NET_CONNECTING ? RGB(0xf2, 0xb7, 0x3a)
                     : st == NET_ERR ? RGB(0xf0, 0x6a, 0x5f) : RGB(0x8d, 0x97, 0x9e);
        int d = H * 8 / BAR_H_REF, x = g_btnW + H / 2;
        HBRUSH db = CreateSolidBrush(dot);
        HGDIOBJ ob = SelectObject(mem, db), op = SelectObject(mem, GetStockObject(NULL_PEN));
        RECT tr;
        Ellipse(mem, x, (H - d) / 2, x + d + 1, (H - d) / 2 + d + 1);
        SelectObject(mem, ob); SelectObject(mem, op); DeleteObject(db);
        SetRect(&tr, x + d + H / 4, 0, W, H);
        SetTextColor(mem, st == NET_ERR ? RGB(0xff, 0xa0, 0x96) : RGB(0xd4, 0xda, 0xde));
        EnterCriticalSection(&g_netTextLock);
        DrawTextW(mem, g_netText, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        LeaveCriticalSection(&g_netTextLock);
    }
    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldFont); SelectObject(mem, oldBmp); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(h, &ps);
}

// Right-align in the "Splitter_1" panel (FL's flexible gap); shrink if the window is narrow.
static void bar_layout(void) {
    RECT rr, sr, cur;
    HWND spl;
    int rowH, h, w, right, minX, x;
    if (!g_netBar || !IsWindow(g_netRow)) return;
    GetClientRect(g_netRow, &rr);
    rowH = rr.bottom > 0 ? rr.bottom : 41;
    h = rowH * BAR_H_REF / 41; if (h < 18) h = 18;
    w = h * BAR_W_REF / BAR_H_REF;
    g_btnW = h * BTN_W_REF / BAR_H_REF;
    right = rr.right - 8; minX = rr.right / 2;
    spl = FindWindowExW(g_netRow, NULL, L"TQuickEditToolbarPanel", L"Splitter_1");
    if (spl && IsWindowVisible(spl)) {
        GetWindowRect(spl, &sr);
        MapWindowPoints(NULL, g_netRow, (POINT *)&sr, 2);
        right = sr.right - 8;
        minX = sr.left + rowH * METERS_W_REF / 41 + 8;
    }
    if (right - w < minX) w = right - minX;
    if (w < g_btnW) w = g_btnW;
    x = right - w;
    GetWindowRect(g_netBar, &cur);
    MapWindowPoints(NULL, g_netRow, (POINT *)&cur, 2);
    if (cur.left != x || cur.right - cur.left != w || cur.bottom - cur.top != h) {
        SetWindowPos(g_netBar, HWND_TOP, x, (rowH - h) / 2, w, h, SWP_NOACTIVATE);
        InvalidateRect(g_netBar, NULL, FALSE);
    }
}

// ---- connect dialog ----
static char g_dlgUrl[512], g_dlgPw[256];

static INT_PTR CALLBACK connect_proc(HWND dlg, UINT m, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (m == WM_INITDIALOG) {
        wchar_t v[512], rem[8];
        flimcfg_get(L"server_url", v, 512);
        SetDlgItemTextW(dlg, IDC_URL, v);
        flimcfg_get_secret(L"server_pw", g_dlgPw, sizeof(g_dlgPw));
        MultiByteToWideChar(CP_UTF8, 0, g_dlgPw, -1, v, 512);
        SetDlgItemTextW(dlg, IDC_PASSWORD, v);
        SecureZeroMemory(g_dlgPw, sizeof(g_dlgPw)); SecureZeroMemory(v, sizeof(v));
        flimcfg_get(L"remember_pw", rem, 8);
        CheckDlgButton(dlg, IDC_REMEMBER, rem[0] == L'0' ? BST_UNCHECKED : BST_CHECKED);
        MultiByteToWideChar(CP_UTF8, 0, g_myName, -1, v, 512);
        SetDlgItemTextW(dlg, IDC_CNAME, v);
        SendDlgItemMessageW(dlg, IDC_CNAME, EM_LIMITTEXT, 64, 0);
        SetFocus(GetDlgItem(dlg, GetWindowTextLengthW(GetDlgItem(dlg, IDC_URL)) ? IDOK : IDC_URL));
        return FALSE;
    }
    if (m == WM_COMMAND && LOWORD(wp) == IDOK) {
        wchar_t w[512], name[FLIM_NAME_MAX + 1];
        NetUrl u;
        int remember = IsDlgButtonChecked(dlg, IDC_REMEMBER) == BST_CHECKED;
        GetDlgItemTextW(dlg, IDC_URL, w, 512);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, g_dlgUrl, sizeof(g_dlgUrl), NULL, NULL);
        if (!parse_url(g_dlgUrl, &u)) {
            MessageBoxW(dlg, L"That doesn't look like a server address.\n\nExamples:\n  wss://my-flim.fly.dev\n  localhost:8080",
                        L"flim", MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(dlg, IDC_URL));
            return TRUE;
        }
        GetDlgItemTextW(dlg, IDC_CNAME, w, 512);
        if (!flim_sanitize_name(w, name, FLIM_NAME_MAX + 1)) {
            MessageBoxW(dlg, L"Enter a name with at least one letter or digit.", L"flim", MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(dlg, IDC_CNAME));
            return TRUE;
        }
        {   // save the name only when it was edited here (FLIM_NAME dev overrides stay out of flim.ini)
            char n8[sizeof(g_myName)];
            WideCharToMultiByte(CP_UTF8, 0, name, -1, n8, sizeof(n8), NULL, NULL);
            if (strcmp(n8, g_myName) != 0) { strcpy_s(g_myName, sizeof(g_myName), n8); flimcfg_set(L"name", name); }
        }
        GetDlgItemTextW(dlg, IDC_PASSWORD, w, 512);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, g_dlgPw, sizeof(g_dlgPw), NULL, NULL);
        SecureZeroMemory(w, sizeof(w));
        GetDlgItemTextW(dlg, IDC_URL, w, 512);
        flimcfg_set(L"server_url", w);
        flimcfg_set(L"remember_pw", remember ? L"1" : L"0");
        flimcfg_set_secret(L"server_pw", remember ? g_dlgPw : NULL);
        EndDialog(dlg, IDOK);
        return TRUE;
    }
    if (m == WM_COMMAND && LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
    return FALSE;
}

// Main thread (the bar's click).
static void bar_clicked(HWND h) {
    int st = g_netState;
    HWND owner = g_mainForm ? g_mainForm : h;
    if (st == NET_ON || st == NET_CONNECTING) {
        if (MessageBoxW(owner, L"Disconnect from the flim server?\n\n"
                        L"Your FL project stays as it is; nothing is synced until you connect again.",
                        L"flim", MB_YESNO | MB_ICONQUESTION) == IDYES)
            net_disconnect();
        return;
    }
    if (DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_CONNECT), owner, connect_proc, 0) == IDOK) {
        net_connect(g_dlgUrl, g_dlgPw);
        SecureZeroMemory(g_dlgPw, sizeof(g_dlgPw));
    }
}

static LRESULT CALLBACK bar_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: bar_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_FLIM_STATUS: InvalidateRect(h, NULL, FALSE); return 0;
    case WM_TIMER: bar_layout(); return 0;
    case WM_SETCURSOR: {
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        SetCursor(LoadCursor(NULL, p.x < g_btnW ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        int on = (short)LOWORD(lp) < g_btnW;
        if (on != g_btnHover) { g_btnHover = on; InvalidateRect(h, NULL, FALSE); }
        if (on) { TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 }; TrackMouseEvent(&t); }
        return 0;
    }
    case WM_MOUSELEAVE: g_btnHover = 0; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN:
        if ((short)LOWORD(lp) < g_btnW) { g_btnDown = 1; SetCapture(h); InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_LBUTTONUP:
        if (g_btnDown) {
            RECT rc; POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
            GetClientRect(h, &rc); rc.right = g_btnW;
            g_btnDown = 0; g_btnHover = 0; ReleaseCapture(); InvalidateRect(h, NULL, FALSE);
            if (PtInRect(&rc, p)) bar_clicked(h);
        }
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static BOOL CALLBACK find_toolbar_row(HWND h, LPARAM lp) {
    if (wnd_class_is(h, "TQuickEditToolBar")) { *(HWND *)lp = h; return FALSE; }
    return TRUE;
}

// Main thread only.
static void ui_create(void) {
    HWND row = NULL;
    if (g_netBar || !g_mainForm) return;
    EnumChildWindows(g_mainForm, find_toolbar_row, (LPARAM)&row);   // the first row (FILE EDIT ...)
    if (!row) { logmsg("UI: toolbar row not found"); return; }
    g_netRow = row;
    {   // sample the toolbar colour near the right end of the row (PrintWindow works even when FL is covered)
        RECT rr; GetClientRect(row, &rr);
        HDC dc = GetDC(row), full = CreateCompatibleDC(dc);
        HBITMAP fullBmp = CreateCompatibleBitmap(dc, rr.right, rr.bottom);
        HGDIOBJ of = SelectObject(full, fullBmp);
        HWND spl = FindWindowExW(row, NULL, L"TQuickEditToolbarPanel", L"Splitter_1");
        int sx = rr.right - 120;
        COLORREF c;
        if (spl) { RECT sr; GetWindowRect(spl, &sr); MapWindowPoints(NULL, row, (POINT *)&sr, 2); sx = sr.right - 20; }
        PrintWindow(row, full, PW_CLIENTONLY | 2 /* PW_RENDERFULLCONTENT */);
        c = GetPixel(full, sx, rr.bottom / 2);
        if (c != CLR_INVALID) g_toolbarBg = c;
        SelectObject(full, of); DeleteObject(fullBmp); DeleteDC(full);
        ReleaseDC(row, dc);
    }
    WNDCLASSW wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = bar_proc;
    wc.hInstance = g_hinst;
    wc.lpszClassName = L"FlimNetBar";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    g_netBar = CreateWindowExW(0, L"FlimNetBar", L"flim", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                               0, 0, 1, 1, row, NULL, g_hinst, NULL);
    if (!g_netBar) { logmsg("UI: CreateWindow failed %lu", GetLastError()); return; }
    bar_layout();
    SetTimer(g_netBar, 1, 1000, NULL);       // follow FL's window resizes
    {
        RECT b; GetWindowRect(g_netBar, &b);
        logmsg("UI net bar %p in toolbar row %p at %ld,%ld %ldx%ld (bg %06lx)", (void *)g_netBar, (void *)row,
               b.left, b.top, b.right - b.left, b.bottom - b.top, (unsigned long)g_toolbarBg);
    }
}

// ---------------- project sync ----------------
// A project load/new/close is never replayed piece by piece. The engine enters
// "project mode": nothing is sent, every param change is absorbed, and only project
// tasks run on FL's thread. When FL's load is over and things are quiet, all baselines
// are re-taken. The leader then sends the project FILE (members open the same .flp);
// a member that loaded something on its own asks the leader for its project instead.
// Both sides work on "<name>_flim_MMDD_hhmm.flp" so Ctrl+S never touches the original file
// and reopening the original never overwrites an earlier session's copy.
static volatile LONG  g_inHook = 0;
static volatile LONG  g_projBusy = 0;          // project mode
static volatile LONG  g_projRemote = 0;        // member: loading the leader's project
static volatile LONG  g_projDoneQueued = 0;
static volatile DWORD g_projSince = 0;
static volatile DWORD g_closingUntil = 0;      // FL is closing: never broadcast what teardown does
static volatile DWORD g_lastHookTick = 0;
static CRITICAL_SECTION g_heldLock;            // member: ops that arrived during a project load
static char **g_held = NULL;
static int    g_nheld = 0, g_capheld = 0;

#define PROJ_SETTLE_MS  1500    // quiet time after FL's load before baselines are re-taken
#define PROJ_MAX_MS     180000  // give up waiting for quiet after this long

static void handle_msg(const char *m);

static int closing_now(void) { return g_closingUntil && (LONG)(GetTickCount() - g_closingUntil) < 0; }

static void fl_ustr(uint32_t va, wchar_t *out, int cap);
static wchar_t g_projNameAtBegin[MAX_PATH];     // FL's project file when the project change began

static void proj_begin(const char *why) {
    if (InterlockedExchange(&g_projBusy, 1) == 0) {
        fl_ustr(PROJNAME_VA, g_projNameAtBegin, MAX_PATH);
        g_projSince = GetTickCount();
        InterlockedExchange(&g_projRemote, 0);
        InterlockedExchange(&g_projDoneQueued, 0);
        logmsg("PROJECT change (%s): sync paused until FL settles", why);
    }
}

static void *fl_form(void) {
    __try { return **(void ***)(g_flBase + (MAINOBJ_VA - IMG_BASE)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}
static void *fl_project(void) {
    __try { char *h = **(char ***)(g_flBase + (FLPROJ_VA - IMG_BASE)); return *(void **)(h + 0x10); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}
// FL is inside an .flp load (its open handler parks a busy object on the form meanwhile).
static int fl_loading(void) {
    char *f = (char *)fl_form();
    if (!f) return 0;
    __try { return *(void **)(f + FORM_BUSY) != NULL; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// Copy of one of FL's UnicodeString globals ('' if empty).
static void fl_ustr(uint32_t va, wchar_t *out, int cap) {
    out[0] = 0;
    __try {
        const wchar_t *s = *(const wchar_t **)(g_flBase + (va - IMG_BASE));
        if (s) wcsncpy_s(out, cap, s, _TRUNCATE);
    } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}
// A Delphi literal string FL may keep a pointer to: never freed (a few hundred bytes per load).
static wchar_t *dstr_keep(const wchar_t *s) {
    int n = (int)wcslen(s);
    unsigned char *m = (unsigned char *)calloc(1, 16 + (n + 1) * 2);
    *(uint16_t *)(m + 4) = 1200; *(uint16_t *)(m + 6) = 2;
    *(int32_t *)(m + 8) = -1;    *(int32_t *)(m + 12) = n;
    memcpy(m + 16, s, (size_t)n * 2);
    return (wchar_t *)(m + 16);
}

static unsigned char *read_file_w(const wchar_t *path, size_t *n) {
    FILE *f = NULL;
    *n = 0;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc(sz > 0 ? sz : 1);
    *n = fread(buf, 1, sz > 0 ? sz : 0, f);
    fclose(f);
    return buf;
}
static int write_file_w(const wchar_t *path, const unsigned char *d, size_t n) {
    FILE *f = NULL;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) return 0;
    size_t w = fwrite(d, 1, n, f);
    fclose(f);
    return w == n;
}
static int file_exists_w(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static int ends_with_ci(const wchar_t *s, const wchar_t *suf) {
    size_t a = wcslen(s), b = wcslen(suf);
    return a >= b && _wcsicmp(s + a - b, suf) == 0;
}
static const wchar_t *base_name(const wchar_t *p) {
    const wchar_t *b = wcsrchr(p, L'\\'), *c = wcsrchr(p, L'/');
    if (c && (!b || c > b)) b = c;
    return b ? b + 1 : p;
}
// "<stem>_flim.flp" or "<stem>_flim_MMDD_hhmm.flp": already a session copy, work on it directly.
static int is_flim_name(const wchar_t *p) {
    size_t n = wcslen(p);
    int i;
    if (ends_with_ci(p, L"_flim.flp")) return 1;
    if (n < 19 || _wcsicmp(p + n - 4, L".flp") != 0) return 0;
    const wchar_t *t = p + n - 19;                     // "_flim_MMDD_hhmm.flp"
    if (_wcsnicmp(t, L"_flim_", 6) != 0 || t[10] != L'_') return 0;
    for (i = 6; i < 15; i++) if (i != 10 && !iswdigit(t[i])) return 0;
    return 1;
}
// path with its extension replaced by "_flim_MMDD_hhmm.flp" (local time).
static void flim_name(const wchar_t *path, wchar_t *out, int cap) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wcscpy_s(out, cap, path);
    wchar_t *dot = wcsrchr(out, L'.'), *sl = wcsrchr(out, L'\\');
    if (dot && (!sl || dot > sl)) *dot = 0;
    size_t n = wcslen(out);
    swprintf_s(out + n, cap - n, L"_flim_%02d%02d_%02d%02d.flp", st.wMonth, st.wDay, st.wHour, st.wMinute);
}

// An existing file at p is kept as p.bak (replacing an older .bak) before p is rewritten.
static void backup_existing(const wchar_t *p) {
    wchar_t bak[MAX_PATH + 8];
    if (!file_exists_w(p)) return;
    swprintf_s(bak, MAX_PATH + 8, L"%s.bak", p);
    if (MoveFileExW(p, bak, MOVEFILE_REPLACE_EXISTING)) logmsg("PROJECT kept the previous copy as %S", bak);
}

// Make path FL's current project (title, Ctrl+S target, recent files). Main thread.
static void set_project_file(const wchar_t *path) {
    void *form = fl_form();
    wchar_t *w = dstr_keep(path);
    if (!form) return;
    __try { ((setproj_t)(g_flBase + (SETPROJ_VA - IMG_BASE)))(form, w, w, 1); }
    __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("PROJECT set name faulted"); }
}

// Re-take every baseline after a project change (main thread). Nothing is sent.
static void proj_rebaseline(void) {
    void **items; int n = chan_list(&items), i, t, s, k;
    known_from_list();
    memset(g_chU, 0, sizeof(g_chU));
    memset(g_fxU, 0, sizeof(g_fxU));
    plug_agreed_reset(1, 1);
    memset(g_chanPlugN, 0, sizeof(g_chanPlugN));
    memset(g_fxN, 0, sizeof(g_fxN));
    for (i = 0; i < n; i++) g_chU[i].inst = plugin_inst(items[i]);
    for (t = 0; t < mix_count() && t < MAX_MIX; t++)
        for (s = 0; s < MAX_FX_SLOTS; s++) g_fxU[t][s].inst = plugin_inst(fx_obj(t, s));
    notes_rebaseline();
    rescan();
    for (k = 0; k < g_nslot; k++) {
        EnterCriticalSection(&g_slotLock);
        g_slots[k].last = read_value(g_slots[k].id);
        g_slots[k].lockedByMe = 0;
        InterlockedIncrement(&g_slots[k].gen);
        LeaveCriticalSection(&g_slotLock);
    }
    InterlockedIncrement(&g_structGen);
}

// {"t":"op","op":{"kind":"project.load","name":"<utf8>","data":"<b64>"}}
static void send_project(const unsigned char *d, size_t n, const wchar_t *name) {
    char nm[MAX_PATH * 3], esc[MAX_PATH * 6];
    int i, o = 0;
    nm[0] = 0;
    if (name && *name) WideCharToMultiByte(CP_UTF8, 0, name, -1, nm, sizeof(nm), NULL, NULL);
    for (i = 0; nm[i] && o < (int)sizeof(esc) - 7; i++) {
        unsigned char c = (unsigned char)nm[i];
        if (c == '"' || c == '\\') { esc[o++] = '\\'; esc[o++] = (char)c; }
        else if (c < 0x20) o += sprintf_s(esc + o, sizeof(esc) - o, "\\u%04x", c);
        else esc[o++] = (char)c;
    }
    esc[o] = 0;
    size_t cap = strlen(esc) + (n + 2) / 3 * 4 + 128;
    char *m = (char *)malloc(cap);
    int len = sprintf_s(m, cap, "{\"t\":\"op\",\"op\":{\"kind\":\"project.load\",\"name\":\"%s\",\"data\":\"", esc);
    b64enc(d, (int)n, m + len);
    strcat_s(m, cap, "\"}}");
    ws_send_samples(m, d, n);
    logmsg("PROJECT sent %S (%zu bytes)", (name && *name) ? name : L"(untitled)", n);
}

// Leader: snapshot of the current project (FL's own project writer; the current file
// name stays as it is). Returns malloc'd bytes or NULL. Main thread.
static unsigned char *project_snapshot(size_t *n) {
    wchar_t path[MAX_PATH];
    char r = 0;
    *n = 0;
    swprintf_s(path, MAX_PATH, L"%S\\snapshot_%ld.flp", g_tmpDir, InterlockedIncrement(&g_tmpSeq));
    __try { r = ((saveproj_t)(g_flBase + (SAVEPROJ_VA - IMG_BASE)))(dstr_keep(path), 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("PROJECT snapshot faulted"); r = 0; }
    if (!r) { logmsg("PROJECT snapshot failed"); return NULL; }
    unsigned char *d = read_file_w(path, n);
    DeleteFileW(path);
    return d;
}

// Leader, after a local load: work on <name>_flim_MMDD_hhmm.flp (a copy next to the original) and
// send that file to the members. Untitled projects (new from template) go as a snapshot.
static void leader_project_loaded(void) {
    wchar_t cur[MAX_PATH], flim[MAX_PATH + 16];
    size_t n = 0;
    unsigned char *d = NULL;
    fl_ustr(PROJNAME_VA, cur, MAX_PATH);
    if (cur[0] && file_exists_w(cur)) {
        if (!is_flim_name(cur)) {
            flim_name(cur, flim, MAX_PATH + 16);
            backup_existing(flim);                       // only if reopened within the same minute
            if (CopyFileW(cur, flim, FALSE)) {
                set_project_file(flim);
                logmsg("PROJECT working copy %S (original %S is never saved by flim sessions)", flim, cur);
                wcscpy_s(cur, MAX_PATH, flim);
            } else logmsg("PROJECT could not create %S (error %lu); staying on the original", flim, GetLastError());
        }
        d = read_file_w(cur, &n);
        if (d) { send_project(d, n, base_name(cur)); free(d); return; }
    }
    d = project_snapshot(&n);
    if (d) { send_project(d, n, NULL); free(d); }
}

static void proj_end(void) {
    InterlockedExchange(&g_projDoneQueued, 0);
    InterlockedExchange(&g_projRemote, 0);
    InterlockedExchange(&g_projBusy, 0);
}

static void replay_held(void) {
    char **h;
    int n, i;
    EnterCriticalSection(&g_heldLock);
    h = g_held; n = g_nheld;
    g_held = NULL; g_nheld = g_capheld = 0;
    LeaveCriticalSection(&g_heldLock);
    if (n) logmsg("PROJECT replaying %d ops that arrived during the load", n);
    for (i = 0; i < n; i++) { handle_msg(h[i]); free(h[i]); }
    free(h);
}

static void do_projdone(void) {
    int remote = g_projRemote, same = 0;
    {
        // a real load replaces every channel object; same objects = cancelled open (or a big
        // preset change that tripped the bulk guard): nothing to send. FL can reallocate the
        // new channels at the old addresses, so a changed project file also counts as a load.
        void **items; int n = chan_list(&items), i;
        wchar_t now[MAX_PATH];
        same = items && g_nChKnown > 0 && n == g_nChKnown;
        for (i = 0; same && i < n; i++) if (items[i] != g_chKnown[i]) same = 0;
        fl_ustr(PROJNAME_VA, now, MAX_PATH);
        if (_wcsicmp(now, g_projNameAtBegin) != 0) same = 0;
    }
    proj_rebaseline();
    if (same && !remote && !closing_now()) {
        logmsg("PROJECT unchanged after %lu ms (open cancelled?): nothing sent", GetTickCount() - g_projSince);
        proj_end();
        return;
    }
    if (closing_now()) { logmsg("PROJECT settled while FL is closing: nothing sent"); proj_end(); return; }
    logmsg("PROJECT settled after %lu ms (%d channels, %ld params)", GetTickCount() - g_projSince, g_nChKnown, g_nslot);
    if (remote) { proj_end(); replay_held(); return; }
    proj_end();
    if (g_isLeader) leader_project_loaded();
    else {
        logmsg("PROJECT loaded locally on a member: re-syncing to the leader's project");
        ws_send("{\"t\":\"op\",\"op\":{\"kind\":\"state.req\"}}");
    }
}

// Member: open the leader's project. it->path = UTF-16 path of the received file.
static void do_projload(const QItem *it) {
    const wchar_t *path = (const wchar_t *)it->path;
    void *form = fl_form(), *proj = fl_project();
    char r = 0;
    DWORD t0 = GetTickCount();
    if (!form || !proj) { logmsg("PROJECT load: FL objects not found"); return; }
    wchar_t *w = dstr_keep(path);
    __try {
        ((formfn_t)(g_flBase + (BUSYBEGIN_VA - IMG_BASE)))(form);
        r = ((projload_t)(*(void ***)proj)[FLPROJ_LOAD_SLOT])(proj, w, 0);   // flags 0: no "save changes?" prompt
        ((formfn_t)(g_flBase + (BUSYEND_VA - IMG_BASE)))(form);
    } __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("PROJECT load faulted"); }
    if (it->mode) set_project_file(path);                     // named: Ctrl+S saves the member's own copy
    logmsg("PROJECT opened %S ret=%d in %lu ms", path, r, GetTickCount() - t0);
}

// Leader: a member asked for the project (join or re-sync).
static void do_projsnap(void) {
    wchar_t cur[MAX_PATH];
    size_t n = 0;
    unsigned char *d = project_snapshot(&n);
    if (!d) return;
    fl_ustr(PROJNAME_VA, cur, MAX_PATH);
    send_project(d, n, cur[0] ? base_name(cur) : NULL);
    free(d);
}

// Member (recv thread): store the leader's project and queue the load.
static void on_project_load(const char *m) {
    const char *nk = strstr(m, "\"name\":\""), *dk = strstr(m, "\"data\":\"");
    wchar_t name[MAX_PATH], dir[MAX_PATH], path[MAX_PATH * 2];
    char nm[MAX_PATH * 3];
    int o = 0, i;
    size_t n = 0;
    if (!dk) return;
    if (nk) {
        for (nk += 8; *nk && *nk != '"' && o < (int)sizeof(nm) - 1; nk++) {
            if (*nk == '\\' && nk[1]) nk++;
            nm[o++] = *nk;
        }
    }
    nm[o] = 0;
    name[0] = 0;
    if (o) MultiByteToWideChar(CP_UTF8, 0, nm, -1, name, MAX_PATH);
    for (i = 0; name[i]; i++) if (wcschr(L"\\/:*?\"<>|", name[i])) name[i] = L'_';
    if (name[0] && !is_flim_name(name)) {                      // members also work on a session copy
        wchar_t tmp[MAX_PATH];
        flim_name(name, tmp, MAX_PATH);
        wcscpy_s(name, MAX_PATH, tmp);
    }
    dk += 8;
    const char *e = strchr(dk, '"');
    if (!e) return;
    unsigned char *d = b64dec(dk, (size_t)(e - dk), &n);
    samples_rewrite(&d, &n);
    if (name[0]) {
        DWORD k = GetEnvironmentVariableW(L"USERPROFILE", dir, MAX_PATH);
        if (!k || k >= MAX_PATH) wcscpy_s(dir, MAX_PATH, L"C:");
        wcscat_s(dir, MAX_PATH, L"\\Documents\\flim");
        SHCreateDirectoryExW(NULL, dir, NULL);
        swprintf_s(path, MAX_PATH * 2, L"%s\\%s", dir, name);
        backup_existing(path);
    } else swprintf_s(path, MAX_PATH * 2, L"%S\\project_%ld.flp", g_tmpDir, InterlockedIncrement(&g_tmpSeq));
    if (!write_file_w(path, d, n)) { logmsg("PROJECT could not write %S", path); free(d); return; }
    free(d);
    // supersede anything queued or held from the previous project
    EnterCriticalSection(&g_heldLock);
    for (i = 0; i < g_nheld; i++) free(g_held[i]);
    g_nheld = 0;
    LeaveCriticalSection(&g_heldLock);
    InterlockedExchange(&g_projBusy, 1);
    InterlockedExchange(&g_projRemote, 1);
    InterlockedExchange(&g_projDoneQueued, 0);
    g_projSince = GetTickCount();
    logmsg("PROJECT received %S (%zu bytes): loading", path, n);
    QItem q; memset(&q, 0, sizeof(q));
    q.type = T_PROJLOAD;
    q.mode = name[0] != 0;
    q.path = (char *)_wcsdup(path);
    enqueue(&q);
}

// Member (recv thread): keep ops for after the load. Returns 1 if held.
static int hold_if_loading(const char *m) {
    if (!g_projBusy || g_isLeader || !strstr(m, "\"t\":\"op\"")) return 0;
    EnterCriticalSection(&g_heldLock);
    if (g_nheld == g_capheld) { g_capheld = g_capheld ? g_capheld * 2 : 64; g_held = (char **)realloc(g_held, sizeof(char *) * g_capheld); }
    g_held[g_nheld++] = _strdup(m);
    LeaveCriticalSection(&g_heldLock);
    return 1;
}

// Poll thread, project mode: absorb everything, wait for FL's load to end and go quiet.
static void proj_poll(DWORD now) {
    static uint64_t sig = 0;
    static DWORD sigSince = 0, lastRescan = 0;
    int k, n, t, s, p;
    if (fl_loading() || g_inHook) { sigSince = now; return; }          // don't touch FL mid-load
    if (now - lastRescan > 500) { rescan(); lastRescan = now; }
    for (k = 0; k < g_nslot; k++) g_slots[k].last = read_value(g_slots[k].id);
    {
        void **items; uint64_t h = 1469598103934665603ull;
        n = chan_list(&items);
        for (k = 0; k < n && items; k++) { h ^= (uint64_t)(uintptr_t)items[k]; h *= 1099511628211ull; }
        for (t = 0; t < mix_count() && t < MAX_MIX; t++)
            for (s = 0; s < MAX_FX_SLOTS; s++) { h ^= (uint64_t)(uintptr_t)fx_obj(t, s); h *= 1099511628211ull; }
        for (p = 1; p < MAX_PAT; p += 1) { h ^= notes_hash(p); h *= 1099511628211ull; }
        h ^= (uint64_t)g_nslot * 0x9E3779B97F4A7C15ull;
        if (h != sig) { sig = h; sigSince = now; }
    }
    if (g_projDoneQueued) return;
    if ((now - g_projSince >= 1000 && now - sigSince >= PROJ_SETTLE_MS) || now - g_projSince > PROJ_MAX_MS) {
        InterlockedExchange(&g_projDoneQueued, 1);
        enqueue_unit(T_PROJDONE, 0, 0, 0, 0, NULL);
    }
}

// ---------------- unit locks (mutexing) ----------------
// Notes of a pattern ("pat:<n>") and the clips of an arrangement ("clips:<a>") sync as whole
// units, so two people editing the same unit at once would overwrite each other. Whoever
// changes a unit first takes its lock (server-arbitrated; ops carry the key and the server
// drops ops on keys someone else holds). While someone else holds it, a local change is
// reverted to the last agreed state and FL's hint bar says who is editing. A lock is released
// ULOCK_IDLE_MS after its owner's last change. Params keep their own per-param locks.
#define ULOCK_MAX      2048
#define ULOCK_IDLE_MS  2000
#define HINT_EVERY_MS  1500
typedef struct {
    char  key[32];
    int   mine;              // we hold it (granted or optimistically requested)
    int   granted;           // the server confirmed it
    int   peer;              // someone else holds it
    DWORD lastLocal;         // our last change under it
    DWORD lastHint;
    int   stateSent;         // plugin key: a plug.state went out under it before the grant
    char  owner[64];
} ULock;
static ULock g_ulocks[ULOCK_MAX];
static int   g_nulocks = 0;
static CRITICAL_SECTION g_ulockLock;   // also guards the last/pending state buffers below
static char  g_peersJson[4096];        // last welcome/presence message (id -> name for hints)

// Last agreed state per unit (what a revert restores) and our sent-but-unconfirmed state.
typedef struct { unsigned char *d; unsigned n; int known; } UState;
static UState g_patLast[MAX_PAT], g_patPend[MAX_PAT];
static UState g_clipLast, g_clipPend;
static int    g_clipLastArr = -1;

static void ustate_set(UState *s, const unsigned char *d, unsigned n, size_t rec) {
    free(s->d);
    s->d = n ? (unsigned char *)malloc((size_t)n * rec) : NULL;
    if (n) memcpy(s->d, d, (size_t)n * rec);
    s->n = n; s->known = 1;
}
static void ustate_move(UState *dst, UState *src) { free(dst->d); *dst = *src; memset(src, 0, sizeof(*src)); }

static ULock *ulock_get(const char *key, int create) {
    int i;
    for (i = 0; i < g_nulocks; i++) if (strcmp(g_ulocks[i].key, key) == 0) return &g_ulocks[i];
    if (!create || g_nulocks >= ULOCK_MAX) return NULL;
    memset(&g_ulocks[g_nulocks], 0, sizeof(ULock));
    strcpy_s(g_ulocks[g_nulocks].key, sizeof(g_ulocks[0].key), key);
    return &g_ulocks[g_nulocks++];
}

static void peer_name(const char *id, char *out, int cap) {
    char pat[96];
    const char *p, *q;
    sprintf_s(pat, sizeof(pat), "\"id\":\"%s\",\"name\":\"", id);
    p = strstr(g_peersJson, pat);
    if (p && (p += strlen(pat), q = strchr(p, '"')) && q - p < cap) { memcpy(out, p, q - p); out[q - p] = 0; }
    else strcpy_s(out, cap, "another user");
}

// FL's hint bar (top left). Main thread. The string is kept: FL may hold on to it.
static void fl_hint(const char *msg) {
    wchar_t w[256];
    void *form = fl_form();
    if (!form) return;
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, w, 256);
    __try { ((hint_t)(g_flBase + (HINT_VA - IMG_BASE)))(form, dstr_keep(w), 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { logmsg("hint faulted"); }
}

// "plug:ch:<i>" / "plug:fx:<track>:<slot>" -> unit. Returns 0 if key is no plugin key.
static int plug_parse(const char *key, int *unit, int *a, int *b) {
    *b = 0;
    if (sscanf_s(key, "plug:ch:%d", a) == 1) { *unit = U_CH; return *a >= 0 && *a < MAX_CH; }
    if (sscanf_s(key, "plug:fx:%d:%d", a, b) == 2) { *unit = U_FX; return *a >= 0 && *a < MAX_MIX && *b >= 0 && *b < MAX_FX_SLOTS; }
    return 0;
}

static void unit_label(const char *key, char *out, int cap) {
    int unit, a, b;
    if (strncmp(key, "pat:", 4) == 0) sprintf_s(out, cap, "Pattern %d", atoi(key + 4));
    else if (strncmp(key, "clips:", 6) == 0) strcpy_s(out, cap, "The playlist");
    else if (plug_parse(key, &unit, &a, &b)) {
        if (unit == U_FX) _snprintf_s(out, cap, _TRUNCATE, "Mixer insert %d slot %d", a, b + 1);
        else {
            char name[48] = "";
            __try {
                const wchar_t *w = *(const wchar_t **)((char *)chan_obj(a) + 0x50);   // CH_NAME
                if (w && !WideCharToMultiByte(CP_UTF8, 0, w, -1, name, sizeof(name), NULL, NULL)) name[0] = 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) { name[0] = 0; }
            if (name[0]) _snprintf_s(out, cap, _TRUNCATE, "Channel '%s'", name);
            else _snprintf_s(out, cap, _TRUNCATE, "Channel %d", a + 1);
        }
    }
    else strcpy_s(out, cap, key);
}

static void hint_locked(ULock *l) {
    char who[64], what[96], msg[200];
    DWORD now = GetTickCount();
    if (now - l->lastHint < HINT_EVERY_MS) return;
    l->lastHint = now;
    peer_name(l->owner, who, sizeof(who));
    unit_label(l->key, what, sizeof(what));
    _snprintf_s(msg, sizeof(msg), _TRUNCATE, "%s is being edited by %s", what, who);
    fl_hint(msg);
}

// Before sending a local change of unit `key` (main thread). Returns 1 = go ahead (lock
// requested/held, op must carry the key), 0 = someone else holds it: caller reverts.
static int ulock_local(const char *key, int *already) {
    int r = 1;
    char m[96];
    EnterCriticalSection(&g_ulockLock);
    ULock *l = ulock_get(key, 1);
    *already = 0;
    if (!l) { LeaveCriticalSection(&g_ulockLock); return 1; }
    if (l->peer) { hint_locked(l); r = 0; }
    else {
        *already = l->granted;
        if (!l->mine) {
            l->mine = 1;
            sprintf_s(m, sizeof(m), "{\"t\":\"lock_acquire\",\"key\":\"%s\"}", key);
            ws_send(m);
        }
        l->lastLocal = GetTickCount();
    }
    LeaveCriticalSection(&g_ulockLock);
    return r;
}

// Poll thread: give back locks we stopped using.
static void ulock_release_idle(DWORD now) {
    int i;
    char m[96];
    EnterCriticalSection(&g_ulockLock);
    for (i = 0; i < g_nulocks; i++) {
        ULock *l = &g_ulocks[i];
        if (l->mine && now - l->lastLocal > ULOCK_IDLE_MS) {
            sprintf_s(m, sizeof(m), "{\"t\":\"lock_release\",\"key\":\"%s\"}", l->key);
            ws_send(m);
            l->mine = 0; l->granted = 0;
        }
    }
    LeaveCriticalSection(&g_ulockLock);
}

// State bookkeeping for a unit key. commit: our pending state becomes the agreed one.
static void unit_commit(const char *key) {
    if (strncmp(key, "pat:", 4) == 0) {
        int p = atoi(key + 4);
        if (p >= 1 && p < MAX_PAT && g_patPend[p].known) ustate_move(&g_patLast[p], &g_patPend[p]);
    } else if (strncmp(key, "clips:", 6) == 0 && g_clipPend.known) {
        ustate_move(&g_clipLast, &g_clipPend);
        g_clipLastArr = atoi(key + 6);
    }
}

// Main thread: restore the last agreed state of a unit (our change lost the lock race or
// was made while someone else held the lock).
static void unit_revert(const char *key) {
    unsigned char *d = NULL;
    unsigned n = 0;
    int known = 0, idx = -1, isPat = strncmp(key, "pat:", 4) == 0;
    int unit, a, b;
    if (plug_parse(key, &unit, &a, &b)) {
        // params were snapped back already (plug_undo); a state we sent was dropped by the server
        int sent = 0;
        EnterCriticalSection(&g_ulockLock);
        ULock *l = ulock_get(key, 0);
        if (l) { sent = l->stateSent; l->stateSent = 0; }
        LeaveCriticalSection(&g_ulockLock);
        if (sent) plug_revert_state(unit, a, b);
        logmsg("LOCK %s held by another user: local change reverted%s", key, sent ? " (state)" : "");
        return;
    }
    EnterCriticalSection(&g_ulockLock);
    if (isPat) {
        idx = atoi(key + 4);
        if (idx >= 1 && idx < MAX_PAT && g_patLast[idx].known) {
            known = 1; n = g_patLast[idx].n;
            if (n) { d = (unsigned char *)malloc((size_t)n * NOTE_SIZE); memcpy(d, g_patLast[idx].d, (size_t)n * NOTE_SIZE); }
            free(g_patPend[idx].d); memset(&g_patPend[idx], 0, sizeof(UState));
        }
    } else if (strncmp(key, "clips:", 6) == 0) {
        idx = atoi(key + 6);
        if (g_clipLast.known && g_clipLastArr == idx) {
            known = 1; n = g_clipLast.n;
            if (n) { d = (unsigned char *)malloc((size_t)n * CLIP_SIZE); memcpy(d, g_clipLast.d, (size_t)n * CLIP_SIZE); }
            free(g_clipPend.d); memset(&g_clipPend, 0, sizeof(UState));
        }
    }
    LeaveCriticalSection(&g_ulockLock);
    if (!known) { logmsg("LOCK %s: no agreed state to revert to", key); return; }
    if (isPat) notes_apply(idx, d, n); else clips_apply(idx, d, n);
    free(d);
    logmsg("LOCK %s held by another user: local change reverted", key);
}

static void do_unitrevert(const QItem *it) {
    unit_revert(it->path);
    EnterCriticalSection(&g_ulockLock);
    ULock *l = ulock_get(it->path, 0);
    if (l) hint_locked(l);
    LeaveCriticalSection(&g_ulockLock);
}

// Inbound lock messages for unit keys (recv thread). Returns 1 if handled.
static int on_unit_lock(const char *m) {
    const char *k = strstr(m, "\"key\":\"");
    char key[32], owner[64] = "";
    int i = 0, granted, released = strstr(m, "\"t\":\"lock_released\"") != NULL;
    if (!k) return 0;
    k += 7;
    if (strncmp(k, "pat:", 4) != 0 && strncmp(k, "clips:", 6) != 0 && strncmp(k, "plug:", 5) != 0) return 0;
    while (k[i] && k[i] != '"' && i < (int)sizeof(key) - 1) { key[i] = k[i]; i++; }
    key[i] = 0;
    granted = strstr(m, "\"granted\":true") != NULL;
    {
        const char *o = strstr(m, "\"owner\":\"");
        if (o) { o += 9; for (i = 0; o[i] && o[i] != '"' && i < (int)sizeof(owner) - 1; i++) owner[i] = o[i]; owner[i] = 0; }
    }
    EnterCriticalSection(&g_ulockLock);
    ULock *l = ulock_get(key, 1);
    if (l) {
        if (released) l->peer = 0;
        else if (strcmp(owner, g_myId) == 0) {
            if (granted) { l->mine = 1; l->granted = 1; l->peer = 0; l->stateSent = 0; unit_commit(key); plug_undo(key, 0); }
        } else {
            int lost = !granted;                     // denials go only to the side that asked: our change lost
            l->peer = 1; l->mine = 0; l->granted = 0;
            strcpy_s(l->owner, sizeof(l->owner), owner);
            if (lost) plug_undo(key, 1);             // plugin params we changed meanwhile: snap back
            if (lost) {
                QItem q; memset(&q, 0, sizeof(q));
                q.type = T_UNITREVERT; q.path = _strdup(key);
                enqueue(&q);
                logmsg("LOCK %s denied (held by %s): reverting our change", key, owner);
            }
        }
    }
    LeaveCriticalSection(&g_ulockLock);
    return 1;
}

// A remote change replaced a unit we had an unsent edit in: say who is editing it.
static void hint_if_held(const char *key) {
    EnterCriticalSection(&g_ulockLock);
    ULock *l = ulock_get(key, 0);
    if (l && l->peer) { hint_locked(l); logmsg("LOCK %s: our unsent edit was replaced by the lock holder's", key); }
    LeaveCriticalSection(&g_ulockLock);
}

// ---- plugin lock ----
// One lock per plugin (channel generator or mixer effect slot): "plug:ch:<i>" / "plug:fx:<t>:<s>".
// Any local change of the plugin takes it (a param tweak or a state change); param.set and
// plug.state ops carry it, so the server drops them while a peer holds it. While a peer holds
// it, local param tweaks snap back and a local state change is undone by reloading the last
// agreed state. A denied request (race) snaps back the params changed while it was pending.
static UState g_plugLastCh[MAX_CH];
static UState g_plugLastFx[MAX_MIX][MAX_FX_SLOTS];
static volatile DWORD g_hintQueuedAt = 0;

static void plug_key(int unit, int a, int b, char *key, int cap) {
    if (unit == U_CH) sprintf_s(key, cap, "plug:ch:%d", a);
    else sprintf_s(key, cap, "plug:fx:%d:%d", a, b);
}
static UState *plug_last(int unit, int a, int b) {
    if (unit == U_CH) return (a >= 0 && a < MAX_CH) ? &g_plugLastCh[a] : NULL;
    return (a >= 0 && a < MAX_MIX && b >= 0 && b < MAX_FX_SLOTS) ? &g_plugLastFx[a][b] : NULL;
}
// Plugin param id -> its unit. 0 for channel/mixer basics (vol, pan, route...): those keep per-param locks.
static int plug_param_unit(uint32_t id, int *unit, int *a, int *b) {
    return !IS_MUTE_ID(id) && (id & 0xFFFF) >= REC_PLUG_FIRST && unit_of_id(id, unit, a, b);
}

// Last agreed state of a plugin (.fst bytes): set on baseline, send and remote apply. NULL clears.
static void plug_agreed_set(int unit, int a, int b, const unsigned char *d, size_t n) {
    EnterCriticalSection(&g_ulockLock);
    UState *s = plug_last(unit, a, b);
    if (s) {
        if (d && n) ustate_set(s, d, (unsigned)n, 1);
        else { free(s->d); memset(s, 0, sizeof(*s)); }
    }
    LeaveCriticalSection(&g_ulockLock);
}
// Channel indices moved / project replaced: stored states no longer belong to their index.
static void plug_agreed_reset(int channels, int fx) {
    int i, j;
    EnterCriticalSection(&g_ulockLock);
    if (channels) for (i = 0; i < MAX_CH; i++) { free(g_plugLastCh[i].d); memset(&g_plugLastCh[i], 0, sizeof(UState)); }
    if (fx)
        for (i = 0; i < MAX_MIX; i++)
            for (j = 0; j < MAX_FX_SLOTS; j++) { free(g_plugLastFx[i][j].d); memset(&g_plugLastFx[i][j], 0, sizeof(UState)); }
    LeaveCriticalSection(&g_ulockLock);
}

static int load_queued(int unit, int a, int b) {
    int i, r = 0;
    EnterCriticalSection(&g_qLock);
    for (i = g_qh; i != g_qt && !r; i = (i + 1) % QCAP)
        r = g_q[i].type == T_LOAD && g_q[i].unit == unit && g_q[i].a == a && g_q[i].b == b;
    LeaveCriticalSection(&g_qLock);
    return r;
}

// Main thread: reload the plugin's last agreed state (our state change lost to the lock holder).
static void plug_revert_state(int unit, int a, int b) {
    unsigned char *d = NULL;
    unsigned n = 0;
    if (load_queued(unit, a, b)) return;         // the holder's newer state is about to replace ours anyway
    EnterCriticalSection(&g_ulockLock);
    UState *s = plug_last(unit, a, b);
    if (s && s->known && s->n) { d = (unsigned char *)malloc(s->n); memcpy(d, s->d, s->n); n = s->n; }
    LeaveCriticalSection(&g_ulockLock);
    if (!d) { logmsg("PLUG %s %d/%d: no agreed state to revert to", unit == U_CH ? "ch" : "fx", a, b); return; }
    char *path = tmp_path("rv");
    FILE *f = NULL;
    if (fopen_s(&f, path, "wb") == 0 && f) {
        fwrite(d, 1, n, f); fclose(f);
        enqueue_unit(T_LOAD, unit, a, b, 0, path);
        logmsg("PLUG %s %d/%d: reverting to the agreed state (%u bytes)", unit == U_CH ? "ch" : "fx", a, b, n);
    } else free(path);
    free(d);
}

// Main thread, before sending a plug.state: 1 = go ahead under *key, 0 = a peer holds the plugin.
static int plug_lock_send(int unit, int a, int b, char *key, int cap) {
    int already;
    plug_key(unit, a, b, key, cap);
    if (!ulock_local(key, &already)) return 0;
    if (!already) {
        EnterCriticalSection(&g_ulockLock);
        ULock *l = ulock_get(key, 0);
        if (l) l->stateSent = 1;
        LeaveCriticalSection(&g_ulockLock);
    }
    return 1;
}

// Poll thread, a local plugin param change: 1 = send it under *key, 0 = a peer holds the
// plugin (the caller snaps the param back). Changes made before the grant remember their
// previous value in case the request is denied.
static int plug_lock_param(int unit, int a, int b, Slot *s, uint32_t prev, char *key, int cap) {
    int r = 1;
    char m[96];
    plug_key(unit, a, b, key, cap);
    EnterCriticalSection(&g_ulockLock);
    ULock *l = ulock_get(key, 1);
    if (l && l->peer) r = 0;
    else if (l) {
        if (!l->mine) {
            l->mine = 1;
            sprintf_s(m, sizeof(m), "{\"t\":\"lock_acquire\",\"key\":\"%s\"}", key);
            ws_send(m);
        }
        if (!l->granted && !s->hasUndo) { s->undo = prev; s->hasUndo = 1; }
        l->lastLocal = GetTickCount();
    }
    LeaveCriticalSection(&g_ulockLock);
    return r;
}

// Lock answer for a plugin key: granted -> forget the undo values; denied -> apply them.
static void plug_undo(const char *key, int apply) {
    int unit, a, b, k, n = 0;
    if (!plug_parse(key, &unit, &a, &b)) return;
    for (k = 0; k < g_nslot; k++) {
        Slot *s = &g_slots[k];
        int u2, a2, b2;
        if (!s->hasUndo || !plug_param_unit(s->id, &u2, &a2, &b2) || u2 != unit || a2 != a || b2 != b) continue;
        if (apply) { enqueue_apply(s->id, s->undo); n++; }
        s->hasUndo = 0;
    }
    if (n) logmsg("LOCK %s denied: %d params snapped back", key, n);
}

// Poll thread: show the hint for a held plugin (throttled; FL calls happen on the main thread).
static void enqueue_hint(const char *key) {
    DWORD now = GetTickCount();
    if (now - g_hintQueuedAt < HINT_EVERY_MS) return;
    g_hintQueuedAt = now;
    QItem q; memset(&q, 0, sizeof(q));
    q.type = T_HINT; q.path = _strdup(key);
    enqueue(&q);
}

static void do_hint(const QItem *it) {
    EnterCriticalSection(&g_ulockLock);
    ULock *l = ulock_get(it->path, 0);
    if (l && l->peer) hint_locked(l);
    LeaveCriticalSection(&g_ulockLock);
}

// Hooks for the notes / clips code ------------------------------------------------------
// Local change of a unit about to be sent (main thread). d = the records being sent.
// Returns 0 if someone else holds the unit: the caller reverts instead of sending.
static int unit_before_send(const char *key, int isPat, int idx, const unsigned char *d, unsigned n) {
    int already;
    if (!ulock_local(key, &already)) return 0;
    EnterCriticalSection(&g_ulockLock);
    ustate_set(isPat ? &g_patPend[idx] : &g_clipPend, d, n, isPat ? NOTE_SIZE : CLIP_SIZE);
    if (already) unit_commit(key);               // lock already ours: this state is agreed now
    LeaveCriticalSection(&g_ulockLock);
    return 1;
}
// A unit was replaced from outside (remote op / revert): that state is the agreed one.
static void unit_agreed(int isPat, int idx, const unsigned char *d, unsigned n) {
    EnterCriticalSection(&g_ulockLock);
    if (isPat) { if (idx >= 1 && idx < MAX_PAT) ustate_set(&g_patLast[idx], d, n, NOTE_SIZE); }
    else { ustate_set(&g_clipLast, d, n, CLIP_SIZE); g_clipLastArr = idx; }
    LeaveCriticalSection(&g_ulockLock);
}
// Baseline (start, project load, structural ops): the current state of every unit is agreed.
// Unlocked, SEH-guarded reads (as the hash scan does); selection bits cleared.
static void units_capture_all(void) {
    int p;
    unsigned i;
    EnterCriticalSection(&g_ulockLock);
    for (p = 1; p < MAX_PAT; p++) {
        __try {
            char *a = pat_notes_arr(p);
            unsigned n = a ? *(unsigned *)(a + NOTES_COUNT) : 0;
            if (n > 1000000) n = 0;
            ustate_set(&g_patLast[p], n ? *(unsigned char **)(a + NOTES_DATA) : NULL, n, NOTE_SIZE);
            for (i = 0; i < n; i++) g_patLast[p].d[(size_t)i * NOTE_SIZE + NOTE_SEL_OFS] &= (unsigned char)~NOTE_SEL_BIT;
        } __except (EXCEPTION_EXECUTE_HANDLER) { g_patLast[p].known = 0; }
    }
    __try {
        int arr = cur_arr();
        char *ca = arr_clips(arr);
        unsigned n = ca ? *(unsigned *)(ca + CLIPS_COUNT) : 0;
        if (n > 200000) n = 0;
        ustate_set(&g_clipLast, n ? *(unsigned char **)(ca + CLIPS_DATA) : NULL, n, CLIP_SIZE);
        for (i = 0; i < n; i++) g_clipLast.d[(size_t)i * CLIP_SIZE + CLIP_SEL_OFS] &= (unsigned char)~CLIP_SEL_BIT;
        g_clipLastArr = arr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_clipLast.known = 0; }
    LeaveCriticalSection(&g_ulockLock);
}

static void run_task(const QItem *it) {
    int remoteHeavy = it->type == T_LOAD || it->type == T_ADDCH || it->type == T_DELCH || it->type == T_FXCLEAR;
    if (remoteHeavy) wguard_begin();
    run_task_inner(it);
    if (remoteHeavy) wguard_pass();
}

static void run_task_inner(const QItem *it) {
    switch (it->type) {
    case T_SNAP:   do_snap(it); break;
    case T_LOAD:   do_load(it); DeleteFileA(it->path); free(it->path); break;
    case T_ADDCH:  do_addch(it); break;      // hands it->path on to a T_LOAD
    case T_DELCH:  do_delch(it); break;
    case T_STRUCT: do_struct(); break;
    case T_FULL:   do_full(); break;
    case T_ORDER:  do_order(it); free(it->order); break;
    case T_FXCLEAR: do_fxclear(it); break;
    case T_FXSYNC: do_fxsync(it); free(it->order); break;
    case T_NOTESNAP: do_notesnap(it); break;
    case T_NOTES:  do_notes(it); free(it->order); break;
    case T_NOTESYNC: do_notesync(it); free(it->order); break;
    case T_PROJLOAD: do_projload(it); free(it->path); break;
    case T_PROJDONE: do_projdone(); break;
    case T_PROJSNAP: do_projsnap(); break;
    case T_CLIPSNAP: do_clipsnap(it); break;
    case T_CLIPS:  do_clips(it); free(it->order); break;
    case T_UNITREVERT: do_unitrevert(it); free(it->path); break;
    case T_HINT:   do_hint(it); free(it->path); break;
    case T_METASNAP: do_metasnap(it); break;
    case T_META:   do_meta(it); free(it->path); break;
    }
}

// ---------------- main-thread apply ----------------
static volatile LONG g_hookCalls = 0;

static void drain_queue_mainthread(void) {
    if (InterlockedExchange(&g_inHook, 1)) return;     // FL may pump messages inside a plugin load
    if (g_wguardUntil && GetTickCount() - g_wguardLast >= 30) wguard_pass();
    if (g_uiWanted && InterlockedExchange(&g_uiWanted, 0)) ui_create();
    for (;;) {
        QItem it;
        EnterCriticalSection(&g_qLock);
        if (g_qh == g_qt) { LeaveCriticalSection(&g_qLock); break; }
        it = g_q[g_qh]; g_qh = (g_qh + 1) % QCAP;
        LeaveCriticalSection(&g_qLock);

        if (g_projBusy && it.type != T_PROJLOAD && it.type != T_PROJDONE && it.type != T_PROJSNAP) {
            if (it.type == T_LOAD || it.type == T_ADDCH) { if (it.path) { DeleteFileA(it.path); free(it.path); } }
            else if (it.type == T_UNITREVERT || it.type == T_PROJLOAD || it.type == T_HINT || it.type == T_META) free(it.path);
            else if (it.type == T_ORDER || it.type == T_FXSYNC || it.type == T_NOTES || it.type == T_NOTESYNC || it.type == T_CLIPS) free(it.order);
            continue;
        }

        if (it.type != T_PARAM) {
            DWORD t0 = GetTickCount();
            run_task(&it);
            DWORD dt = GetTickCount() - t0;
            if (dt > 50) logmsg("SLOW main-thread task type=%d unit=%d %d/%d took %lu ms", it.type, it.unit, it.a, it.b, dt);
            // one heavy FL call per tick; come back for the rest
            if (g_qh != g_qt && g_mainWnd) PostMessageW(g_mainWnd, WM_NULL, 0, 0);
            break;
        }
        Slot *s = slot_by_id(it.id);
        if (s) {
            // Record the value before writing so the poller sees no change, and bump
            // gen so any poll that raced with this write is discarded.
            EnterCriticalSection(&g_slotLock);
            s->last = it.v;
            InterlockedIncrement(&s->gen);
            LeaveCriticalSection(&g_slotLock);
        }
        uint32_t r = write_value(it.id, it.v);
        if (s) InterlockedIncrement(&s->gen);
        logmsg("APPLY id=0x%08x v=%u ret=%u", it.id, it.v, r);
    }
    InterlockedExchange(&g_inHook, 0);
}

static LRESULT CALLBACK getMsgHook(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && lp) {
        const MSG *msg = (const MSG *)lp;
        switch (msg->message) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MOUSEWHEEL: case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_LBUTTONDBLCLK:
            g_lastInput = GetTickCount();
            break;
        case WM_MOUSEMOVE:
            if (msg->wParam & (MK_LBUTTON | MK_RBUTTON)) g_lastInput = GetTickCount();   // drags
            break;
        case WM_FL_OPENPROJ:
            if (msg->hwnd == g_mainForm) proj_begin("FL is opening a project");
            break;
        case WM_QUIT:
            g_closingUntil = GetTickCount() + 30000;
            proj_begin("FL is quitting");
            break;
        }
    }
    g_lastHookTick = GetTickCount();
    InterlockedIncrement(&g_hookCalls);
    if (code >= 0) drain_queue_mainthread();
    return CallNextHookEx(g_hook, code, wp, lp);
}

// Sent messages: closing FL (or Windows logoff) must never be broadcast as edits.
static HHOOK g_hookCwp = NULL;
static LRESULT CALLBACK callWndHook(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && lp) {
        const CWPSTRUCT *c = (const CWPSTRUCT *)lp;
        if ((c->message == WM_CLOSE && (c->hwnd == g_mainForm || c->hwnd == g_mainWnd)) ||
            c->message == WM_QUERYENDSESSION || c->message == WM_ENDSESSION) {
            if (!closing_now()) logmsg("FL is closing (msg 0x%x): structural changes will not be sent", c->message);
            g_closingUntil = GetTickCount() + 30000;
            proj_begin("FL is closing");
        }
    }
    return CallNextHookEx(g_hookCwp, code, wp, lp);
}

static BOOL CALLBACK findMainWnd(HWND h, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetWindow(h, GW_OWNER) == NULL) {
        *(HWND *)lp = h;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK findMainForm(HWND h, LPARAM lp) {
    DWORD pid = 0;
    char c[64];
    GetWindowThreadProcessId(h, &pid);
    GetClassNameA(h, c, sizeof(c));
    if (pid == GetCurrentProcessId() && strcmp(c, "TFruityLoopsMainForm") == 0) { *(HWND *)lp = h; return FALSE; }
    return TRUE;
}

static int install_mainthread_hook(void) {
    HWND h = NULL;
    int tries;
    for (tries = 0; tries < 600 && !h; tries++) { EnumWindows(findMainWnd, (LPARAM)&h); if (!h) Sleep(100); }
    if (!h) { logmsg("main window not found"); return -1; }
    g_mainWnd = h;
    DWORD tid = GetWindowThreadProcessId(h, NULL);
    g_hook = SetWindowsHookExW(WH_GETMESSAGE, getMsgHook, g_hinst, tid);
    g_hookCwp = SetWindowsHookExW(WH_CALLWNDPROC, callWndHook, g_hinst, tid);
    char cls[128] = { 0 }; GetClassNameA(h, cls, sizeof(cls));
    logmsg("main window %p class=%s tid=%lu hook=%p", (void *)h, cls, tid, (void *)g_hook);
    return g_hook ? 0 : -1;
}

// ---------------- detect ----------------
static void send_lock(const char *t, uint32_t id) {
    char m[96];
    sprintf_s(m, sizeof(m), "{\"t\":\"%s\",\"key\":\"param:%u\"}", t, id);
    ws_send(m);
}

// Which "container" an id belongs to (a channel, a mixer track, one effect slot).
// Used to tell a project load (many containers change at once) from a knob
// tweak or a preset change (one container).
static uint32_t container_of(uint32_t id) {
    if (IS_MUTE_ID(id)) return 0x20000u | (id & 0xFFFF);
    if (id >= 0x70000000u) return 0x10000u | ((id - 0x70000000u) >> 16);
    return id >> 16;
}

#define BULK_MIN_CHANGES    24
#define BULK_MIN_CONTAINERS 4

typedef struct { Slot *s; uint32_t prev, v; } Change;
static Change g_changes[MAX_SLOTS];

// True if FL's channel list no longer matches what the main thread last recorded.
static int chan_list_changed(void) {
    void **items; int n = chan_list(&items), i;
    if (g_nChKnown < 0 || !items) return 0;
    if (n != g_nChKnown) return 1;
    for (i = 0; i < n; i++) if (items[i] != g_chKnown[i]) return 1;
    return 0;
}

// Decide when units need a state snapshot (runs on the poll thread; the
// snapshots themselves are queued for the main thread).
static void schedule_units(DWORD now) {
    static DWORD lastSweep = 0, lastStruct = 0;
    static int sweepPos = 0;
    int i, t, s, nch = g_nChKnown, nm = mix_count();
    if (now - lastStruct >= STRUCT_MS) { enqueue_unit(T_STRUCT, 0, 0, 0, 0, NULL); lastStruct = now; }
    if (nch < 0) return;
    for (i = 0; i < nch + nm * MAX_FX_SLOTS; i++) {
        int unit = i < nch ? U_CH : U_FX;
        int a = i < nch ? i : (i - nch) / MAX_FX_SLOTS, b = i < nch ? 0 : (i - nch) % MAX_FX_SLOTS;
        Unit *u = unit_ptr(unit, a, b);
        if (!u) continue;
        if (unit == U_FX && !u->inst && !u->identAt) continue;
        if (u->addAt && now - u->addAt > SETTLE_IDENT_MS) {
            u->addAt = 0; u->identAt = 0; u->lastAct = 0;
            enqueue_unit(T_SNAP, unit, a, b, SNAP_ADD, NULL);
        } else if (u->identAt && now - u->identAt > SETTLE_IDENT_MS) {
            u->identAt = 0; u->lastAct = 0;
            enqueue_unit(T_SNAP, unit, a, b, SNAP_FORCE, NULL);
        } else if (!u->identAt && u->lastAct && now - u->lastAct > SETTLE_ACT_MS) {
            // a preset-sized burst may carry state params don't cover; plain knob
            // tweaks only move the baseline (a full reload on the peer would glitch audio)
            enqueue_unit(T_SNAP, unit, a, b, u->burst ? SNAP_IF_DIFF : SNAP_BASELINE, NULL);
            u->lastAct = 0; u->burst = 0;
        } else if (u->baselineAt && (LONG)(now - u->baselineAt) >= 0) {
            u->baselineAt = 0;
            enqueue_unit(T_SNAP, unit, a, b, SNAP_BASELINE, NULL);
        }
    }
    // Round-robin sweep: catches state changes that touch no params (e.g. Serum wavetables).
    if (now - lastSweep >= SWEEP_MS && g_lastInput && now - g_lastInput < INPUT_RECENT_MS) {
        int total = nch + nm * MAX_FX_SLOTS, tries;
        lastSweep = now;
        for (tries = 0; tries < total; tries++) {
            int k = (sweepPos++) % (total ? total : 1);
            int unit = k < nch ? U_CH : U_FX;
            int a = k < nch ? k : (k - nch) / MAX_FX_SLOTS, b = k < nch ? 0 : (k - nch) % MAX_FX_SLOTS;
            Unit *u = unit_ptr(unit, a, b);
            if (!u || (unit == U_FX && !u->inst)) continue;
            if (u->lastAct || u->identAt || u->addAt || u->baselineAt || now < u->suppressUntil) continue;
            enqueue_unit(T_SNAP, unit, a, b, u->hash ? SNAP_IF_DIFF : SNAP_BASELINE, NULL);
            break;
        }
    }
    (void)t; (void)s;
}

static DWORD WINAPI pollThread(LPVOID p) {
    (void)p;
    DWORD lastScan = GetTickCount(), lastBeat = GetTickCount();
    LONG seenGen = g_structGen;
    for (;;) {
        DWORD now = GetTickCount();
        if (now - lastBeat > 30000) {
            int depth = (g_qt - g_qh + QCAP) % QCAP;
            logmsg("BEAT queue=%d inHook=%ld hookCalls=%ld chanChanged=%d known=%d", depth, g_inHook,
                   InterlockedExchange(&g_hookCalls, 0), chan_list_changed(), g_nChKnown);
            lastBeat = now;
        }
        int i, n, nc = 0;
        LONG gen0 = g_structGen;
        ulock_release_idle(now);
        if (g_projBusy) { proj_poll(now); Sleep(POLL_MS); continue; }
        if (chan_list_changed()) {
            // A channel was added/removed and the main thread hasn't processed it yet:
            // REC ids of later channels have shifted, so diffs would be garbage.
            int k;
            for (k = 0; k < g_nslot; k++) g_slots[k].last = read_value(g_slots[k].id);
            schedule_units(now);
            Sleep(POLL_MS);
            continue;
        }
        if (gen0 != seenGen) {
            // channel indices shifted (delete): re-read everything silently
            int k;
            for (k = 0; k < g_nslot; k++) g_slots[k].last = read_value(g_slots[k].id);
            seenGen = gen0;
        }
        if (now - lastScan > RESCAN_MS) { rescan(); retry_pending(); lastScan = now; }
        scan_notes(now);
        scan_clips(now);
        scan_meta(now);
        n = g_nslot;
        for (i = 0; i < n; i++) {
            Slot *s = &g_slots[i];
            LONG g0 = s->gen;
            uint32_t v = read_value(s->id);
            if (s->gen != g0) continue;                 // raced with an apply; re-read next tick
            EnterCriticalSection(&g_slotLock);
            uint32_t prev = s->last;
            if (v != prev) s->last = v;
            LeaveCriticalSection(&g_slotLock);
            if (v != prev && v != REC_NONE && prev != REC_NONE) {
                int unit, a, b;
                Unit *u = unit_of_id(s->id, &unit, &a, &b) ? unit_ptr(unit, a, b) : NULL;
                if (u && now < u->suppressUntil) continue;   // caused by a remote state load
                g_changes[nc].s = s; g_changes[nc].prev = prev; g_changes[nc].v = v; nc++;
            }
            if (s->lockedByMe && now - s->lastTouch > LOCK_IDLE_MS) {
                send_lock("lock_release", s->id);
                s->lockedByMe = 0;
            }
        }
        if (g_structGen != gen0) { Sleep(POLL_MS); continue; }   // indices moved mid-tick

        // per-unit activity: remember it, and flag preset-sized bursts
        // (a huge tick is a project load, handled by the bulk guard below)
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
        schedule_units(now);

        if (nc >= BULK_MIN_CHANGES) {
            uint32_t seen[BULK_MIN_CONTAINERS];
            int ns = 0, j;
            for (i = 0; i < nc && ns < BULK_MIN_CONTAINERS; i++) {
                uint32_t c = container_of(g_changes[i].s->id);
                for (j = 0; j < ns && seen[j] != c; j++) {}
                if (j == ns) seen[ns++] = c;
            }
            if (ns >= BULK_MIN_CONTAINERS) {
                // Project load/new/revert: never broadcast as edits (see project sync).
                logmsg("BULK local change (%d values, >=%d containers)", nc, ns);
                proj_begin("bulk param change");
                Sleep(POLL_MS);
                continue;
            }
        }

        for (i = 0; i < nc; i++) {
            Slot *s = g_changes[i].s;
            char key[32];
            int unit, a, b;
            if (plug_param_unit(s->id, &unit, &a, &b)) {
                // plugin params go under their plugin's lock
                if (!plug_lock_param(unit, a, b, s, g_changes[i].prev, key, sizeof(key))) {
                    logmsg("LOCK %s held by another user: param 0x%08x snapped back", key, s->id);
                    enqueue_apply(s->id, g_changes[i].prev);
                    enqueue_hint(key);
                    continue;
                }
            } else {
                if (s->lockedByPeer) {
                    // someone else holds this knob: snap our local edit back
                    enqueue_apply(s->id, g_changes[i].prev);
                    continue;
                }
                if (!s->lockedByMe) { send_lock("lock_acquire", s->id); s->lockedByMe = 1; }
                s->lastTouch = now;
                sprintf_s(key, sizeof(key), "param:%u", s->id);
            }
            char op[160];
            sprintf_s(op, sizeof(op),
                "{\"t\":\"op\",\"op\":{\"kind\":\"param.set\",\"key\":\"%s\",\"id\":%u,\"v\":%u}}",
                key, s->id, g_changes[i].v);
            ws_send(op);
            logmsg("DETECT id=0x%08x %u -> %u (sent)", s->id, g_changes[i].prev, g_changes[i].v);
        }
        Sleep(POLL_MS);
    }
}

// ---------------- inbound ----------------
static int jget_u32(const char *s, const char *name, uint32_t *out) {
    char pat[32]; sprintf_s(pat, sizeof(pat), "\"%s\":", name);
    const char *p = strstr(s, pat);
    if (!p) return 0;
    *out = (uint32_t)strtoul(p + strlen(pat), NULL, 10);
    return 1;
}
static int jget_paramkey(const char *s, uint32_t *id) {
    const char *p = strstr(s, "\"key\":\"param:");
    if (!p) return 0;
    *id = (uint32_t)strtoul(p + strlen("\"key\":\"param:"), NULL, 10);
    return 1;
}

// Leader: send every known value so a joining member converges to our state.
static void send_full_state(void) {
    int i, n = 0, cap = 64 + g_nslot * 24;
    char *buf = (char *)malloc(cap);
    int o = sprintf_s(buf, cap, "{\"t\":\"op\",\"op\":{\"kind\":\"state.full\",\"vals\":[");
    for (i = 0; i < g_nslot; i++) {
        uint32_t v = g_slots[i].last;
        if (v == REC_NONE) continue;
        o += sprintf_s(buf + o, cap - o, "%s[%u,%u]", n ? "," : "", g_slots[i].id, v);
        n++;
    }
    sprintf_s(buf + o, cap - o, "]}}");
    ws_send(buf);
    free(buf);
    logmsg("STATE sent %d values", n);
}

// Inbound values for ids this FL doesn't have yet (still loading, plugin not
// scanned yet) wait here and are retried after each rescan.
#define MAX_PEND 8192
typedef struct { uint32_t id, v; } Pend;
static Pend g_pend[MAX_PEND];
static int  g_npend = 0;

static void pend_put(uint32_t id, uint32_t v) {
    int i;
    EnterCriticalSection(&g_qLock);
    for (i = 0; i < g_npend && g_pend[i].id != id; i++) {}
    if (i < g_npend) g_pend[i].v = v;
    else if (g_npend < MAX_PEND) { g_pend[g_npend].id = id; g_pend[g_npend].v = v; g_npend++; }
    LeaveCriticalSection(&g_qLock);
}

// Apply an inbound value (queued for the main thread). onlyIfDiff skips values
// we already have (join state). Returns 1 if an apply was queued.
static int route_inbound(uint32_t id, uint32_t v, int onlyIfDiff) {
    if (!add_slot(id)) { pend_put(id, v); return 0; }
    Slot *s = slot_by_id(id);
    if (onlyIfDiff && s && s->last == v) return 0;
    enqueue_apply(id, v);
    return 1;
}

static void retry_pending(void) {
    int i, kept = 0, applied = 0;
    EnterCriticalSection(&g_qLock);
    for (i = 0; i < g_npend; i++) {
        Pend p = g_pend[i];
        if (add_slot(p.id)) {
            Slot *s = slot_by_id(p.id);
            if (!s || s->last != p.v) { enqueue_apply(p.id, p.v); applied++; }
        } else g_pend[kept++] = p;
    }
    g_npend = kept;
    LeaveCriticalSection(&g_qLock);
    if (applied) logmsg("pending: applied %d, still waiting %d", applied, kept);
}

// Member: apply a state.full payload ("vals":[[id,v],...]).
static void apply_full_state(const char *m) {
    const char *p = strstr(m, "\"vals\":[");
    int n = 0, diff = 0;
    if (!p) return;
    p += strlen("\"vals\":[");
    while ((p = strchr(p, '[')) != NULL) {
        char *e;
        uint32_t id = (uint32_t)strtoul(p + 1, &e, 10);
        if (*e != ',') break;
        uint32_t v = (uint32_t)strtoul(e + 1, &e, 10);
        if (route_inbound(id, v, 1)) diff++;
        n++;
        p = e;
    }
    logmsg("STATE received %d values, %d differ (applying)", n, diff);
}

// ---- inbound plugin state ----
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Decode base64 [s, s+n) into a new temp .fst; returns its path (malloc'd) or NULL.
static char *b64_to_tmpfile(const char *s, size_t n) {
    unsigned char *out = (unsigned char *)malloc(n / 4 * 3 + 3);
    size_t o = 0, i;
    int acc = 0, bits = 0;
    for (i = 0; i < n; i++) {
        int v = b64val(s[i]);
        if (v < 0) continue;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (unsigned char)((acc >> bits) & 0xff); }
    }
    samples_rewrite(&out, &o);
    char *path = tmp_path("in");
    FILE *f = NULL;
    if (fopen_s(&f, path, "wb") != 0 || !f) { free(out); free(path); return NULL; }
    fwrite(out, 1, o, f); fclose(f); free(out);
    return path;
}

static int int_after(const char *p, const char *end, const char *key, int *out) {
    const char *q = strstr(p, key);
    if (!q || (end && q > end)) return 0;
    *out = atoi(q + strlen(key));
    return 1;
}

// Parse one unit object starting at p ("unit":..., ids..., "data":"..."). Returns
// pointer past it, or NULL. Fields are always written in this order by unit_json_fields.
static const char *parse_unit(const char *p, int *unit, int *a, int *b, char **path) {
    const char *u = strstr(p, "\"unit\":\"");
    const char *d = u ? strstr(u, "\"data\":\"") : NULL;
    if (!u || !d) return NULL;
    *unit = strncmp(u + 8, "fx", 2) == 0 ? U_FX : U_CH;
    *a = *b = 0;
    if (*unit == U_CH) int_after(u, d, "\"ch\":", a);
    else { int_after(u, d, "\"trk\":", a); int_after(u, d, "\"slot\":", b); }
    d += 8;
    const char *e = strchr(d, '"');
    if (!e) return NULL;
    *path = b64_to_tmpfile(d, (size_t)(e - d));
    return e + 1;
}

// Member: converge channels and plugin states to the leader's.
static void apply_plug_full(const char *m) {
    int nch = 0, unit, a, b, items = 0, k;
    char *path;
    void **list;
    int local = chan_list(&list);
    int_after(m, NULL, "\"nch\":", &nch);
    for (k = local - 1; k >= nch; k--) enqueue_unit(T_DELCH, U_CH, k, 0, 0, NULL);  // extra channels
    if (local > nch) local = nch;
    const char *p = strstr(m, "\"items\":[");
    unsigned char *keep = (unsigned char *)calloc(MAX_MIX * MAX_FX_SLOTS, 1);
    while (p && (p = parse_unit(p, &unit, &a, &b, &path)) != NULL) {
        if (!path) continue;
        if (unit == U_FX && a >= 0 && a < MAX_MIX && b >= 0 && b < MAX_FX_SLOTS) keep[a * MAX_FX_SLOTS + b] = 1;
        if (unit == U_CH && a >= local) { enqueue_unit(T_ADDCH, U_CH, a, 0, 0, path); local++; }
        else enqueue_unit(T_LOAD, unit, a, b, 1, path);
        items++;
    }
    {
        QItem q; memset(&q, 0, sizeof(q));
        q.type = T_FXSYNC; q.order = (int *)keep;
        enqueue(&q);
    }
    logmsg("PLUG full state received: leader has %d channels, %d units queued", nch, items);
}

static void handle_msg(const char *m) {
    uint32_t id, v;
    int unit, a, b;
    char *path;
    if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"project.load\"")) {
        if (!g_isLeader) on_project_load(m);
        return;
    }
    if (hold_if_loading(m)) return;
    if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"state.req\"")) {
        // join / member re-sync: send the project (mid-load, the load's own broadcast covers it)
        if (g_isLeader && !g_projBusy) enqueue_unit(T_PROJSNAP, 0, 0, 0, 0, NULL);
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"plug.full\"")) {
        if (!g_isLeader) apply_plug_full(m);
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"plug.state\"")) {
        if (parse_unit(m, &unit, &a, &b, &path) && path) enqueue_unit(T_LOAD, unit, a, b, 0, path);
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"chan.add\"")) {
        if (parse_unit(m, &unit, &a, &b, &path) && path) enqueue_unit(T_ADDCH, U_CH, a, 0, 0, path);
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"chan.del\"")) {
        if (int_after(m, NULL, "\"ch\":", &a)) enqueue_unit(T_DELCH, U_CH, a, 0, 0, NULL);
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"notes.set\"")) {
        parse_notes_item(strstr(m, "\"kind\":\"notes.set\""), NULL);
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"clips.set\"")) {
        on_clips_set(m);
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"meta.set\"")) {
        on_meta_set(m);
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"notes.full\"")) {
        if (!g_isLeader) {
            unsigned char *keep = (unsigned char *)calloc(MAX_PAT, 1);
            const char *q = strstr(m, "\"items\":[");
            int cnt = 0;
            while (q && (q = parse_notes_item(q, keep)) != NULL) cnt++;
            QItem it; memset(&it, 0, sizeof(it));
            it.type = T_NOTESYNC; it.order = (int *)keep;
            enqueue(&it);
            logmsg("NOTES full state received: %d patterns", cnt);
        }
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"fx.clear\"")) {
        if (int_after(m, NULL, "\"trk\":", &a) && int_after(m, NULL, "\"slot\":", &b))
            enqueue_unit(T_FXCLEAR, U_FX, a, b, 0, NULL);
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"chan.order\"")) {
        int n = 0;
        const char *f = strstr(m, "\"from\":[");
        if (int_after(m, NULL, "\"n\":", &n) && f && n > 0 && n <= MAX_CH) {
            QItem q; memset(&q, 0, sizeof(q));
            q.type = T_ORDER;
            q.order = (int *)malloc(sizeof(int) * (size_t)(n + 1));
            q.order[0] = n;
            f += 8;
            for (a = 0; a < n; a++) { q.order[a + 1] = (int)strtol(f, (char **)&f, 10); if (*f == ',') f++; }
            enqueue(&q);
        }
    } else if (strstr(m, "\"t\":\"op\"") && strstr(m, "\"kind\":\"state.full\"")) {
        if (!g_isLeader) apply_full_state(m);
    } else if (strstr(m, "\"t\":\"welcome\"")) {
        strncpy_s(g_peersJson, sizeof(g_peersJson), m, _TRUNCATE);
        logmsg("server: %.300s", m);
        // our own role is the first "role" field; later ones belong to the peers list
        const char *r = strstr(m, "\"role\":\"");
        g_isLeader = r && strncmp(r + 8, "leader", 6) == 0;
        net_show_connected(m);
        if (!g_isLeader) ws_send("{\"t\":\"op\",\"op\":{\"kind\":\"state.req\"}}");
    } else if (strstr(m, "\"t\":\"op\"")) {
        if (strstr(m, "\"kind\":\"param.set\"") && jget_u32(m, "id", &id) && jget_u32(m, "v", &v)) {
            route_inbound(id, v, 0);
        }
    } else if ((strstr(m, "\"t\":\"lock\"") || strstr(m, "\"t\":\"lock_released\"")) && on_unit_lock(m)) {
        // pattern / playlist lock (mutexing)
    } else if (strstr(m, "\"t\":\"lock\"") && jget_paramkey(m, &id)) {
        Slot *s = slot_by_id(id);
        int granted = strstr(m, "\"granted\":true") != NULL;
        char ownerPat[96]; sprintf_s(ownerPat, sizeof(ownerPat), "\"owner\":\"%s\"", g_myId);
        int mine = strstr(m, ownerPat) != NULL;
        if (s) {
            if (granted && !mine) s->lockedByPeer = 1;
            if (!granted && !mine) { s->lockedByMe = 0; s->lockedByPeer = 1; logmsg("lock denied id=0x%08x", id); }
        }
    } else if (strstr(m, "\"t\":\"lock_released\"") && jget_paramkey(m, &id)) {
        Slot *s = slot_by_id(id);
        if (s) s->lockedByPeer = 0;
    } else if (strstr(m, "\"t\":\"presence\"")) {
        strncpy_s(g_peersJson, sizeof(g_peersJson), m, _TRUNCATE);
        logmsg("server: %.300s", m);
        net_show_connected(m);
    } else if (strstr(m, "\"t\":\"error\"")) {
        logmsg("server: %.300s", m);
        if (strstr(m, "\"code\":\"bad_password\"")) strcpy_s(g_netErr, sizeof(g_netErr), "Wrong password");
    } else if (strstr(m, "\"t\":\"leader_left\"")) {
        logmsg("server: %.300s", m);
        strcpy_s(g_netErr, sizeof(g_netErr), "The session leader left: session ended");
    }
}

// ---------------- samples ----------------
// Sample files travel with the data that uses them. FLP event 196 (a channel's sample path,
// UTF-16) appears in .flp files and in saved channel states alike, so the same code covers
// project loads (project.load) and live channel changes (plug.state / chan.add).
// Sender (outbox thread): the op lists the samples its data references,
//   "samples":[{"p":<base64 UTF-16 path as written in the data>,"h":<sha256 hex>,"s":<bytes>}],
// after uploading the files the server doesn't have yet to its blob store (HTTP next to the
// WebSocket: /blobs/missing, /blob/<sha256>; see server/src/blobs.ts). More than
// SAMPLE_ASK_BYTES to upload at once -> Yes/No dialog first; "No" leaves those samples out
// (and they are not asked about again this session). Every op queued behind waits, so the
// order of ops is kept.
// Receiver (inbox thread): before an op that lists samples is handled, each sample is found
// locally (same path with the same content, or any file known to have that content) or
// downloaded into Documents\flim\samples\<hash>\; the paths in the op's data are then rewritten
// to the local files. Later ops wait behind it, again to keep the order.
// Samples inside plugin states (Slicex, FPC, DirectWave...) are not covered.
#define EV_SAMPLE_PATH   196
#define SAMPLE_ASK_BYTES (50ull * 1024 * 1024)
#define SAMPLE_MAX_BYTES (2000ull * 1024 * 1024)
#define SAMPLE_PATH_MAX  1024
#define BLOB_CHUNK       (256 * 1024)

typedef struct { wchar_t *path; uint64_t size; FILETIME mt; char hex[65]; } HashEnt;
typedef struct { wchar_t *from, *to; } PathMap;
typedef struct OutItem { char *msg; unsigned char *blob; size_t blobn; struct OutItem *next; } OutItem;
typedef struct InItem { char *msg; struct InItem *next; } InItem;

static HashEnt *g_hashes;  static int g_nhash, g_caphash;    // content hash per local file (path+size+mtime)
static PathMap *g_pmap;    static int g_npmap, g_cappmap;    // receiver: path in peer data -> local file
static char (*g_declined)[65]; static int g_ndeclined, g_capdeclined;   // "No" in the upload dialog
static CRITICAL_SECTION g_sampleLock, g_outLock, g_inLock;                        // all of the above + the blob server address
static NetUrl  g_blobUrl;
static char    g_blobKey[65];                                // sha256 hex of the server password
static volatile LONG g_blobOn = 0;
static int     g_noLocal = 0;                                // FLIM_SAMPLE_NOLOCAL=1 (tests): ignore local copies
static OutItem *g_outHead, *g_outTail;
static int     g_outBusy;
static InItem  *g_inHead, *g_inTail;
static int     g_inBusy;
static HANDLE  g_outEvt, g_inEvt;
static volatile uint64_t g_xferDone, g_xferTotal;
static const char *g_xferWhat = "";
static DWORD   g_xferShown;

typedef struct { BCRYPT_ALG_HANDLE alg; BCRYPT_HASH_HANDLE h; } Sha;
static int sha_begin(Sha *s) {
    memset(s, 0, sizeof(*s));
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&s->alg, BCRYPT_SHA256_ALGORITHM, NULL, 0))) return 0;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(s->alg, &s->h, NULL, 0, NULL, 0, 0))) { BCryptCloseAlgorithmProvider(s->alg, 0); return 0; }
    return 1;
}
static void sha_add(Sha *s, const void *p, size_t n) { BCryptHashData(s->h, (PUCHAR)p, (ULONG)n, 0); }
static void sha_end(Sha *s, char hex[65]) {
    UCHAR d[32];
    int i;
    BCryptFinishHash(s->h, d, 32, 0);
    BCryptDestroyHash(s->h);
    BCryptCloseAlgorithmProvider(s->alg, 0);
    for (i = 0; i < 32; i++) sprintf_s(hex + i * 2, 3, "%02x", d[i]);
}
static void sha_hex(const void *p, size_t n, char hex[65]) {
    Sha s;
    hex[0] = 0;
    if (!sha_begin(&s)) return;
    sha_add(&s, p, n);
    sha_end(&s, hex);
}
static int is_hex64(const char *h) {
    int i;
    for (i = 0; i < 64; i++) if (!((h[i] >= '0' && h[i] <= '9') || (h[i] >= 'a' && h[i] <= 'f'))) return 0;
    return 1;
}

// Transfer progress in the toolbar status.
static void xfer_show(int force) {
    DWORD now = GetTickCount();
    if (!force && now - g_xferShown < 1000) return;
    g_xferShown = now;
    net_status(NET_ON, "%s samples: %.1f / %.1f MB", g_xferWhat, g_xferDone / 1048576.0, g_xferTotal / 1048576.0);
}
static void xfer_end(void) { if (g_peersJson[0]) net_show_connected(g_peersJson); }

static int file_info(const wchar_t *p, uint64_t *size, FILETIME *mt) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    *size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    if (mt) *mt = a.ftLastWriteTime;
    return 1;
}
static int is_abs_path(const wchar_t *p) {
    return (iswalpha(p[0]) && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/')) || (p[0] == L'\\' && p[1] == L'\\');
}

static int hash_file(const wchar_t *p, char hex[65]) {
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    unsigned char *buf;
    DWORD got;
    Sha s;
    int ok = 1;
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (!sha_begin(&s)) { CloseHandle(f); return 0; }
    buf = (unsigned char *)malloc(BLOB_CHUNK * 4);
    for (;;) {
        if (!ReadFile(f, buf, BLOB_CHUNK * 4, &got, NULL)) { ok = 0; break; }
        if (!got) break;
        sha_add(&s, buf, got);
    }
    sha_end(&s, hex);
    free(buf);
    CloseHandle(f);
    return ok;
}

static void hash_put(const wchar_t *p, uint64_t size, FILETIME mt, const char *hex) {
    int i;
    EnterCriticalSection(&g_sampleLock);
    for (i = 0; i < g_nhash && _wcsicmp(g_hashes[i].path, p) != 0; i++) {}
    if (i == g_nhash) {
        if (g_nhash == g_caphash) { g_caphash = g_caphash ? g_caphash * 2 : 256; g_hashes = (HashEnt *)realloc(g_hashes, sizeof(HashEnt) * g_caphash); }
        g_hashes[g_nhash].path = _wcsdup(p);
        g_nhash++;
    }
    g_hashes[i].size = size; g_hashes[i].mt = mt;
    strcpy_s(g_hashes[i].hex, sizeof(g_hashes[i].hex), hex);
    LeaveCriticalSection(&g_sampleLock);
}

// Content hash of a local file (cached while path, size and mtime stay the same).
static int sample_hash(const wchar_t *p, char hex[65], uint64_t *size) {
    FILETIME mt;
    uint64_t sz;
    int i, hit = 0;
    if (!file_info(p, &sz, &mt) || sz > SAMPLE_MAX_BYTES) return 0;
    EnterCriticalSection(&g_sampleLock);
    for (i = 0; i < g_nhash; i++)
        if (_wcsicmp(g_hashes[i].path, p) == 0 && g_hashes[i].size == sz && CompareFileTime(&g_hashes[i].mt, &mt) == 0) {
            strcpy_s(hex, 65, g_hashes[i].hex); hit = 1; break;
        }
    LeaveCriticalSection(&g_sampleLock);
    if (!hit) {
        if (!hash_file(p, hex)) return 0;
        hash_put(p, sz, mt, hex);
    }
    *size = sz;
    return 1;
}

// Any local file known to have this content (and unchanged since it was hashed).
static int find_by_hash(const char *hex, wchar_t *out, int cap) {
    int i, found = 0;
    EnterCriticalSection(&g_sampleLock);
    for (i = 0; i < g_nhash && !found; i++) {
        uint64_t sz;
        FILETIME mt;
        if (strcmp(g_hashes[i].hex, hex) != 0) continue;
        if (!file_info(g_hashes[i].path, &sz, &mt) || sz != g_hashes[i].size || CompareFileTime(&g_hashes[i].mt, &mt) != 0) continue;
        wcsncpy_s(out, cap, g_hashes[i].path, _TRUNCATE);
        found = 1;
    }
    LeaveCriticalSection(&g_sampleLock);
    return found;
}

static void map_set(const wchar_t *from, const wchar_t *to) {
    int i;
    EnterCriticalSection(&g_sampleLock);
    for (i = 0; i < g_npmap && wcscmp(g_pmap[i].from, from) != 0; i++) {}
    if (!to) {
        if (i < g_npmap) { free(g_pmap[i].from); free(g_pmap[i].to); g_pmap[i] = g_pmap[--g_npmap]; }
    } else {
        if (i == g_npmap) {
            if (g_npmap == g_cappmap) { g_cappmap = g_cappmap ? g_cappmap * 2 : 64; g_pmap = (PathMap *)realloc(g_pmap, sizeof(PathMap) * g_cappmap); }
            g_pmap[g_npmap].from = _wcsdup(from);
            g_pmap[g_npmap].to = NULL;
            g_npmap++;
        }
        free(g_pmap[i].to);
        g_pmap[i].to = _wcsdup(to);
    }
    LeaveCriticalSection(&g_sampleLock);
}

static int declined(const char *hex) {
    int i, r = 0;
    EnterCriticalSection(&g_sampleLock);
    for (i = 0; i < g_ndeclined && !r; i++) r = strcmp(g_declined[i], hex) == 0;
    LeaveCriticalSection(&g_sampleLock);
    return r;
}
static void decline(const char *hex) {
    EnterCriticalSection(&g_sampleLock);
    if (g_ndeclined == g_capdeclined) { g_capdeclined = g_capdeclined ? g_capdeclined * 2 : 64; g_declined = (char (*)[65])realloc(g_declined, 65 * (size_t)g_capdeclined); }
    strcpy_s(g_declined[g_ndeclined++], 65, hex);
    LeaveCriticalSection(&g_sampleLock);
}

// %FLStudioUserData% = <"Shared data" from FL's registry paths>\FL Studio
static void fl_user_data(wchar_t *out, int cap) {
    DWORD n = (DWORD)cap * 2;
    size_t l;
    out[0] = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Image-Line\\Shared\\Paths", L"Shared data", RRF_RT_REG_SZ, NULL, out, &n) != ERROR_SUCCESS || !out[0]) {
        DWORD k = GetEnvironmentVariableW(L"USERPROFILE", out, cap);
        if (!k || k >= (DWORD)cap) out[0] = 0;
        wcscat_s(out, cap, L"\\Documents\\Image-Line\\");
    }
    l = wcslen(out);
    if (l && out[l - 1] != L'\\') wcscat_s(out, cap, L"\\");
    wcscat_s(out, cap, L"FL Studio");
}
// A sample path as FL writes it -> a local file path. 0 = not a transferable file
// (factory data and other FL placeholders: every install has those).
static int expand_path(const wchar_t *p, wchar_t *out, int cap) {
    static const wchar_t UD[] = L"%FLStudioUserData%";
    size_t ul = wcslen(UD);
    if (p[0] != L'%') { if (wcslen(p) >= (size_t)cap) return 0; wcscpy_s(out, cap, p); return is_abs_path(out); }
    if (_wcsnicmp(p, UD, ul) != 0) return 0;
    fl_user_data(out, cap);
    if (wcslen(out) + wcslen(p + ul) >= (size_t)cap) return 0;
    wcscat_s(out, cap, p + ul);
    return 1;
}

// ---- FLP / .fst images ----
typedef int (*flpev_t)(void *ctx, int id, const unsigned char *p, size_t n, size_t at, size_t end);
// Walk the events of an FLP image ("FLhd" header chunk, then one "FLdt" chunk). cb gets each
// event: id, payload, and the event's byte range [at, end). Returns 0 if d is no FLP image.
static int flp_walk(const unsigned char *d, size_t n, flpev_t cb, void *ctx) {
    size_t o, hl;
    if (!d || n < 22 || memcmp(d, "FLhd", 4) != 0) return 0;
    hl = *(const uint32_t *)(d + 4);
    o = 8 + hl;
    if (o + 8 > n || memcmp(d + o, "FLdt", 4) != 0) return 0;
    o += 8;
    while (o < n) {
        size_t at = o, len = 0;
        int id = d[o++];
        if (id < 64) len = 1;
        else if (id < 128) len = 2;
        else if (id < 192) len = 4;
        else {
            int s = 0;
            for (;;) {
                unsigned char b;
                if (o >= n || s > 28) return 0;
                b = d[o++];
                len |= (size_t)(b & 0x7f) << s;
                s += 7;
                if (!(b & 0x80)) break;
            }
        }
        if (o + len > n) return 0;
        if (cb && !cb(ctx, id, d + o, len, at, o + len)) break;
        o += len;
    }
    return 1;
}

static void ev_wstr(const unsigned char *p, size_t n, wchar_t *out, int cap) {
    size_t k = n / 2;
    if (k >= (size_t)cap) k = (size_t)cap - 1;
    memcpy(out, p, k * 2);
    out[k] = 0;
}

typedef struct { wchar_t **paths; int n, cap; } PathList;
static int collect_cb(void *ctx, int id, const unsigned char *p, size_t n, size_t at, size_t end) {
    PathList *L = (PathList *)ctx;
    wchar_t w[SAMPLE_PATH_MAX];
    int i;
    (void)at; (void)end;
    if (id != EV_SAMPLE_PATH || n < 4) return 1;
    ev_wstr(p, n, w, SAMPLE_PATH_MAX);
    if (!w[0]) return 1;
    for (i = 0; i < L->n; i++) if (wcscmp(L->paths[i], w) == 0) return 1;
    if (L->n == L->cap) { L->cap = L->cap ? L->cap * 2 : 16; L->paths = (wchar_t **)realloc(L->paths, sizeof(wchar_t *) * L->cap); }
    L->paths[L->n++] = _wcsdup(w);
    return 1;
}
static void pathlist_free(PathList *L) {
    int i;
    for (i = 0; i < L->n; i++) free(L->paths[i]);
    free(L->paths);
    memset(L, 0, sizeof(*L));
}

typedef struct { unsigned char *o; size_t n, cap; int changed; const unsigned char *d; } RewriteCtx;
static void rw_put(RewriteCtx *c, const void *p, size_t n) {
    if (c->n + n > c->cap) { c->cap = (c->n + n) * 2 + 1024; c->o = (unsigned char *)realloc(c->o, c->cap); }
    memcpy(c->o + c->n, p, n);
    c->n += n;
}
// Caller holds g_sampleLock.
static int rewrite_cb(void *ctx, int id, const unsigned char *p, size_t n, size_t at, size_t end) {
    RewriteCtx *c = (RewriteCtx *)ctx;
    wchar_t w[SAMPLE_PATH_MAX];
    int i;
    if (id == EV_SAMPLE_PATH && n >= 4) {
        ev_wstr(p, n, w, SAMPLE_PATH_MAX);
        for (i = 0; i < g_npmap; i++) {
            if (wcscmp(g_pmap[i].from, w) != 0) continue;
            {
                size_t len = (wcslen(g_pmap[i].to) + 1) * 2, v = len;
                unsigned char b = (unsigned char)id;
                rw_put(c, &b, 1);
                do { b = (unsigned char)(v & 0x7f); v >>= 7; if (v) b |= 0x80; rw_put(c, &b, 1); } while (v);
                rw_put(c, g_pmap[i].to, len);        // UTF-16LE + terminating NUL
                c->changed = 1;
            }
            return 1;
        }
    }
    rw_put(c, c->d + at, end - at);
    return 1;
}

// Receiver: point the sample paths in an inbound .flp / .fst image at the local files.
static void samples_rewrite(unsigned char **d, size_t *n) {
    RewriteCtx c;
    size_t start;
    int ok;
    if (!g_npmap || !*d || *n < 22 || memcmp(*d, "FLhd", 4) != 0) return;
    start = 8 + *(const uint32_t *)(*d + 4) + 8;
    if (start > *n) return;
    memset(&c, 0, sizeof(c));
    c.d = *d;
    rw_put(&c, *d, start);
    EnterCriticalSection(&g_sampleLock);
    ok = flp_walk(*d, *n, rewrite_cb, &c);
    LeaveCriticalSection(&g_sampleLock);
    if (!ok || !c.changed) { free(c.o); return; }
    *(uint32_t *)(c.o + start - 4) = (uint32_t)(c.n - start);        // FLdt chunk length
    logmsg("SAMPLES paths rewritten to local files (%zu -> %zu bytes)", *n, c.n);
    free(*d);
    *d = c.o;
    *n = c.n;
}

// ---- blob store (HTTP) ----
// One request to the server's blob store. Returns the HTTP status (0 = failed).
//   up/upSize: PUT body streamed from this file; body/bodyN: small in-memory body;
//   down: response body written to this file (downHex = its sha256); resp: small text response.
static DWORD blob_http(const wchar_t *verb, const char *what, const wchar_t *up, uint64_t upSize,
                       const char *body, DWORD bodyN, const wchar_t *down, char *downHex, char *resp, int respCap) {
    NetUrl u;
    char key[65];
    wchar_t path[700], wwhat[200], hdr[128];
    HINTERNET ses = NULL, con = NULL, req = NULL;
    DWORD status = 0, sl = sizeof(status), err = 0;
    unsigned char *buf = NULL;
    HANDLE f = INVALID_HANDLE_VALUE;
    size_t pl;
    EnterCriticalSection(&g_sampleLock);
    if (!g_blobOn) { LeaveCriticalSection(&g_sampleLock); return 0; }
    u = g_blobUrl;
    strcpy_s(key, sizeof(key), g_blobKey);
    LeaveCriticalSection(&g_sampleLock);
    wcscpy_s(path, 700, u.path);
    pl = wcslen(path);
    while (pl && path[pl - 1] == L'/') path[--pl] = 0;
    MultiByteToWideChar(CP_UTF8, 0, what, -1, wwhat, 200);
    wcscat_s(path, 700, wwhat);
    swprintf_s(hdr, 128, L"x-flim-key: %S\r\n", key);
    ses = WinHttpOpen(L"flim/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) ses = WinHttpOpen(L"flim/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) goto out;
    WinHttpSetTimeouts(ses, 0, 15000, 120000, 120000);
    con = WinHttpConnect(ses, u.host, u.port, 0);
    if (!con) goto out;
    req = WinHttpOpenRequest(con, verb, path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) goto out;
    buf = (unsigned char *)malloc(BLOB_CHUNK);
    if (up) {
        uint64_t left = upSize;
        f = CreateFileW(up, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (f == INVALID_HANDLE_VALUE) goto out;
        if (!WinHttpSendRequest(req, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, (DWORD)upSize, 0)) goto out;
        while (left) {
            DWORD want = left > BLOB_CHUNK ? BLOB_CHUNK : (DWORD)left, got = 0, wr = 0;
            if (!ReadFile(f, buf, want, &got, NULL) || !got) goto out;
            if (!WinHttpWriteData(req, buf, got, &wr)) goto out;
            left -= got;
            g_xferDone += got;
            xfer_show(0);
        }
        CloseHandle(f);
        f = INVALID_HANDLE_VALUE;
    } else if (!WinHttpSendRequest(req, hdr, (DWORD)-1L, (LPVOID)body, bodyN, bodyN, 0)) goto out;
    if (!WinHttpReceiveResponse(req, NULL)) goto out;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &sl, WINHTTP_NO_HEADER_INDEX);
    if (status == 200 && down) {
        Sha s;
        f = CreateFileW(down, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f == INVALID_HANDLE_VALUE || !sha_begin(&s)) { err = GetLastError(); status = 0; goto out; }
        for (;;) {
            DWORD got = 0, wr = 0;
            if (!WinHttpReadData(req, buf, BLOB_CHUNK, &got)) { err = GetLastError(); status = 0; break; }
            if (!got) break;
            sha_add(&s, buf, got);
            if (!WriteFile(f, buf, got, &wr, NULL) || wr != got) { err = GetLastError(); status = 0; break; }
            g_xferDone += got;
            xfer_show(0);
        }
        sha_end(&s, downHex);
    } else if (resp && respCap > 0) {
        int o = 0;
        for (;;) {
            DWORD got = 0;
            if (o >= respCap - 1 || !WinHttpReadData(req, resp + o, (DWORD)(respCap - 1 - o), &got) || !got) break;
            o += (int)got;
        }
        resp[o] = 0;
    }
out:
    if (!status) logmsg("SAMPLES %S %s failed (%lu)", verb, what, err ? err : GetLastError());
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    free(buf);
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return status;
}

// ---- sender ----
typedef struct { const wchar_t *p; wchar_t *full; char hex[65]; uint64_t size; int upload, skip; } SEnt;

static void group_digits(uint64_t v, wchar_t *out, int cap) {
    wchar_t t[32];
    int n, i, o = 0;
    swprintf_s(t, 32, L"%llu", (unsigned long long)v);
    n = (int)wcslen(t);
    for (i = 0; i < n && o < cap - 2; i++) {
        if (i && (n - i) % 3 == 0) out[o++] = L',';
        out[o++] = t[i];
    }
    out[o] = 0;
}

// Outbox thread: list (and upload) the samples an outgoing project / channel state uses.
// Returns the message with "samples":[...] spliced in, or NULL to send it unchanged.
static char *samples_attach(const char *msg, const unsigned char *blob, size_t n) {
    PathList L;
    SEnt *E = NULL;
    int i, j, ne = 0, nup = 0;
    uint64_t upBytes = 0;
    char *out = NULL;
    memset(&L, 0, sizeof(L));
    flp_walk(blob, n, collect_cb, &L);
    if (!L.n || !g_blobOn) goto done;
    E = (SEnt *)calloc((size_t)L.n, sizeof(SEnt));
    for (i = 0; i < L.n; i++) {
        wchar_t full[SAMPLE_PATH_MAX];
        if (!expand_path(L.paths[i], full, SAMPLE_PATH_MAX)) continue;
        if (!sample_hash(full, E[ne].hex, &E[ne].size)) { logmsg("SAMPLES %S: no such file here (or too big), not sent", full); continue; }
        E[ne].p = L.paths[i];
        E[ne].full = _wcsdup(full);
        ne++;
    }
    if (!ne) goto done;
    {   // which ones does the server lack?
        char *body = (char *)malloc((size_t)ne * 65 + 1), *resp = (char *)malloc((size_t)ne * 65 + 16), *line, *ctx = NULL;
        int o = 0;
        DWORD st;
        for (i = 0; i < ne; i++) o += sprintf_s(body + o, (size_t)ne * 65 + 1 - o, "%s\n", E[i].hex);
        st = blob_http(L"POST", "/blobs/missing", NULL, 0, body, (DWORD)o, NULL, NULL, resp, ne * 65 + 16);
        if (st != 200) { logmsg("SAMPLES the server's sample store doesn't answer (HTTP %lu): sent without samples", st); free(body); free(resp); goto done; }
        for (line = strtok_s(resp, "\r\n", &ctx); line; line = strtok_s(NULL, "\r\n", &ctx))
            for (i = 0; i < ne; i++) if (strcmp(E[i].hex, line) == 0) E[i].upload = 1;
        free(body); free(resp);
    }
    for (i = 0; i < ne; i++) {
        if (!E[i].upload) continue;
        if (declined(E[i].hex)) { E[i].upload = 0; E[i].skip = 1; continue; }
        for (j = 0; j < i && !(E[j].upload && strcmp(E[j].hex, E[i].hex) == 0); j++) {}
        if (j < i) { E[i].upload = 0; continue; }        // same content listed twice: one upload
        upBytes += E[i].size;
        nup++;
    }
    if (upBytes > SAMPLE_ASK_BYTES) {
        wchar_t q[900], grouped[40];
        group_digits(upBytes, grouped, 40);
        swprintf_s(q, 900,
            L"The other people in this session don't have %d of the samples used here.\n\n"
            L"Sending them uploads %.1f MB (%s bytes).\n\n"
            L"Send them now?\n\n"
            L"Yes: upload them (flim sends your other changes after the upload).\n"
            L"No: don't send these samples. They will be missing for the others.",
            nup, upBytes / 1048576.0, grouped);
        logmsg("SAMPLES asking before uploading %llu bytes (%d files)", (unsigned long long)upBytes, nup);
        // Owned by FL's main window: an ownerless box from this background thread can't come to
        // the front and may open behind other windows. Flash FL's taskbar button in case FL isn't in front.
        if (g_mainForm) {
            FLASHWINFO fw = { sizeof(fw), g_mainForm, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0 };
            FlashWindowEx(&fw);
        }
        if (MessageBoxW(g_mainForm, q, L"flim: send samples?", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) != IDYES) {
            for (i = 0; i < ne; i++) {
                if (!E[i].upload) continue;
                decline(E[i].hex);
                for (j = 0; j < ne; j++) if (strcmp(E[j].hex, E[i].hex) == 0) E[j].skip = 1;
                E[i].upload = 0;
            }
            logmsg("SAMPLES upload declined: %d files left out", nup);
            upBytes = 0; nup = 0;
        }
    }
    if (nup) {
        char what[80];
        g_xferWhat = "Uploading"; g_xferDone = 0; g_xferTotal = upBytes;
        xfer_show(1);
        for (i = 0; i < ne; i++) {
            DWORD st;
            if (!E[i].upload) continue;
            sprintf_s(what, sizeof(what), "/blob/%s", E[i].hex);
            st = blob_http(L"PUT", what, E[i].full, E[i].size, NULL, 0, NULL, NULL, NULL, 0);
            if (st == 200 || st == 201) logmsg("SAMPLES uploaded %S (%llu bytes)", E[i].full, (unsigned long long)E[i].size);
            else {
                logmsg("SAMPLES upload of %S failed (HTTP %lu): left out", E[i].full, st);
                for (j = 0; j < ne; j++) if (strcmp(E[j].hex, E[i].hex) == 0) E[j].skip = 1;
            }
        }
        xfer_end();
    }
    {   // splice "samples":[...] in front of the op's fields
        const char *op = strstr(msg, "\"op\":{");
        size_t cap = strlen(msg) + 64, len;
        int k = 0;
        if (!op) goto done;
        for (i = 0; i < ne; i++) cap += wcslen(E[i].p) * 3 + 120;
        out = (char *)malloc(cap);
        len = (size_t)(op + 6 - msg);
        memcpy(out, msg, len);
        len += sprintf_s(out + len, cap - len, "\"samples\":[");
        for (i = 0; i < ne; i++) {
            if (E[i].skip) continue;
            len += sprintf_s(out + len, cap - len, "%s{\"p\":\"", k++ ? "," : "");
            b64enc((const unsigned char *)E[i].p, (int)(wcslen(E[i].p) * 2), out + len);
            len += strlen(out + len);
            len += sprintf_s(out + len, cap - len, "\",\"h\":\"%s\",\"s\":%llu}", E[i].hex, (unsigned long long)E[i].size);
        }
        len += sprintf_s(out + len, cap - len, "],");
        strcpy_s(out + len, cap - len, op + 6);
        logmsg("SAMPLES op lists %d samples (%d uploaded)", k, nup);
    }
done:
    if (E) { for (i = 0; i < ne; i++) free(E[i].full); free(E); }
    pathlist_free(&L);
    return out;
}

static void out_push(char *msg, unsigned char *blob, size_t n) {   // caller holds g_outLock
    OutItem *it = (OutItem *)calloc(1, sizeof(OutItem));
    it->msg = msg; it->blob = blob; it->blobn = n;
    if (g_outTail) g_outTail->next = it; else g_outHead = it;
    g_outTail = it;
}

// Every op goes through here: straight out, unless something is queued in the outbox
// (an op waiting for its samples) -- then behind it, to keep the order.
static void ws_send(const char *msg) {
    EnterCriticalSection(&g_outLock);
    if (g_outBusy || g_outHead) out_push(_strdup(msg), NULL, 0);
    else ws_send_now(msg);
    LeaveCriticalSection(&g_outLock);
}

// An op whose data (an .flp / .fst image) may reference samples. Takes ownership of msg.
static void ws_send_samples(char *msg, const unsigned char *blob, size_t n) {
    unsigned char *copy;
    if (!g_ws) { free(msg); return; }
    copy = (unsigned char *)malloc(n ? n : 1);
    if (n) memcpy(copy, blob, n);
    EnterCriticalSection(&g_outLock);
    out_push(msg, copy, n);
    LeaveCriticalSection(&g_outLock);
    SetEvent(g_outEvt);
}

static DWORD WINAPI outThread(LPVOID p) {
    (void)p;
    for (;;) {
        WaitForSingleObject(g_outEvt, INFINITE);
        for (;;) {
            OutItem *it;
            EnterCriticalSection(&g_outLock);
            it = g_outHead;
            if (!it) { g_outBusy = 0; LeaveCriticalSection(&g_outLock); break; }
            g_outHead = it->next;
            if (!g_outHead) g_outTail = NULL;
            g_outBusy = 1;
            LeaveCriticalSection(&g_outLock);
            if (it->blob) {
                char *m2 = samples_attach(it->msg, it->blob, it->blobn);
                if (m2) { free(it->msg); it->msg = m2; }
            }
            EnterCriticalSection(&g_outLock);        // direct senders wait: nothing overtakes this one
            ws_send_now(it->msg);
            LeaveCriticalSection(&g_outLock);
            free(it->msg); free(it->blob); free(it);
        }
    }
}

// ---- receiver ----
static void samples_dir(wchar_t *out, int cap) {
    DWORD k = GetEnvironmentVariableW(L"USERPROFILE", out, cap);
    if (!k || k >= (DWORD)cap) wcscpy_s(out, cap, L"C:");
    wcscat_s(out, cap, L"\\Documents\\flim\\samples");
}

// Where a downloaded sample is kept: samples\<first 16 hex>\<its file name>.
static void cache_path(const char *hex, const wchar_t *p, wchar_t *out, int cap) {
    wchar_t dir[MAX_PATH], name[MAX_PATH];
    const wchar_t *b = p + wcslen(p);
    int i;
    while (b > p && b[-1] != L'\\' && b[-1] != L'/') b--;
    wcsncpy_s(name, MAX_PATH, *b ? b : L"sample.wav", _TRUNCATE);
    for (i = 0; name[i]; i++) if (wcschr(L"\\/:*?\"<>|", name[i]) || name[i] < 32) name[i] = L'_';
    samples_dir(dir, MAX_PATH);
    swprintf_s(out, cap, L"%s\\%.16S\\%s", dir, hex, name);
}

typedef struct { wchar_t *p; char hex[65]; uint64_t size; wchar_t cache[MAX_PATH * 2]; int need; } REnt;

// Inbox thread: make the samples an inbound op lists available here and record the path
// rewrites for its data.
static void samples_resolve(const char *m) {
    const char *q = strstr(m, "\"samples\":[");
    REnt *R = NULL;
    int nr = 0, cap = 0, i, nneed = 0, nfail = 0;
    uint64_t total = 0;
    if (!q) return;
    q += 11;
    while (*q == '{') {
        const char *e = strchr(q, '}'), *pp = strstr(q, "\"p\":\""), *hh = strstr(q, "\"h\":\""), *ss = strstr(q, "\"s\":");
        if (!e) break;
        if (pp && pp < e && hh && hh < e && ss && ss < e) {
            const char *pe = strchr(pp + 5, '"');
            size_t bn;
            unsigned char *b;
            if (pe && pe < e && is_hex64(hh + 5)) {
                if (nr == cap) { cap = cap ? cap * 2 : 16; R = (REnt *)realloc(R, sizeof(REnt) * cap); }
                memset(&R[nr], 0, sizeof(REnt));
                b = b64dec(pp + 5, (size_t)(pe - pp - 5), &bn);
                R[nr].p = (wchar_t *)calloc(bn / 2 + 1, 2);
                memcpy(R[nr].p, b, bn / 2 * 2);
                free(b);
                memcpy(R[nr].hex, hh + 5, 64);
                R[nr].size = _strtoui64(ss + 4, NULL, 10);
                if (R[nr].p[0]) nr++; else free(R[nr].p);
            }
        }
        q = e + 1;
        if (*q == ',') q++;
    }
    for (i = 0; i < nr; i++) {
        wchar_t full[SAMPLE_PATH_MAX], found[SAMPLE_PATH_MAX];
        char hx[65];
        uint64_t sz;
        if (!g_noLocal && expand_path(R[i].p, full, SAMPLE_PATH_MAX) && sample_hash(full, hx, &sz) && strcmp(hx, R[i].hex) == 0) {
            map_set(R[i].p, NULL);                      // the same file is right where the peer has it
            continue;
        }
        if (!g_noLocal && find_by_hash(R[i].hex, found, SAMPLE_PATH_MAX)) { map_set(R[i].p, found); continue; }
        cache_path(R[i].hex, R[i].p, R[i].cache, MAX_PATH * 2);
        if (sample_hash(R[i].cache, hx, &sz) && strcmp(hx, R[i].hex) == 0) { map_set(R[i].p, R[i].cache); continue; }
        R[i].need = 1;
        total += R[i].size;
        nneed++;
    }
    if (nneed) {
        g_xferWhat = "Downloading"; g_xferDone = 0; g_xferTotal = total;
        xfer_show(1);
        for (i = 0; i < nr; i++) {
            wchar_t part[MAX_PATH * 2 + 8], dir[MAX_PATH * 2];
            char what[80], got[65] = "", hx[65];
            uint64_t sz;
            DWORD st;
            wchar_t *slash;
            if (!R[i].need) continue;
            if (sample_hash(R[i].cache, hx, &sz) && strcmp(hx, R[i].hex) == 0) { map_set(R[i].p, R[i].cache); continue; }   // same content twice
            wcscpy_s(dir, MAX_PATH * 2, R[i].cache);
            slash = wcsrchr(dir, L'\\');
            if (slash) { *slash = 0; SHCreateDirectoryExW(NULL, dir, NULL); }
            swprintf_s(part, MAX_PATH * 2 + 8, L"%s.part", R[i].cache);
            sprintf_s(what, sizeof(what), "/blob/%s", R[i].hex);
            st = blob_http(L"GET", what, NULL, 0, NULL, 0, part, got, NULL, 0);
            if (st == 200 && strcmp(got, R[i].hex) == 0 && MoveFileExW(part, R[i].cache, MOVEFILE_REPLACE_EXISTING)) {
                FILETIME mt;
                if (file_info(R[i].cache, &sz, &mt)) hash_put(R[i].cache, sz, mt, R[i].hex);
                map_set(R[i].p, R[i].cache);
                logmsg("SAMPLES downloaded %S (%llu bytes)", R[i].cache, (unsigned long long)R[i].size);
            } else {
                DeleteFileW(part);
                nfail++;
                logmsg("SAMPLES could not get %S (HTTP %lu%s): it will be missing", R[i].p, st, st == 200 ? ", content mismatch" : "");
            }
        }
        xfer_end();
    }
    logmsg("SAMPLES op lists %d samples: %d downloaded, %d missing", nr, nneed - nfail, nfail);
    for (i = 0; i < nr; i++) free(R[i].p);
    free(R);
}

// Every inbound message comes through here (receive thread).
static void inbox_msg(const char *m) {
    int queue;
    EnterCriticalSection(&g_inLock);
    queue = g_inBusy || g_inHead || strstr(m, "\"samples\":[") != NULL;
    if (queue) {
        InItem *it = (InItem *)calloc(1, sizeof(InItem));
        it->msg = _strdup(m);
        if (g_inTail) g_inTail->next = it; else g_inHead = it;
        g_inTail = it;
    }
    LeaveCriticalSection(&g_inLock);
    if (queue) SetEvent(g_inEvt);
    else handle_msg(m);
}

static DWORD WINAPI inThread(LPVOID p) {
    (void)p;
    for (;;) {
        WaitForSingleObject(g_inEvt, INFINITE);
        for (;;) {
            InItem *it;
            EnterCriticalSection(&g_inLock);
            it = g_inHead;
            if (!it) { g_inBusy = 0; LeaveCriticalSection(&g_inLock); break; }
            g_inHead = it->next;
            if (!g_inHead) g_inTail = NULL;
            g_inBusy = 1;
            LeaveCriticalSection(&g_inLock);
            if (strstr(it->msg, "\"samples\":[")) samples_resolve(it->msg);
            handle_msg(it->msg);
            free(it->msg); free(it);
        }
    }
}

// Connected: where the blob store is (same server, same password).
static void samples_connected(const NetUrl *u, const char *pw) {
    EnterCriticalSection(&g_sampleLock);
    g_blobUrl = *u;
    sha_hex(pw, strlen(pw), g_blobKey);
    InterlockedExchange(&g_blobOn, 1);
    LeaveCriticalSection(&g_sampleLock);
}
// Disconnected: drop what was waiting to go out or to be applied.
static void samples_disconnected(void) {
    EnterCriticalSection(&g_sampleLock);
    InterlockedExchange(&g_blobOn, 0);
    LeaveCriticalSection(&g_sampleLock);
    EnterCriticalSection(&g_outLock);
    while (g_outHead) { OutItem *n = g_outHead->next; free(g_outHead->msg); free(g_outHead->blob); free(g_outHead); g_outHead = n; }
    g_outTail = NULL;
    LeaveCriticalSection(&g_outLock);
    EnterCriticalSection(&g_inLock);
    while (g_inHead) { InItem *n = g_inHead->next; free(g_inHead->msg); free(g_inHead); g_inHead = n; }
    g_inTail = NULL;
    LeaveCriticalSection(&g_inLock);
}

// ---------------- connection (one net thread per connect) ----------------
// net_connect starts a thread that opens the socket, joins the room and runs the receive
// loop. Each connect / disconnect bumps g_netGen; a thread whose generation is stale
// (the user disconnected or reconnected meanwhile) no longer touches the status.
typedef struct { LONG gen; char url[512]; char pw[256]; } NetReq;
static volatile LONG g_netGen = 0;
static volatile LONG g_pollStarted = 0;
static char g_room[64] = "FLIM";

static void json_esc(const char *in, char *out, int cap) {
    int o = 0;
    for (; *in && o < cap - 7; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c < 0x20) o += sprintf_s(out + o, cap - o, "\\u%04x", c);
        else out[o++] = (char)c;
    }
    out[o] = 0;
}

// Forget every lock: they belong to the connection (the server drops them on disconnect).
static void net_reset(void) {
    int i;
    InterlockedExchange(&g_isLeader, 0);
    EnterCriticalSection(&g_slotLock);
    for (i = 0; i < g_nslot; i++) { g_slots[i].lockedByMe = 0; g_slots[i].lockedByPeer = 0; }
    LeaveCriticalSection(&g_slotLock);
    EnterCriticalSection(&g_ulockLock);
    for (i = 0; i < g_nulocks; i++) { g_ulocks[i].mine = g_ulocks[i].granted = g_ulocks[i].peer = 0; g_ulocks[i].owner[0] = 0; }
    g_peersJson[0] = 0;
    LeaveCriticalSection(&g_ulockLock);
}

static DWORD WINAPI netThread(LPVOID p) {
    NetReq *r = (NetReq *)p;
    NetUrl u;
    NetConn c;
    char err[200], name[2 * sizeof(g_myName)], pw[600], hello[1024];
    unsigned char *buf = NULL;
    DWORD cap = 0, len = 0;

    if (!parse_url(r->url, &u)) { net_status(NET_ERR, "Bad server address"); goto done; }
    logmsg("NET connecting to %s (%s, port %u)", r->url, u.secure ? "wss" : "ws", u.port);
    if (!ws_open(&u, &c, err, sizeof(err))) {
        logmsg("NET connect to %s failed: %s", r->url, err);
        if (r->gen == g_netGen) net_status(NET_ERR, "Can't connect: %s", err);
        goto done;
    }
    EnterCriticalSection(&g_sendLock);
    if (r->gen != g_netGen) {                       // cancelled while connecting
        LeaveCriticalSection(&g_sendLock);
        net_close_handles(&c);
        goto done;
    }
    g_ws = c.ws;
    strcpy_s(g_netShown, sizeof(g_netShown), u.shown);
    g_netErr[0] = 0;
    LeaveCriticalSection(&g_sendLock);
    samples_connected(&u, r->pw);
    net_status(NET_CONNECTING, "Joining %s...", u.shown);

    json_esc(g_myName, name, sizeof(name));
    json_esc(r->pw, pw, sizeof(pw));
    sprintf_s(hello, sizeof(hello),
        "{\"t\":\"join_room\",\"room\":\"%s\",\"password\":\"%s\",\"user\":{\"id\":\"%s\",\"name\":\"%s\"}}",
        g_room, pw, g_myId, name);
    ws_send(hello);
    SecureZeroMemory(hello, sizeof(hello)); SecureZeroMemory(pw, sizeof(pw));
    logmsg("joining room %s as %s (%s)", g_room, g_myName, g_myId);
    if (!InterlockedExchange(&g_pollStarted, 1)) CreateThread(NULL, 0, pollThread, NULL, 0, NULL);

    for (;;) {
        DWORD got = 0, e;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        if (cap - len < 65536) { cap = cap ? cap * 2 : 262144; buf = (unsigned char *)realloc(buf, cap); }
        e = WinHttpWebSocketReceive(c.ws, buf + len, cap - len - 1, &got, &type);
        if (e) { logmsg("NET receive ended (%lu)", e); break; }
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            USHORT code = 0; char reason[124] = { 0 }; DWORD rl = 0;
            WinHttpWebSocketQueryCloseStatus(c.ws, &code, reason, sizeof(reason) - 1, &rl);
            logmsg("NET server closed the connection (%u %.*s)", code, (int)rl, reason);
            break;
        }
        len += got;
        if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE)
            continue;                               // more of the same message follows
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) { buf[len] = 0; inbox_msg((char *)buf); }
        len = 0;
    }

    EnterCriticalSection(&g_sendLock);
    if (g_ws == c.ws) g_ws = NULL;
    LeaveCriticalSection(&g_sendLock);
    samples_disconnected();
    if (r->gen == g_netGen) {
        net_reset();
        if (g_netErr[0]) net_status(NET_ERR, "%s", g_netErr);
        else net_status(NET_ERR, "Connection to %s lost", u.shown);
    }
    net_close_handles(&c);
done:
    free(buf);
    SecureZeroMemory(r, sizeof(*r));
    free(r);
    return 0;
}

static void net_connect(const char *url, const char *pw) {
    NetReq *r = (NetReq *)calloc(1, sizeof(NetReq));
    HANDLE th;
    r->gen = InterlockedIncrement(&g_netGen);
    strncpy_s(r->url, sizeof(r->url), url, _TRUNCATE);
    strncpy_s(r->pw, sizeof(r->pw), pw ? pw : "", _TRUNCATE);
    {
        NetUrl u;
        if (parse_url(url, &u)) net_status(NET_CONNECTING, "Connecting to %s...", u.shown);
    }
    th = CreateThread(NULL, 0, netThread, r, 0, NULL);
    if (th) CloseHandle(th);
}

// Main thread. Sends a close frame; the net thread sees the server's reply and cleans up.
static void net_disconnect(void) {
    InterlockedIncrement(&g_netGen);
    EnterCriticalSection(&g_sendLock);
    if (g_ws) {
        WinHttpWebSocketShutdown(g_ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
        g_ws = NULL;
    }
    LeaveCriticalSection(&g_sendLock);
    net_reset();
    net_status(NET_OFF, "Not connected");
}

// ---------------- boot ----------------
static DWORD WINAPI engineMain(LPVOID p) {
    (void)p;
    srand(GetTickCount() ^ GetCurrentProcessId());
    logmsg("=== flim engine v4.4 (connect button + wss; params + plugin state + channels + native move + fx clear + notes + project files + playlist clips + unit locks + names/colors/routing + plugin locks + samples) loaded ===");

    HMODULE fl = NULL;
    int tries;
    for (tries = 0; tries < 600 && !fl; tries++) { fl = GetModuleHandleW(FL_MODULE); if (!fl) Sleep(50); }
    if (!fl) { logmsg("FLEngine module not found"); return 0; }
    g_flBase = (char *)fl;
    g_rec = (rec_t)(g_flBase + REC_RVA);
    g_setEnabled = (setenabled_t)(g_flBase + MUTE_RVA);
    g_savePreset = (presetfile_t)(g_flBase + (SAVEPRESET_VA - IMG_BASE));
    g_loadPreset = (presetfile_t)(g_flBase + (LOADPRESET_VA - IMG_BASE));
    g_hasPlugin  = (hasplugin_t)(g_flBase + (HASPLUGIN_VA - IMG_BASE));
    g_getPlugin  = (getplugin_t)(g_flBase + (GETPLUGIN_VA - IMG_BASE));
    g_intfClear  = (intfclear_t)(g_flBase + (INTFCLEAR_VA - IMG_BASE));
    g_addChannel = (addchan_t)(g_flBase + (ADDCHAN_VA - IMG_BASE));
    g_delChannel = (delchan_t)(g_flBase + (DELCHAN_VA - IMG_BASE));
    g_resolve    = (resolver_t)(g_flBase + (RESOLVER_VA - IMG_BASE));
    g_moveChans  = (movechan_t)(g_flBase + (MOVECHAN_VA - IMG_BASE));
    g_noteArr    = (notearr_t)(g_flBase + (NOTEARR_VA - IMG_BASE));
    g_noteDel    = (notedel_t)(g_flBase + (NOTEDEL_VA - IMG_BASE));
    g_gridRefresh = (gridrefresh_t)(g_flBase + (GRIDREFRESH_VA - IMG_BASE));
    logmsg("FLEngine base=%p rec=%p setEnabled=%p", (void *)fl, (void *)g_rec, (void *)g_setEnabled);

    // per-process scratch dir for plugin state files
    GetTempPathA(MAX_PATH, g_tmpDir);
    strcat_s(g_tmpDir, MAX_PATH, "flim");
    CreateDirectoryA(g_tmpDir, NULL);
    sprintf_s(g_tmpDir + strlen(g_tmpDir), MAX_PATH - strlen(g_tmpDir), "\\%lu", GetCurrentProcessId());
    CreateDirectoryA(g_tmpDir, NULL);

    {
        char *slash;
        GetModuleFileNameA(NULL, g_fxDeletePreset, MAX_PATH);
        slash = strrchr(g_fxDeletePreset, '\\');
        if (slash) slash[1] = 0;
        strcat_s(g_fxDeletePreset, MAX_PATH, "Data\\System\\(delete) effect.fst");
        if (GetFileAttributesA(g_fxDeletePreset) == INVALID_FILE_ATTRIBUTES) logmsg("WARNING: %s not found", g_fxDeletePreset);
    }

    // Wait for the main window (FL fully booted), then hook its message loop.
    if (install_mainthread_hook() != 0) return 0;

    // FL loads the startup template a moment after its window appears, which
    // resets every param. Join only once params have been quiet for SETTLE_MS,
    // otherwise that reset would be taken for edits (or undo the joined state).
    {
        const DWORD SETTLE_MS = 3000, SETTLE_MAX_MS = 60000;
        DWORD start = GetTickCount(), quietSince;
        while (!fl_ready() && GetTickCount() - start < SETTLE_MAX_MS) Sleep(100);
        logmsg("FL ready after %lu ms", GetTickCount() - start);
        Sleep(500);
        quietSince = GetTickCount();
        rescan();
        while (GetTickCount() - quietSince < SETTLE_MS && GetTickCount() - start < SETTLE_MAX_MS) {
            Sleep(250);
            LONG before = g_nslot;
            int i, changed = 0;
            rescan();
            for (i = 0; i < g_nslot; i++) {
                uint32_t v = read_value(g_slots[i].id);
                if (v != g_slots[i].last) { g_slots[i].last = v; changed++; }
            }
            if (changed || g_nslot != before) quietSince = GetTickCount();
        }
        logmsg("settled after %lu ms (%ld params)", GetTickCount() - start, g_nslot);
        EnumWindows(findMainForm, (LPARAM)&g_mainForm);
        logmsg("main form %p", (void *)g_mainForm);
        InterlockedExchange(&g_uiWanted, 1);
        PostMessageW(g_mainWnd, WM_NULL, 0, 0);
    }

    {   // who we are: flim.ini name (flim.exe's setup), FLIM_NAME overrides (dev: two FLs on one PC)
        wchar_t w[128], clean[FLIM_NAME_MAX + 1];
        char env[600], pw[256] = "";
        if (GetEnvironmentVariableW(L"FLIM_NAME", w, 128) == 0) flimcfg_get(L"name", w, 128);
        if (flim_sanitize_name(w, clean, FLIM_NAME_MAX + 1))
            WideCharToMultiByte(CP_UTF8, 0, clean, -1, g_myName, sizeof(g_myName), NULL, NULL);
        else sprintf_s(g_myName, sizeof(g_myName), "FL-%lu", GetCurrentProcessId());
        if (GetEnvironmentVariableA("FLIM_ROOM", env, sizeof(env)) > 0) {
            int i, o = 0;                               // room goes into JSON unescaped: keep it simple
            for (i = 0; env[i] && o < (int)sizeof(g_room) - 1; i++)
                if (isalnum((unsigned char)env[i]) || env[i] == '-' || env[i] == '_') g_room[o++] = env[i];
            g_room[o] = 0;
            if (!o) strcpy_s(g_room, sizeof(g_room), "FLIM");
        }
        sprintf_s(g_myId, sizeof(g_myId), "fl-%lu-%u", GetCurrentProcessId(), (unsigned)rand());
        logmsg("I am %s (%s); not connected until Connect is clicked", g_myName, g_myId);

        // dev / tests: connect right away
        if (GetEnvironmentVariableA("FLIM_AUTOCONNECT", env, sizeof(env)) == 0) {
            char srv[256];
            if (GetEnvironmentVariableA("FLIM_SERVER", srv, sizeof(srv)) > 0) sprintf_s(env, sizeof(env), "ws://%s", srv);
            else env[0] = 0;
        }
        if (env[0]) {
            GetEnvironmentVariableA("FLIM_PASSWORD", pw, sizeof(pw));
            net_connect(env, pw);
            SecureZeroMemory(pw, sizeof(pw));
        }
    }
    // This thread installed the main-thread hooks, and Windows removes a hook when the
    // thread that set it exits: it must stay alive for as long as FL runs.
    for (;;) Sleep(INFINITE);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_hinst = h;
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&g_sendLock);
        InitializeCriticalSection(&g_slotLock);
        InitializeCriticalSection(&g_qLock);
        InitializeCriticalSection(&g_heldLock);
        InitializeCriticalSection(&g_ulockLock);
        InitializeCriticalSection(&g_netTextLock);
        InitializeCriticalSection(&g_sampleLock);
        InitializeCriticalSection(&g_outLock);
        InitializeCriticalSection(&g_inLock);
        g_outEvt = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_inEvt = CreateEventW(NULL, FALSE, FALSE, NULL);
        {
            char v[8];
            g_noLocal = GetEnvironmentVariableA("FLIM_SAMPLE_NOLOCAL", v, sizeof(v)) && v[0] == '1';
        }
        CreateThread(NULL, 0, outThread, NULL, 0, NULL);
        CreateThread(NULL, 0, inThread, NULL, 0, NULL);
        CreateThread(NULL, 0, engineMain, NULL, 0, NULL);
    }
    return TRUE;
}
