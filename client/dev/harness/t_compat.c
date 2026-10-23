/* Negotiation and project gates use synthetic messages only; no FL or network is opened. */
#include <assert.h>
#include "../../engine/flim_engine.c"

static const char *welcome = "{\"t\":\"welcome\",\"compatibility\":{\"protocol\":1,\"host_fl\":\"25.2.5.5319\",\"wire\":\"flim-25-26-v1\",\"mixed\":true}}";
static unsigned checks;

static void reset(void) {
    g_compatReady = g_compatFault = g_compatMixed = 0;
    g_hostFlBuild[0] = 0;
    g_fl.build = L"26.1.7.5654";
}
static void incoming(const char *m, int want) { assert(compat_incoming(m) == want); checks++; }
static void outgoing(const char *m, int want) { assert(compat_outgoing(m) == want); checks++; }
int main(void) {
    const char *invalid[] = {
        "{\"t\":\"welcome\"}",
        "{\"t\":\"welcome\",\"nested\":{\"compatibility\":{\"protocol\":1,\"host_fl\":\"25.2.5.5319\",\"wire\":\"flim-25-26-v1\",\"mixed\":true}}}",
        "{\"t\":\"welcome\",\"compatibility\":{\"protocol\":1,\"protocol\":1,\"host_fl\":\"25.2.5.5319\",\"wire\":\"flim-25-26-v1\",\"mixed\":true}}",
        "{\"t\":\"welcome\",\"compatibility\":{\"protocol\":1,\"host_fl\":\"27.1.7.5654\",\"wire\":\"flim-25-26-v1\",\"mixed\":true}}",
        "{\"t\":\"welcome\",\"compatibility\":{\"protocol\":1,\"host_fl\":\"25.02.5.5319\",\"wire\":\"flim-25-26-v1\",\"mixed\":true}}",
        "{\"t\":\"welcome\",\"compatibility\":{\"protocol\":1,\"host_fl\":\"25.2.5.5319\",\"wire\":\"unknown\",\"mixed\":true}}",
        "{\"t\":\"welcome\",\"compatibility\":{\"protocol\":1,\"host_fl\":\"25.2.5.5319\",\"wire\":\"flim-25-26-v1\",\"mixed\":false}}",
        "{\"t\":\"welcome\",\"compatibility\":{\"protocol\":1,\"host_fl\":\"26.1.8.6000\",\"wire\":\"flim-25-26-v1\",\"mixed\":true}}"
    };
    size_t i;
    InitializeCriticalSection(&g_sendLock);
    InitializeCriticalSection(&g_netTextLock);
    reset();
    g_workBound = 1; g_workGen = g_netGen - 1;
    incoming(welcome, 0);
    assert(!g_compatReady);
    g_workBound = 0;
    outgoing("{\"t\":\"op\",\"op\":{\"kind\":\"param.set\"}}", 0);
    outgoing("{\"t\":\"join\",\"compat\":{\"fl\":\"26.1.7.5654\"}}", 1);
    incoming(welcome, 1);
    assert(g_compatReady && g_compatMixed && !strcmp(g_hostFlBuild, "25.2.5.5319"));
    outgoing("{\"t\":\"op\",\"op\":{\"kind\":\"param.set\",\"id\":0,\"v\":12}}", 1);
    incoming("{\"t\":\"op\",\"source_fl\":\"25.2.5.5319\",\"op\":{\"kind\":\"plug.state\",\"data\":\"\"}}", 1);
    outgoing("{\"t\":\"op\",\"op\":{\"kind\":\"plug.state\",\"data\":\"\"}}", 0);
    assert(g_compatFault && !g_compatReady);
    reset(); incoming(welcome, 1);
    incoming("{\"t\":\"op\",\"source_fl\":\"26.1.7.5654\",\"op\":{\"kind\":\"chan.add\"}}", 0);
    reset(); incoming(welcome, 1);
    incoming("{\"t\":\"op\",\"op\":{\"kind\":\"plug.state\",\"source_fl\":\"25.2.5.5319\"}}", 0);
    reset(); incoming(welcome, 1);
    incoming("{\"t\":\"op\",\"source_fl\":\"26.1.7.5654\",\"op\":{\"kind\":\"notes.set\"}}", 1);
    outgoing("{\"t\":\"op\",\"op\":{\"kind\":\"future.op\"}}", 0);
    for (i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) { reset(); incoming(invalid[i], 0); }
    reset(); g_fl.build = L"25.2.5.5319"; incoming(welcome, 1);
    outgoing("{\"t\":\"op\",\"op\":{\"kind\":\"chan.add\"}}", 1);
    reset(); g_fl.build = L"25.2.5.5319";
    incoming("{\"t\":\"welcome\",\"compatibility\":{\"protocol\":1,\"host_fl\":\"26.1.7.5654\",\"wire\":\"flim-25-26-v1\",\"mixed\":true}}", 0);
    printf("Native build negotiation: OK (%u checks)\n", checks);
    return 0;
}
