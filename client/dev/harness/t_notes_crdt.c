#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int allocations = -1;
static void *checked_malloc(size_t n) { if (!allocations) return NULL; if (allocations > 0) allocations--; return malloc(n); }
static void *checked_calloc(size_t n, size_t size) { if (!allocations) return NULL; if (allocations > 0) allocations--; return calloc(n, size); }
#define malloc checked_malloc
#define calloc checked_calloc
#include "../../common/flim_notes_crdt.h"
#undef malloc
#undef calloc

static unsigned serial;
static int rng_fail;
static const unsigned char scope[16] = {1,2,3,4};
static int identity(unsigned char id[16]) {
    if (rng_fail) return 0;
    memset(id, 0, 16); id[0] = 255;
    ++serial; id[12] = (unsigned char)(serial >> 24); id[13] = (unsigned char)(serial >> 16);
    id[14] = (unsigned char)(serial >> 8); id[15] = (unsigned char)serial; return 1;
}
static void note(unsigned char *out, unsigned pitch) { memset(out, 0, FNC_NOTE); out[0] = (unsigned char)pitch; out[4] = 24; out[8] = 60; out[20] = 100; }
static void same(const FncState *a, const FncState *b) {
    unsigned na, nb; unsigned char *da = fnc_materialize(a, &na), *db = fnc_materialize(b, &nb);
    assert(na == nb && (!na || !memcmp(da, db, (size_t)na * FNC_NOTE)));
    assert(a->count == b->count && (!a->count || !memcmp(a->entries, b->entries, a->count * FNC_ENTRY)));
    free(da); free(db);
}
static void concurrent(void) {
    FncState a = {0}, b = {0}, c = {0};
    FncEntry *da, *db; unsigned na, nb, n; unsigned char baseline[24], next[48], *out;
    note(baseline, 60); assert(fnc_seed(&a, baseline, 1, scope)); assert(fnc_seed(&b, baseline, 1, scope)); assert(fnc_seed(&c, baseline, 1, scope));
    memcpy(next, baseline, 24); note(next + 24, 65);
    assert(fnc_capture(&a, next, 2, identity, &da, &na, FNC_LIMIT) && na == 1);
    note(next, 67); /* Other participant deletes the old note and inserts a different one. */
    assert(fnc_capture(&b, next, 1, identity, &db, &nb, FNC_LIMIT) && nb == 2);
    assert(fnc_merge(&a, scope, db, nb)); assert(fnc_merge(&a, scope, db, nb));
    assert(fnc_merge(&b, scope, da, na)); assert(fnc_merge(&c, scope, db, nb)); assert(fnc_merge(&c, scope, da, na));
    same(&a, &b); same(&a, &c); out = fnc_materialize(&a, &n); assert(n == 2 && out[0] == 65 && out[24] == 67);
    free(out); free(da); free(db); fnc_free(&a); fnc_free(&b); fnc_free(&c);
}
static void remove_before_add(void) {
    FncState a = {0}, b = {0}; FncEntry *add, *del; unsigned na, nd, n; unsigned char data[24], *out;
    assert(fnc_seed(&a, NULL, 0, scope)); assert(fnc_seed(&b, NULL, 0, scope)); note(data, 72);
    assert(fnc_capture(&a, data, 1, identity, &add, &na, FNC_LIMIT));
    assert(fnc_capture(&a, NULL, 0, identity, &del, &nd, FNC_LIMIT));
    assert(fnc_merge(&b, scope, del, nd)); assert(fnc_merge(&b, scope, add, na)); assert(fnc_merge(&b, scope, add, na));
    out = fnc_materialize(&b, &n); assert(!n && b.count == 1 && b.entries[0].dead); free(out); same(&a, &b);
    free(add); free(del); fnc_free(&a); fnc_free(&b);
}
static void duplicates_and_moves(void) {
    FncState a = {0}, b = {0}; FncEntry *da, *db; unsigned na, nb, n; unsigned char data[48], *out;
    note(data, 60); note(data + 24, 60); assert(fnc_seed(&a, data, 2, scope)); assert(fnc_seed(&b, data, 2, scope));
    assert(fnc_capture(&a, data, 1, identity, &da, &na, FNC_LIMIT) && na == 1 && da[0].dead);
    assert(fnc_merge(&b, scope, da, na)); out = fnc_materialize(&b, &n); assert(n == 1); free(out); free(da);
    note(data, 62); assert(fnc_capture(&a, data, 1, identity, &da, &na, FNC_LIMIT));
    note(data, 64); assert(fnc_capture(&b, data, 1, identity, &db, &nb, FNC_LIMIT));
    assert(fnc_merge(&a, scope, db, nb)); assert(fnc_merge(&b, scope, da, na)); same(&a, &b);
    out = fnc_materialize(&a, &n); assert(n == 2); /* Concurrent moves retain both fresh versions. */
    free(out); free(da); free(db); fnc_free(&a); fnc_free(&b);
}
static void malformed_and_atomic_failures(void) {
    FncState s = {0}; FncEntry *delta; unsigned edits, bytes, count, i; const FncEntry *entries;
    unsigned char data[48], different[16] = {9}, decoded[16], *wire;
    note(data, 60); note(data + 24, 61); assert(fnc_seed(&s, data, 1, scope));
    wire = fnc_pack(&s, s.entries, s.count, &bytes); assert(wire && bytes == 80);
    assert(fnc_unpack(wire, bytes, decoded, &entries, &count) && count == 1 && !memcmp(scope, decoded, 16));
    assert(!fnc_merge(&s, different, entries, count));
    wire[32 + 16] ^= 1; assert(!fnc_merge(&s, scope, entries, count)); wire[32 + 16] ^= 1;
    for (i = 0; i < 4; i++) {
        allocations = (int)i; assert(!fnc_capture(&s, data, 2, identity, &delta, &edits, FNC_LIMIT));
        allocations = -1; assert(s.count == 1 && !memcmp(s.entries, wire + 32, FNC_ENTRY));
    }
    allocations = 0; assert(fnc_merge(&s, scope, entries, count)); allocations = -1;
    rng_fail = 1; assert(!fnc_capture(&s, data, 2, identity, &delta, &edits, FNC_LIMIT)); rng_fail = 0;
    assert(s.count == 1 && !memcmp(s.entries, wire + 32, FNC_ENTRY));
    wire[0] ^= 1; assert(!fnc_unpack(wire, bytes, decoded, &entries, &count)); wire[0] ^= 1;
    wire[28] = 1; assert(!fnc_unpack(wire, bytes, decoded, &entries, &count)); wire[28] = 0;
    wire[32 + 40] = 2; assert(!fnc_unpack(wire, bytes, decoded, &entries, &count)); wire[32 + 40] = 0;
    wire[32 + 41] = 1; assert(!fnc_unpack(wire, bytes, decoded, &entries, &count)); wire[32 + 41] = 0;
    wire[32 + 35] = 128; assert(!fnc_unpack(wire, bytes, decoded, &entries, &count)); wire[32 + 35] = 0;
    assert(!fnc_unpack(wire, bytes - 1, decoded, &entries, &count));
    assert(!fnc_unpack(NULL, bytes, decoded, &entries, &count));
    assert(!fnc_capture(&s, data, 2, identity, &delta, &edits, 1));
    assert(fnc_capture(&s, data, 1, identity, &delta, &edits, 1) && !edits && !delta);
    data[19] |= 128; assert(!fnc_seed(&s, data, 1, scope) && s.count == 1);
    free(wire); fnc_free(&s);
}
static void history_limit(void) {
    FncState s = {0}; FncEntry *delta; unsigned edits, i; unsigned char *data = (unsigned char *)calloc(FNC_LIMIT + 1, FNC_NOTE);
    assert(data); for (i = 0; i < FNC_LIMIT; i++) memcpy(data + (size_t)i * FNC_NOTE, &i, sizeof(i));
    assert(fnc_seed(&s, data, FNC_LIMIT, scope)); assert(!fnc_seed(&s, data, FNC_LIMIT + 1, scope));
    assert(fnc_capture(&s, data, FNC_LIMIT, identity, &delta, &edits, FNC_LIMIT) && !edits); free(delta);
    assert(fnc_capture(&s, NULL, 0, identity, &delta, &edits, FNC_LIMIT) && edits == FNC_LIMIT); free(delta);
    assert(!fnc_capture(&s, data, 1, identity, &delta, &edits, FNC_LIMIT)); assert(s.count == FNC_LIMIT && s.entries[0].dead);
    fnc_free(&s); free(data);
}
static void shuffled_three_replicas(void) {
    FncState s[3] = {{0}}; unsigned round, i, j;
    for (i = 0; i < 3; i++) assert(fnc_seed(s + i, NULL, 0, scope));
    for (round = 0; round < 200; round++) {
        FncEntry *delta[3]; unsigned changes[3];
        for (i = 0; i < 3; i++) {
            unsigned n; unsigned char *current = fnc_materialize(s + i, &n), next[32 * FNC_NOTE];
            if (n > 26) n = 26; else if (round % 3 == 0 && n) n--;
            if (n) memcpy(next, current, n * FNC_NOTE);
            if (round % 3 != 0) { note(next + n * FNC_NOTE, (round + i * 17) % 128); n++; }
            assert(fnc_capture(s + i, next, n, identity, &delta[i], &changes[i], FNC_LIMIT)); free(current);
        }
        for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) {
            unsigned from = (round + j + i) % 3;
            assert(fnc_merge(s + i, scope, delta[from], changes[from]));
            assert(fnc_merge(s + i, scope, delta[from], changes[from]));
        }
        same(s, s + 1); same(s, s + 2);
        for (i = 0; i < 3; i++) free(delta[i]);
    }
    for (i = 0; i < 3; i++) fnc_free(s + i);
}
static void efficient_and_atomic_outputs(void) {
    FncState s = {0}; FncEntry *delta, incoming; unsigned changed, bytes = 99, n = 99;
    unsigned char data[24], decoded[16], *wire; const FncEntry *entries = (const FncEntry *)1;
    note(data, 60); assert(fnc_seed(&s, data, 1, scope));
    assert(fnc_seed(&s, data, 1, s.scope) && !memcmp(s.scope, scope, 16));
    allocations = 0; assert(fnc_merge(&s, scope, s.entries, s.count)); allocations = -1;
    allocations = 2; assert(fnc_capture(&s, data, 1, identity, &delta, &changed, FNC_LIMIT) && !changed && !delta); allocations = -1;
    incoming = s.entries[0]; incoming.dead = 1;
    allocations = 0; assert(!fnc_merge(&s, scope, &incoming, 1) && !s.entries[0].dead); allocations = -1;
    assert(fnc_merge(&s, scope, &incoming, 1));
    allocations = 0; assert(fnc_capture(&s, NULL, 0, identity, &delta, &changed, FNC_LIMIT) && !changed && !delta); allocations = -1;
    allocations = 0; assert(!fnc_pack(&s, s.entries, s.count, &bytes) && !bytes); allocations = -1;
    wire = fnc_pack(&s, s.entries, s.count, &bytes); assert(wire);
    wire[32 + 41] = 1;
    assert(!fnc_unpack(wire, bytes, decoded, &entries, &n) && !n && !entries);
    for (unsigned i = 0; i < 16; i++) assert(!decoded[i]);
    free(wire); fnc_free(&s);
    puts("PASS CRDT self-scope reseed, allocation-free duplicate merges/empty captures, reduced no-op allocations, and atomic failed wire outputs");
}
int main(void) {
    efficient_and_atomic_outputs(); concurrent(); remove_before_add(); duplicates_and_moves(); malformed_and_atomic_failures(); history_limit(); shuffled_three_replicas();
    puts("CRDT: OK (convergence, reordered and repeated deltas, concurrent delete/move, duplicate notes, wire validation, RNG/allocation failure, bounded history, 200 three-replica rounds)"); return 0;
}
