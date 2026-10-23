#pragma once
#include <stdint.h>
#include <string.h>

#define FLIM_WIRE "flim-25-26-v1"
#define FLIM_COMPAT_PROTOCOL 1

/* Canonical PE versions. Reject aliases so profile paths and handshake identities agree. */
static int flim_version_parse(const char *s, unsigned parts[4]) {
    int i;
    if (!s) return 0;
    for (i = 0; i < 4; i++) {
        unsigned n = 0;
        if (*s < '0' || *s > '9' || (*s == '0' && s[1] >= '0' && s[1] <= '9')) return 0;
        do { n = n * 10 + (unsigned)(*s++ - '0'); if (n > 65535) return 0; }
        while (*s >= '0' && *s <= '9');
        parts[i] = n;
        if (i < 3) { if (*s++ != '.') return 0; } else if (*s) return 0;
    }
    return 1;
}

static int flim_version_family(const char *s) {
    unsigned v[4];
    return flim_version_parse(s, v) && (v[0] == 25 || v[0] == 26);
}

static int flim_version_cmp(const char *a, const char *b) {
    unsigned x[4], y[4]; int i;
    if (!flim_version_parse(a, x) || !flim_version_parse(b, y)) return 0;
    for (i = 0; i < 4; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

static int flim_wire_known(const char *kind) {
    static const char *const kinds[] = { "project.load", "plug.full", "notes.full", "state.full",
        "state.req", "plug.state", "chan.add", "chan.del", "notes.set", "clips.set", "meta.set",
        "pat.op", "focus", "cur", "fx.clear", "chan.order", "param.set" };
    size_t i;
    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) if (!strcmp(kind, kinds[i])) return 1;
    return 0;
}

static int flim_wire_opaque(const char *kind) {
    return !strcmp(kind, "project.load") || !strcmp(kind, "plug.full") ||
           !strcmp(kind, "plug.state") || !strcmp(kind, "chan.add");
}
