"""Test ongoing piano-roll edits in two owned FL processes; this does not inject MIDI input."""
import os
import struct
import sys
import time

if not os.environ.get('FLIM_TEST_A') or not os.environ.get('FLIM_TEST_B'):
    raise SystemExit('Set FLIM_TEST_A and FLIM_TEST_B to the owned test process IDs.')

import fl
import mk
import feature_test as ft

for pid in (mk.A, mk.B):
    fl.inject(pid)
a, b = fl.Probe(mk.A), fl.Probe(mk.B)
print('Piano-roll recording-path test:', mk.V(a).build, 'hosting', mk.V(b).build, flush=True)


def records(p, pat):
    v = mk.V(p)
    arr = p.q(p.g(v.PATARR) + pat * v.PAT_STRIDE + 0x28)
    if not arr:
        return []
    count = p.d(arr + 0x14)
    data = p.q(arr + 8)
    out = []
    for i in range(count):
        rec = bytearray(p.r(data + i * 24, 24))
        rec[0x13] &= 0x7f
        out.append(bytes(rec))
    return out


def continuous(source, target, pat, label):
    v = mk.V(source)
    if records(source, pat) or records(target, pat):
        raise RuntimeError('Fresh empty test pattern required')
    arr = source.call(v.NOTEARR, pat, 1)
    fn = source.q(source.q(arr) + 8)
    memory = source.alloc(24)
    observed_at = None
    start = time.monotonic()
    for i in range(30):
        source.w(memory, struct.pack('<IHHIHHBBBBBBBB', i * 24, 0x4000, 0, 24, 60 + i % 12, 0, 120, 0, 64, 0, 64, 100, 128, 128))
        result = source.cmd('call %x %d %d' % (fn, arr, memory))
        if result.startswith(('ERR', 'fault')):
            raise RuntimeError(result)
        if observed_at is None and records(target, pat):
            observed_at = time.monotonic() - start
        time.sleep(0.08)
    ft.check(label + ' arrives before recording stops', lambda: observed_at is not None, True)
    ft.check(label + ' final 30-note content', lambda: records(target, pat), records(source, pat), 12)
    print(label, 'first received during insertion after', round(observed_at, 3) if observed_at is not None else 'never', 'seconds', flush=True)
    moved = bytearray(records(source, pat)[0])
    struct.pack_into('<I', moved, 0, 768)
    source.call(v.NOTEDEL, arr, 0, 0)
    source.w(memory, moved)
    result = source.cmd('call %x %d %d' % (fn, arr, memory))
    if result.startswith(('ERR', 'fault')):
        raise RuntimeError(result)
    ft.check(label + ' moved note', lambda: records(target, pat), records(source, pat), 12)
    source.call(v.NOTEDEL, arr, 0, 0)
    ft.check(label + ' deleted note', lambda: records(target, pat), records(source, pat), 12)


continuous(a, b, 30, 'Host to member')
continuous(b, a, 31, 'Member to host')
pat = 32
if records(a, pat) or records(b, pat):
    raise RuntimeError('Fresh empty concurrent test pattern required')
sources = [(p, mk.V(p), p.alloc(24)) for p in (a, b)]
arrays = [p.call(v.NOTEARR, pat, 1) for p, v, _ in sources]
expected = []
for i in range(12):
    for peer, (p, v, memory) in enumerate(sources):
        note = struct.pack('<IHHIHHBBBBBBBB', i * 48 + peer * 24, 0x4000, 0, 24, 60 + i + peer * 12, 0, 120, 0, 64, 0, 64, 100, 128, 128)
        expected.append(note)
        p.w(memory, note)
        arr = arrays[peer]
        result = p.cmd('call %x %d %d' % (p.q(p.q(arr) + 8), arr, memory))
        if result.startswith(('ERR', 'fault')):
            raise RuntimeError(result)
    time.sleep(.04)
for p, label in ((a, 'Host'), (b, 'Member')):
    ft.check(label + ' retains both concurrent recordings', lambda p=p: sorted(records(p, pat)), sorted(expected), 12)
if mk.V(a).build == mk.V(b).build:
    ft.t_channels(b, a, mk.V(b))
    ft.t_reorder(b, a, mk.V(b))
passed = sum(ok for _, ok in ft.results)
print('%d / %d passed' % (passed, len(ft.results)), flush=True)
raise SystemExit(0 if passed == len(ft.results) else 1)
