#pragma once
#include <stddef.h>
#include <string.h>

typedef struct { const char *p; size_t n; } FlJson;
static void fj_space(const char **p, const char *end) {
    while (*p < end && (**p == ' ' || **p == '\r' || **p == '\n' || **p == '\t')) (*p)++;
}
static int fj_string(const char **p, const char *end) {
    if (*p == end || *(*p)++ != '"') return 0;
    while (*p < end) {
        unsigned char c = (unsigned char)*(*p)++;
        if (c == '"') return 1;
        if (c < 32) return 0;
        if (c == '\\') {
            int i;
            if (*p == end) return 0;
            c = *(*p)++;
            if (c == 'u') for (i = 0; i < 4; i++) {
                if (*p == end || !strchr("0123456789abcdefABCDEF", **p)) return 0;
                (*p)++;
            }
            else if (!strchr("\"\\/bfnrt", c)) return 0;
        }
    }
    return 0;
}
static int fj_value(const char **p, const char *end, unsigned depth) {
    const char *start; char close;
    fj_space(p, end);
    if (*p == end || depth > 32) return 0;
    if (**p == '"') return fj_string(p, end);
    if (**p == '{' || **p == '[') {
        int object = **p == '{'; close = object ? '}' : ']'; (*p)++; fj_space(p, end);
        if (*p < end && **p == close) { (*p)++; return 1; }
        for (;;) {
            if (object) {
                if (!fj_string(p, end)) return 0;
                fj_space(p, end); if (*p == end || *(*p)++ != ':') return 0;
            }
            if (!fj_value(p, end, depth + 1)) return 0;
            fj_space(p, end); if (*p == end) return 0;
            if (**p == close) { (*p)++; return 1; }
            if (*(*p)++ != ',') return 0;
            fj_space(p, end);
        }
    }
    start = *p;
    if (**p == 't' && end - *p >= 4 && !memcmp(*p, "true", 4)) { *p += 4; return 1; }
    if (**p == 'f' && end - *p >= 5 && !memcmp(*p, "false", 5)) { *p += 5; return 1; }
    if (**p == 'n' && end - *p >= 4 && !memcmp(*p, "null", 4)) { *p += 4; return 1; }
    if (**p == '-') (*p)++;
    if (*p == end) return 0;
    if (**p == '0') (*p)++;
    else {
        if (**p < '1' || **p > '9') return 0;
        do { (*p)++; } while (*p < end && **p >= '0' && **p <= '9');
    }
    if (*p < end && **p == '.') {
        (*p)++; if (*p == end || **p < '0' || **p > '9') return 0;
        do { (*p)++; } while (*p < end && **p >= '0' && **p <= '9');
    }
    if (*p < end && (**p == 'e' || **p == 'E')) {
        (*p)++; if (*p < end && (**p == '+' || **p == '-')) (*p)++;
        if (*p == end || **p < '0' || **p > '9') return 0;
        do { (*p)++; } while (*p < end && **p >= '0' && **p <= '9');
    }
    return *p != start;
}

/* Read one direct member, validating the complete object and rejecting duplicate field names.
   Protocol keys are literal ASCII; escaped key spellings are rejected to prevent aliases. */
static int fj_get(FlJson object, const char *key, FlJson *out) {
    const char *p = object.p, *end; int found = 0;
    const char *names[128]; size_t lengths[128], count = 0;
    if (!p) return 0;
    end = p + object.n;
    fj_space(&p, end); if (p == end || *p++ != '{') return 0;
    fj_space(&p, end);
    if (p < end && *p == '}') { p++; fj_space(&p, end); return 0; }
    for (;;) {
        const char *name = p, *value, *nameEnd; size_t len, i;
        if (!fj_string(&p, end)) return 0;
        nameEnd = p; len = (size_t)(p - name) - 2; name++;
        if (memchr(name, '\\', len) || count == 128) return 0;
        for (i = 0; i < count; i++) if (lengths[i] == len && !memcmp(names[i], name, len)) return 0;
        names[count] = name; lengths[count++] = len;
        fj_space(&p, end); if (p == end || *p++ != ':') return 0;
        fj_space(&p, end); value = p;
        if (!fj_value(&p, end, 0)) return 0;
        if (strlen(key) == len && !memcmp(name, key, len)) { out->p = value; out->n = (size_t)(p - value); found = 1; }
        (void)nameEnd;
        fj_space(&p, end); if (p == end) return 0;
        if (*p == '}') { p++; fj_space(&p, end); return p == end && found; }
        if (*p++ != ',') return 0;
        fj_space(&p, end);
    }
}
static int fj_text(FlJson value, char *out, size_t cap) {
    size_t i, n;
    if (value.n < 2 || value.p[0] != '"' || value.p[value.n-1] != '"') return 0;
    n = value.n - 2;
    if (n >= cap) return 0;
    for (i = 1; i <= n; i++) if ((unsigned char)value.p[i] < 32 || value.p[i] == '\\' || value.p[i] == '"') return 0;
    memcpy(out, value.p + 1, n); out[n] = 0; return 1;
}
static int fj_is(FlJson value, const char *literal) {
    return value.n == strlen(literal) && !memcmp(value.p, literal, value.n);
}
