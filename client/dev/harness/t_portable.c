#include <assert.h>
#include "../../engine/flim_engine.c"
static uint64_t image[4], patterns[MAX_PAT * 8], channels[3];
static void *channelList, *channelItems[MAX_CH + 1];
static unsigned nextChannel = 1000, adds, deletes;
static void *add_channel(int a, int b, int c) {
    int n = (int)channels[2]; (void)a; (void)b; (void)c;
    assert(n < MAX_CH); adds++;
    channelItems[n] = (void *)(uintptr_t)++nextChannel; channels[2]++;
    return channelItems[n];
}
static void delete_channel(unsigned index, char force, uint64_t flags) {
    int n = (int)channels[2]; (void)force; (void)flags;
    assert(n > 1 && index < (unsigned)n); deletes++;
    memmove(channelItems + index, channelItems + index + 1, (n - index - 1) * sizeof(void *)); channels[2]--;
}

static void clear_out(void) {
    while (g_outHead) { OutItem *next = g_outHead->next; free(g_outHead->msg); free(g_outHead->blob); free(g_outHead); g_outHead = next; }
    g_outTail = NULL; g_outCount = 0; g_outBytes = 0;
}

int main(void) {
    const char *welcome = "{\"t\":\"welcome\",\"portable_sync\":1,\"compatibility\":{\"protocol\":1,\"host_fl\":\"26.1.7.5654\",\"wire\":\"flim-25-26-v1\",\"mixed\":true}}";
    char temp[MAX_PATH]; QItem task;
    InitializeCriticalSection(&g_lifecycleLock); InitializeCriticalSection(&g_qLock);
    InitializeCriticalSection(&g_sendLock); InitializeCriticalSection(&g_outLock);
    InitializeCriticalSection(&g_netTextLock); InitializeCriticalSection(&g_ulockLock);
    InitializeCriticalSection(&g_heldLock); InitializeCriticalSection(&g_slotLock);
    strcpy_s(g_myId, sizeof(g_myId), "older");
    GetTempPathA(MAX_PATH, temp); strcpy_s(g_tmpDir, sizeof(g_tmpDir), temp);
    g_fl.build = L"24.2.2.4597";
    assert(compat_incoming(welcome) && g_portableLocal && g_portableSync);
    on_project_load("not a project"); assert(!g_compatFault && !g_projBusy && g_qh == g_qt);
    handle_msg_inner("{\"t\":\"op\",\"from\":\"host\",\"source_fl\":\"26.1.7.5654\",\"op\":{\"kind\":\"portable.start\",\"for\":\"someone-else\",\"nch\":4}}");
    assert(g_qh == g_qt);
    handle_msg_inner("{\"t\":\"op\",\"from\":\"host\",\"source_fl\":\"26.1.7.5654\",\"op\":{\"kind\":\"portable.start\",\"for\":\"older\",\"nch\":4}}");
    assert(g_q[g_qh].type == T_PORTABLESTART && g_q[g_qh].a == 4); g_qh = g_qt;
    handle_msg_inner("{\"t\":\"op\",\"from\":\"host\",\"source_fl\":\"26.1.7.5654\",\"op\":{\"kind\":\"plug.state\",\"unit\":\"ch\",\"ch\":0,\"data\":\"bmV3\"}}");
    task = g_q[g_qh]; assert(task.type == T_LOAD && task.mode == 3 && !strcmp(task.presetFl, "26.1.7.5654")); discard_task(&task); g_qh = g_qt;
    assert(plug_param_blocked(REC_PLUG_FIRST) && plug_param_blocked(FX_ID(1, 0, 1)) && !plug_param_blocked(MIX_ID(1, 0x1fc0)));
    g_isLeader = 1; g_portableLocal = 0; g_ws = (HINTERNET)1; g_outBusy = 1;
    g_flBase = (char *)image; g_fl.PATARR = IMG_BASE + 8; PAT_STRIDE = 64;
    image[1] = (uint64_t)(uintptr_t)patterns;
    g_outEvt = CreateEventW(NULL, FALSE, FALSE, NULL);
    portable_send("older");
    assert(g_outCount == 3 && strstr(g_outHead->msg, "portable.start") && strstr(g_outHead->msg, "\"for\":\"older\""));
    assert(strstr(g_outHead->next->msg, "notes.full") && strstr(g_outTail->msg, "state.full"));
    clear_out(); portable_send("bad\\id"); assert(!g_outCount);
    handle_msg_inner("{\"t\":\"op\",\"from\":\"older\",\"source_fl\":\"24.2.2.4597\",\"op\":{\"kind\":\"state.req\",\"portable\":1}}");
    assert(g_q[g_qh].type == T_PORTABLESEND && !strcmp(g_q[g_qh].path, "older")); discard_task(&g_q[g_qh]); g_qh = g_qt;
    channelList=channels; channels[1]=(uint64_t)(uintptr_t)channelItems;
    image[2]=(uint64_t)(uintptr_t)&channelList; g_fl.CHANLIST=IMG_BASE+16;
    g_addChannel=add_channel;g_delChannel=delete_channel;channels[2]=5;
    assert(portable_channels(3) && channels[2]==3 && adds==3 && deletes==5);
    assert(portable_channels(1) && channels[2]==1);
    assert(portable_channels(MAX_CH) && channels[2]==MAX_CH);
    { unsigned previous=adds; assert(!portable_channels(MAX_CH+1) && adds==previous); }
    g_portableLocal=1;g_nslot=1;g_slots[0].id=0;g_slots[0].last=6400;g_hash[hash_id(0)]=1;
    apply_full_state("{\"t\":\"op\",\"op\":{\"kind\":\"state.full\",\"vals\":[[0,6400]]}}");
    assert(g_qh!=g_qt && g_q[g_qh].type==T_PARAM && g_q[g_qh].v==6400);g_qh=g_qt;
    g_ws = NULL; g_portableLocal = 1;
    portable_start(2); assert(g_compatFault && strstr(g_alert, "local project was kept"));
    CloseHandle(g_outEvt);
    puts("Portable host order: negotiation, targeted bootstrap order, newer project bypass, newer preset isolation, blocked plugin controls, recovery failure preserves local project: OK");
    return 0;
}
