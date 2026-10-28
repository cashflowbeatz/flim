#pragma once
#include <stddef.h>
#include <string.h>
#include <stdint.h>

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

static int fj_member_is(FlJson object, const char *key, const char *literal) {
    FlJson value;
    return fj_get(object, key, &value) && fj_is(value, literal);
}
static int fj_u32(FlJson value, uint32_t *out) {
    size_t i; uint32_t n = 0;
    if (!value.n || (value.n > 1 && value.p[0] == '0')) return 0;
    for (i = 0; i < value.n; i++) {
        unsigned c = (unsigned char)value.p[i];
        if (c < '0' || c > '9' || n > (UINT32_MAX - (c - '0')) / 10) return 0;
        n = n * 10 + c - '0';
    }
    *out = n; return 1;
}

/* Display strings accept JSON escapes; protocol identifiers still use fj_text. */
static int fj_hex4(const char *p, uint32_t *out) {
    unsigned i; uint32_t n = 0;
    for (i = 0; i < 4; i++) {
        unsigned c = (unsigned char)p[i], digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return 0;
        n = n * 16 + digit;
    }
    *out = n; return 1;
}
static int fj_decode(FlJson value, char *out, size_t cap) {
    size_t i = 1, o = 0;
    if (cap) out[0] = 0;
    if (!cap || value.n < 2 || value.p[0] != '"' || value.p[value.n - 1] != '"') return 0;
    while (i < value.n - 1) {
        uint32_t c = (unsigned char)value.p[i++];
        unsigned char bytes[4]; size_t n = 1;
        if (c < 32 || c == '"') goto invalid;
        if (c == '\\') {
            if (i == value.n - 1) goto invalid;
            c = (unsigned char)value.p[i++];
            if (c == 'u') {
                uint32_t low;
                if (value.n - 1 - i < 4 || !fj_hex4(value.p + i, &c)) goto invalid;
                i += 4;
                if (c >= 0xd800 && c <= 0xdbff) {
                    if (value.n - 1 - i < 6 || value.p[i] != '\\' || value.p[i + 1] != 'u' ||
                        !fj_hex4(value.p + i + 2, &low) || low < 0xdc00 || low > 0xdfff) goto invalid;
                    c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00; i += 6;
                } else if (c >= 0xdc00 && c <= 0xdfff) goto invalid;
            } else if (c == 'b') c = '\b'; else if (c == 'f') c = '\f';
            else if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t';
            else if (c != '"' && c != '\\' && c != '/') goto invalid;
        } else if (c >= 128) {
            unsigned need, j; uint32_t minimum;
            if (c >= 0xc2 && c <= 0xdf) { need = 1; minimum = 0x80; c &= 31; }
            else if (c >= 0xe0 && c <= 0xef) { need = 2; minimum = 0x800; c &= 15; }
            else if (c >= 0xf0 && c <= 0xf4) { need = 3; minimum = 0x10000; c &= 7; }
            else goto invalid;
            if (value.n - 1 - i < need) goto invalid;
            for (j = 0; j < need; j++) { unsigned b = (unsigned char)value.p[i++]; if ((b & 192) != 128) goto invalid; c = (c << 6) | (b & 63); }
            if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) goto invalid;
        }
        if (!c) goto invalid;
        if (c < 0x80) bytes[0] = (unsigned char)c;
        else if (c < 0x800) { n = 2; bytes[0] = (unsigned char)(0xc0 | (c >> 6)); bytes[1] = (unsigned char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { n = 3; bytes[0] = (unsigned char)(0xe0 | (c >> 12)); bytes[1] = (unsigned char)(0x80 | ((c >> 6) & 63)); bytes[2] = (unsigned char)(0x80 | (c & 63)); }
        else { n = 4; bytes[0] = (unsigned char)(0xf0 | (c >> 18)); bytes[1] = (unsigned char)(0x80 | ((c >> 12) & 63)); bytes[2] = (unsigned char)(0x80 | ((c >> 6) & 63)); bytes[3] = (unsigned char)(0x80 | (c & 63)); }
        if (n >= cap - o) goto invalid;
        memcpy(out + o, bytes, n); o += n;
    }
    out[o] = 0; return 1;
invalid:
    out[0] = 0; return 0;
}
