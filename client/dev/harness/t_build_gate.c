// Synthetic FLP bytes only; no process, project file, or network connection is opened.
#include <assert.h>
#include "../../engine/flim_engine.c"

static unsigned char image[2048];
static size_t used;
static unsigned int checks;

static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void begin_image(int extended) {
    const unsigned char prefix[] = {0xac, 1, 1, 0, 0xc0, 4, 'F', 0, 'L', 0};
    memset(image, 0, sizeof(image));
    memcpy(image, "FLhd", 4); put32(image + 4, 6);
    memcpy(image + 14, "FLdt", 4);
    used = 22;
    if (extended) { memcpy(image + used, prefix, sizeof(prefix)); used += sizeof(prefix); }
}

static void version_bytes(const void *p, size_t n) {
    assert(n < 128 && used + n + 2 <= sizeof(image));
    image[used++] = EV_FL_VERSION;
    image[used++] = (unsigned char)n;
    memcpy(image + used, p, n); used += n;
}

static size_t finish_image(void) {
    put32(image + 18, (uint32_t)(used - 22));
    return used;
}

static void expect(FlpBuildCheck result, const unsigned char *p, size_t n, const char *expected) {
    char theirs[32], mine[32], current[32];
    assert(WideCharToMultiByte(CP_UTF8, 0, g_fl.build, -1, current, 32, NULL, NULL));
    assert(flp_check_build(p, n, theirs, mine) == result);
    assert(!strcmp(mine, current));
    assert(!strcmp(theirs, expected ? expected : ""));
    checks++;
}

int main(void) {
    const size_t builds = sizeof(k_flBuilds) / sizeof(k_flBuilds[0]);
    const size_t allowed = sizeof(FLIM_FL_BUILDS) / sizeof(FLIM_FL_BUILDS[0]);
    const char *invalid[] = {"", "25", "25.2.3", "25.2.3.5171.1", "25.2.3.5171junk",
        " 25.2.3.5171", "25.2.3.5171 ", "+25.2.3.5171", "25.-2.3.5171", "25..3.5171",
        "25.2.3.65536", "99999999999999999999999999999999999.2.3.1"};
    size_t i, j, n;
    int extended, nul, old25a = 0, old25b = 0;
    char current[32], other[32];

    assert(builds == allowed);
    for (i = 0; i < builds; i++) {
        unsigned int matches = 0;
        for (j = 0; j < allowed; j++) matches += !wcscmp(k_flBuilds[i].build, FLIM_FL_BUILDS[j]);
        assert(matches == 1);
        for (j = 0; j < i; j++) assert(wcscmp(k_flBuilds[i].build, k_flBuilds[j].build));
        old25a |= !wcscmp(k_flBuilds[i].build, L"25.2.3.5171");
        old25b |= !wcscmp(k_flBuilds[i].build, L"25.1.6.4997");
#ifndef FLIM_EXPERIMENTAL_FL26
        assert(wcsncmp(k_flBuilds[i].build, L"26.", 3));
#endif
        g_fl = k_flBuilds[i];
        for (j = 0; j < builds; j++) {
            assert(WideCharToMultiByte(CP_UTF8, 0, k_flBuilds[j].build, -1, other, 32, NULL, NULL));
            for (extended = 0; extended < 2; extended++) for (nul = 0; nul < 2; nul++) {
                begin_image(extended);
                version_bytes(other, strlen(other) + nul);
                n = finish_image();
                expect(i == j ? FLP_BUILD_MATCH : FLP_BUILD_MISMATCH, image, n, other);
            }
        }
    }
    assert(old25a && old25b);

    g_fl = k_flBuilds[0];
    assert(WideCharToMultiByte(CP_UTF8, 0, g_fl.build, -1, current, 32, NULL, NULL));
    expect(FLP_BUILD_INVALID, NULL, 0, NULL);
    begin_image(0); expect(FLP_BUILD_INVALID, image, finish_image(), NULL);
    begin_image(1); expect(FLP_BUILD_INVALID, image, finish_image(), NULL);
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        begin_image(0); version_bytes(invalid[i], strlen(invalid[i]));
        expect(FLP_BUILD_INVALID, image, finish_image(), NULL);
        begin_image(0); version_bytes(invalid[i], strlen(invalid[i]) + 1);
        expect(FLP_BUILD_INVALID, image, finish_image(), NULL);
    }
    begin_image(0); version_bytes("25.2.3.5171\0trailing", sizeof("25.2.3.5171\0trailing"));
    expect(FLP_BUILD_INVALID, image, finish_image(), NULL);
    begin_image(0); version_bytes(current, strlen(current) + 1); image[used++] = 0; image[23]++;
    expect(FLP_BUILD_INVALID, image, finish_image(), NULL);

    begin_image(1); version_bytes(current, strlen(current)); version_bytes(current, strlen(current) + 1);
    expect(FLP_BUILD_MATCH, image, finish_image(), current);
    for (i = 0; i < 2; i++) {
        begin_image(1);
        version_bytes(i ? current : "26.99.99.65535", i ? strlen(current) : strlen("26.99.99.65535"));
        version_bytes(i ? "26.99.99.65535" : current, i ? strlen("26.99.99.65535") : strlen(current));
        expect(FLP_BUILD_INVALID, image, finish_image(), NULL);
        begin_image(0);
        version_bytes(i ? current : "", i ? strlen(current) : 0);
        version_bytes(i ? "" : current, i ? 0 : strlen(current));
        expect(FLP_BUILD_INVALID, image, finish_image(), NULL);
    }

    begin_image(1); version_bytes(current, strlen(current) + 1); n = finish_image();
    for (i = 0; i < n; i++) expect(FLP_BUILD_INVALID, image, i, NULL);
    image[0] = 'X'; expect(FLP_BUILD_INVALID, image, n, NULL); image[0] = 'F';
    put32(image + 4, UINT32_MAX); expect(FLP_BUILD_INVALID, image, n, NULL); put32(image + 4, 6);
    put32(image + 18, UINT32_MAX); expect(FLP_BUILD_INVALID, image, n, NULL);
    begin_image(0); version_bytes(current, strlen(current)); image[used++] = 0xac;
    expect(FLP_BUILD_INVALID, image, finish_image(), NULL);
    printf("Project build gate: OK (%u checks across %zu builds; whitelist/table agreement and both older FL25 rows retained)\n", checks, builds);
    return 0;
}
