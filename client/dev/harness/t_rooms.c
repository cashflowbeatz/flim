#include <assert.h>
#include "../../engine/flim_engine.c"

int main(void) {
    NetUrl url;
    char session[FLIM_SESSION_CAP], link[400], longUrl[900];
    size_t i;
    const char *invalid[] = {
        "https://relay/room/1234567", "https://relay/room/123456789",
        "https://relay/room/1234a678", "https://relay/room/12345678/extra",
        "https://relay/room/12345678?key=hidden", "https://relay/room/12345678#frag",
        "https://relay/room/%31%32%33%34%35%36%37%38", "https://relay/room/12345678\\",
        "https://user@relay/room/12345678", "https://relay:+443/room/12345678",
        "https://relay: 443/room/12345678", "https://relay:65536/room/12345678",
        "https://relay:0/room/12345678", "https://relay/room/1234\t5678",
        "https://relay/12345678", "https://relay/03/extra", "https://relay/"
    };
    assert(parse_link("https://relay/room/00123456", &url, session));
    assert(!strcmp(session, "00123456") && !strcmp(url.shown, "relay"));
    assert(!wcscmp(url.path, L"/room/00123456") && url.secure && url.port == 443);
    session_link(&url, session, link, sizeof(link));
    assert(!strcmp(link, "https://relay/room/00123456"));
    assert(welcome_session("{\"session\":\"00123456\"}", &url, session));
    assert(!welcome_session("{\"session\":\"12345678\"}", &url, session));
    assert(!welcome_session("{\"session\":\"01\"}", &url, session));
    assert(!welcome_session("{\"session\":\"00123456\",\"session\":\"00123456\"}", &url, session));
    assert(!welcome_session("{\"nested\":{\"session\":\"00123456\"}}", &url, session));
    assert(!welcome_session("{\"session\":\"00123456x\"}", &url, session));
    assert(parse_host("wss://relay/room/00123456/", &url));
    assert(!wcscmp(url.path, L"/room/00123456"));
    assert(parse_link(" ws://localhost:49938/room/87654321/ ", &url, session));
    assert(!url.secure && url.port == 49938 && !strcmp(url.shown, "localhost:49938"));
    assert(parse_link("https://relay/3", &url, session));
    assert(!strcmp(session, "03") && !wcscmp(url.path, L"/"));
    assert(welcome_session("{\"session\":\"03\"}", &url, session));
    session_link(&url, session, link, sizeof(link));
    assert(!strcmp(link, "https://relay/03"));
    assert(!welcome_session("{\"session\":\"00123456\"}", &url, session));
    assert(parse_link("https://relay/01/", &url, session));
    assert(parse_host("https://relay", &url) && !wcscmp(url.path, L"/"));
    assert(parse_host("localhost:49938", &url) && url.port == 49938);
    assert(!parse_host("https://relay/01", &url));
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) assert(!parse_link(invalid[i], &url, session));
    memset(longUrl, 'x', sizeof(longUrl)); longUrl[sizeof(longUrl) - 1] = 0;
    assert(!parse_url(longUrl, &url));
    memcpy(longUrl, "https://", 8); longUrl[262] = '/'; strcpy_s(longUrl + 263, sizeof(longUrl) - 263, "01");
    assert(!parse_url(longUrl, &url));
    puts("Desktop room URLs, welcome identity and legacy slots: OK");
    return 0;
}
