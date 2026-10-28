"""Exercise two explicitly owned FL test processes through the probe, without desktop input."""
import os
import struct
import sys

if not os.environ.get('FLIM_TEST_A') or not os.environ.get('FLIM_TEST_B'):
    raise SystemExit('Set FLIM_TEST_A and FLIM_TEST_B to the owned test process IDs.')

import fl
import mk
import feature_test as ft

for pid in (mk.A, mk.B):
    fl.inject(pid)
a, b = fl.Probe(mk.A), fl.Probe(mk.B)
v = mk.V(a)
print('Cloudflare runtime: host', v.build, 'member', mk.V(b).build, flush=True)
steps = ('params', 'mixer', 'returnpath') if '--transport-only' in sys.argv else ('params', 'mixer', 'channels', 'sample', 'returnpath')
for step in steps:
    ft.STEPS[step](a, b, v)

if '--large' in sys.argv:
    # A fresh pattern keeps this fixture separate from the small feature checks.
    pat, count = 20, 6000
    arr = a.call(v.NOTEARR, pat, 1)
    rec = a.alloc(24)
    fn = a.q(a.q(arr) + 8)
    for i in range(count):
        a.w(rec, struct.pack('<IHHIHHBBBBBBBB', i * 24, 0x4000, 0, 24, 60 + i % 12, 0, 120, 0, 64, 0, 64, 100, 128, 128))
        result = a.cmd('call %x %d %d' % (fn, arr, rec))
        if result.startswith(('ERR', 'fault')):
            raise RuntimeError(result)
        if (i + 1) % 1000 == 0:
            print('Large-edit fixture:', i + 1, 'notes inserted', flush=True)
    ft.check('6000-note edit reaches member', lambda: mk.pats(b, pat)[pat - 1][2], count, 30)
    log = open(os.path.join(os.environ['TEMP'], 'flim_engine.log'), encoding='utf-8', errors='replace').read()
    ft.check('host uses Cloudflare file upload', lambda: ('[%d] CLOUD uploaded serialized edit' % mk.A) in log, True)
    ft.check('member verifies Cloudflare download', lambda: ('[%d] CLOUD downloaded verified edit' % mk.B) in log, True)

passed = sum(ok for _, ok in ft.results)
print('%d / %d passed' % (passed, len(ft.results)), flush=True)
raise SystemExit(0 if passed == len(ft.results) else 1)
