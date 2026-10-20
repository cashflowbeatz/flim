# Helpers for poking at FL's state through the probe (see README.md). A, B = the pids of the
# leader and member FL started by restart_test.ps1, read from the engine log.
import fl, struct, sys, os, re

def session_pids(log=os.path.join(os.environ['TEMP'], 'flim_engine.log')):
    t = open(log, encoding='utf-8', errors='replace').read()
    pid = lambda role: int(re.findall(r'\[(\d+)\] server: \{"t":"welcome".{0,40}"role":"%s"' % role, t)[-1])
    return pid('leader'), pid('member')

try: A, B = session_pids()
except (OSError, IndexError): A = B = None
def marks(p):
    PL = p.q(p.g(0x14993e8)); L = p.q(PL + 0xd50); a = p.q(L)
    n = p.q(a - 8) if a else 0
    out = []
    for i in range(n):
        r = p.r(a + i * 0x34, 0x34)
        out.append((struct.unpack('<i', r[0:4])[0], struct.unpack('<i', r[4:8])[0], p.ustr(struct.unpack('<Q', r[8:16])[0]), r[16:].hex()))
    return out
def ts(p): return p.d(p.g(0x14997d0)), p.d(p.g(0x1497bd0))
def chans(p):
    lst = p.q(p.g(0x1498140)); items = p.q(lst + 8); n = p.d(lst + 0x10)
    return [(i, p.ustr(p.q(p.q(items + 8 * i) + 0x50))) for i in range(n)]
def pats(p, n=6):
    tbl = p.g(0x1498920); out = []
    for i in range(1, n + 1):
        r = tbl + i * 0xC0
        na = p.q(r + 0x28); cnt = p.d(na + 0x14) if na else 0
        out.append((i, p.ustr(p.q(r)), cnt))
    return out
def patop(p, slot, *args):
    proj = p.q(p.q(p.g(0x149a508)) + 0x10); vmt = p.q(proj)
    fn = p.q(vmt + slot)
    r = p.cmd('call %x %d %s' % (fn, proj, ' '.join('%d' % (x & 0xFFFFFFFFFFFFFFFF) for x in args)))
    return r
def setup(a):
    import struct
    for pat, nm in ((1, 'Verse'), (2, 'Chorus'), (3, 'Bridge')):
        a.call(0x11cad70, pat, a.mkstr(nm))
        arr = a.call(0x11cb490, pat, 1)
        rec = a.alloc(24)
        a.w(rec, struct.pack('<IHHIHHBBBBBBBB', 0, 0x4000, 0, 96 * pat, 60 + pat, 0, 120, 0, 64, 0, 64, 100, 128, 128))
        a.cmd('call %x %d %d' % (a.q(a.q(arr) + 8), arr, rec))
