#pragma once
#include "flim_json.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define FLIM_CLOUD_INLINE (128u * 1024u)
#define FLIM_CLOUD_MAX (64u * 1024u * 1024u)
#define FLIM_CLOUD_FILE_MAX 90000000ull
typedef struct { char type[24], kind[65], key[257], hash[65], target[65]; size_t size; } FlCloud;

static int fc_target(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    for (; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return 0;
    return 1;
}

static int fc_uint(FlJson value, uint64_t *out) {
    size_t i; uint64_t n = 0;
    if (!value.n) return 0;
    for (i = 0; i < value.n; i++) {
        unsigned c = (unsigned char)value.p[i];
        if (c < '0' || c > '9' || n > (UINT64_MAX - (c - '0')) / 10) return 0;
        n = n * 10 + c - '0';
    }
    *out = n; return 1;
}
static int fc_text(FlJson object, const char *name, char *out, size_t cap) {
    FlJson value; return fj_get(object, name, &value) && fj_text(value, out, cap);
}
static int fc_safe(const char *s) {
    for (; *s; s++) if ((unsigned char)*s < 32 || *s == '"' || *s == '\\') return 0;
    return 1;
}
static int fc_root(FlJson root, const char *const *allowed, size_t count) {
    const char *p = root.p, *end = p + root.n;
    FlJson value; char name[65]; size_t i;
    /* fj_get validates the complete root, including duplicate and escaped keys. */
    if (!fj_get(root, "t", &value)) return 0;
    fj_space(&p, end); p++; fj_space(&p, end);
    while (p < end && *p != '}') {
        const char *start = p;
        if (!fj_string(&p, end)) return 0;
        value.p = start; value.n = (size_t)(p - start);
        if (!fj_text(value, name, sizeof(name))) return 0;
        for (i = 0; i < count; i++) if (!strcmp(name, allowed[i])) break;
        if (i == count) return 0;
        fj_space(&p, end); p++; fj_space(&p, end);
        if (!fj_value(&p, end, 0)) return 0;
        fj_space(&p, end);
        if (p < end && *p == ',') { p++; fj_space(&p, end); }
    }
    return 1;
}
/* Opaque data must not introduce a control field that the native dispatcher's searches
   could interpret before the operation the relay actually authorized. */
static int fc_fields(FlJson value, int top, unsigned depth) {
    const char *p = value.p, *end = p + value.n;
    int object; const char *names[128]; size_t sizes[128], count = 0;
    if (depth > 32) return 0;
    fj_space(&p, end);
    if (p == end || (*p != '{' && *p != '[')) return 1;
    object = *p++ == '{'; fj_space(&p, end);
    while (p < end && *p != (object ? '}' : ']')) {
        FlJson child; const char *start;
        if (object) {
            char name[257]; FlJson field; size_t i;
            start = p;
            if (!fj_string(&p, end)) return 0;
            field.p = start; field.n = (size_t)(p - start);
            if (!fj_text(field, name, sizeof(name)) || count == 128 ||
                !strcmp(name, "t") || (!top && !strcmp(name, "kind")) ||
                !strcmp(name, "source_fl") || !strcmp(name, "host_fl") ||
                !strcmp(name, "compat") || !strcmp(name, "compatibility")) return 0;
            for (i = 0; i < count; i++) if (sizes[i] == field.n && !memcmp(names[i], field.p, field.n)) return 0;
            names[count] = field.p; sizes[count++] = field.n;
            fj_space(&p, end); if (p == end || *p++ != ':') return 0;
        }
        fj_space(&p, end); start = p;
        if (!fj_value(&p, end, depth)) return 0;
        child.p = start; child.n = (size_t)(p - start);
        if (!fc_fields(child, 0, depth + 1)) return 0;
        fj_space(&p, end);
        if (p < end && *p == ',') { p++; fj_space(&p, end); }
        else break;
    }
    if (p == end || *p++ != (object ? '}' : ']')) return 0;
    fj_space(&p, end); return p == end;
}
static int fc_original(const char *message, FlCloud *out, FlJson *body) {
    FlJson root = { message, strlen(message) }, op, value;
    static const char *const opKeys[] = { "t", "op" }, *const projectKeys[] = { "t", "flp" };
    memset(out, 0, sizeof(*out));
    if (!fc_text(root, "t", out->type, sizeof(out->type))) return 0;
    if (!strcmp(out->type, "op")) {
        if (!fc_root(root, opKeys, 2) || !fj_get(root, "op", &op) || !fc_text(op, "kind", out->kind, sizeof(out->kind)) || !out->kind[0] ||
            !fc_fields(op, 1, 0)) return 0;
        if (fj_get(op, "key", &value) && (!fj_text(value, out->key, sizeof(out->key)) || !out->key[0])) return 0;
        if (fj_get(op, "for", &value) && (!fj_text(value, out->target, sizeof(out->target)) || !out->target[0] || !fc_target(out->target))) return 0;
        *body = op;
    } else if (!strcmp(out->type, "snapshot") || !strcmp(out->type, "project_swap")) {
        if (!fc_root(root, projectKeys, 2) || !fj_get(root, "flp", &value) || value.n < 2 || value.p[0] != '"') return 0;
        *body = value;
    } else return 0;
    return fc_safe(out->type) && fc_safe(out->kind) && fc_safe(out->key);
}
static int fc_reference(const char *message, FlCloud *out) {
    FlJson root = { message, strlen(message) }, value; char t[24]; uint64_t n; size_t i;
    memset(out, 0, sizeof(*out));
    if (!fc_text(root, "t", t, sizeof(t)) || strcmp(t, "stored") ||
        !fc_text(root, "type", out->type, sizeof(out->type)) ||
        !fc_text(root, "h", out->hash, sizeof(out->hash)) || strlen(out->hash) != 64 ||
        !fj_get(root, "s", &value) || !fc_uint(value, &n) || !n || n > FLIM_CLOUD_MAX) return 0;
    for (i = 0; i < 64; i++) if (!strchr("0123456789abcdef", out->hash[i])) return 0;
    out->size = (size_t)n;
    if (!strcmp(out->type, "op")) {
        if (!fc_text(root, "kind", out->kind, sizeof(out->kind)) || !out->kind[0]) return 0;
        if (fj_get(root, "key", &value) && (!fj_text(value, out->key, sizeof(out->key)) || !out->key[0])) return 0;
        if (fj_get(root, "for", &value) && (!fj_text(value, out->target, sizeof(out->target)) || !out->target[0] || !fc_target(out->target))) return 0;
    } else if (strcmp(out->type, "snapshot") && strcmp(out->type, "project_swap")) return 0;
    return fc_safe(out->kind) && fc_safe(out->key);
}
static int fc_header(const char *message, const char *hash, char *out, size_t cap) {
    FlCloud info; FlJson body; int n;
    if (!fc_original(message, &info, &body) || strlen(message) > FLIM_CLOUD_MAX) return 0;
    n = snprintf(out, cap, "{\"t\":\"stored\",\"type\":\"%s\",\"h\":\"%s\",\"s\":%llu%s%s%s%s%s%s%s%s%s}",
        info.type, hash, (unsigned long long)strlen(message),
        info.kind[0] ? ",\"kind\":\"" : "", info.kind, info.kind[0] ? "\"" : "",
        info.key[0] ? ",\"key\":\"" : "", info.key, info.key[0] ? "\"" : "",
        info.target[0] ? ",\"for\":\"" : "", info.target, info.target[0] ? "\"" : "");
    return n > 0 && (size_t)n < cap;
}
static char *fc_merge(const char *envelope, const char *original) {
    FlCloud ref, info; FlJson body, root = { envelope, strlen(envelope) }, value;
    char prefix[512], source[32], from[65], delivery[24]; uint64_t seq; int n, quoted = 0, escaped = 0; char *out; size_t i, used;
    if (!fc_reference(envelope, &ref) || strlen(original) != ref.size ||
        !fc_original(original, &info, &body) || strcmp(ref.type, info.type) ||
        strcmp(ref.kind, info.kind) || strcmp(ref.key, info.key) || strcmp(ref.target, info.target)) return NULL;
    if (!strcmp(info.type, "op")) {
        if (!fc_text(root, "source_fl", source, sizeof(source)) || !fc_safe(source) ||
            !fc_text(root, "from", from, sizeof(from)) || !fc_safe(from) ||
            !fj_get(root, "seq", &value) || !fc_uint(value, &seq)) return NULL;
        n = snprintf(prefix, sizeof(prefix), "{\"t\":\"op\",\"seq\":%llu,\"from\":\"%s\",\"source_fl\":\"%s\",\"op\":", (unsigned long long)seq, from, source);
    } else {
        memcpy(delivery, info.type, strlen(info.type) + 1);
        if (fj_get(root, "delivery", &value) && (!fj_text(value, delivery, sizeof(delivery)) ||
            (strcmp(delivery, "snapshot") && strcmp(delivery, "project_swap")))) return NULL;
        if (!fj_get(root, "epoch", &value) || !fc_uint(value, &seq)) return NULL;
        n = snprintf(prefix, sizeof(prefix), "{\"t\":\"%s\",\"epoch\":%llu,\"flp\":", delivery, (unsigned long long)seq);
    }
    if (n < 0 || (size_t)n >= sizeof(prefix) || body.n > FLIM_CLOUD_MAX - (size_t)n - 2) return NULL;
    out = (char *)malloc((size_t)n + body.n + 2);
    if (!out) return NULL;
    memcpy(out, prefix, (size_t)n); used = (size_t)n;
    for (i = 0; i < body.n; i++) {
        char c = body.p[i];
        if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) continue;
        out[used++] = c;
        if (escaped) escaped = 0;
        else if (quoted && c == '\\') escaped = 1;
        else if (c == '"') quoted = !quoted;
    }
    out[used++] = '}'; out[used] = 0;
    return out;
}
