# RE helpers for FLEngine_x64.dll (VAs at image base 0x400000, like the engine's *_VA defines)
import argparse
import hashlib
import json
import sys, struct, re, os, bisect
from pathlib import Path
import numpy as np
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = None
HERE = os.path.dirname(os.path.abspath(__file__))
BASE = 0x400000

data = pe = TEXT = None
SECS = []
IDENTITY = None
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False


def load_target(path=None):
    """Select one DLL and invalidate every index derived from the previous target."""
    global DLL, data, pe, BASE, SECS, TEXT, IDENTITY, _pd, _stubs, _names, _tx
    target = path or os.environ.get('FLIM_FL_DLL')
    if not target:
        raise ValueError('select FLEngine_x64.dll with --dll, FLIM_FL_DLL, or flre.load_target(path)')
    target = Path(target).resolve()
    raw = target.read_bytes()
    image = pefile.PE(data=raw, fast_load=True)
    if image.FILE_HEADER.Machine != 0x8664 or image.OPTIONAL_HEADER.Magic != 0x20b:
        raise ValueError(f'{target}: expected a Windows x64 PE image')
    image.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
    versions = getattr(image, 'VS_FIXEDFILEINFO', [])
    version = None
    if versions:
        v = versions[0]
        version = '.'.join(str(n) for n in (v.FileVersionMS >> 16, v.FileVersionMS & 0xffff,
                                           v.FileVersionLS >> 16, v.FileVersionLS & 0xffff))
    base = image.OPTIONAL_HEADER.ImageBase
    sections = [(s.VirtualAddress + base, s.Misc_VirtualSize, s.PointerToRawData,
                 s.SizeOfRawData, s.Name.rstrip(b'\0')) for s in image.sections]
    text = next((s for s in sections if s[4] == b'.text'), None)
    if text is None:
        raise ValueError(f'{target}: no .text section')
    DLL, data, pe, BASE, SECS, TEXT = str(target), raw, image, base, sections, text
    IDENTITY = dict(path=DLL, file_version=version, sha256=hashlib.sha256(raw).hexdigest(),
                    size_bytes=len(raw), image_base=hex(base))
    _pd = _stubs = _names = _tx = None
    return dict(IDENTITY)


def _ensure_loaded():
    if pe is None:
        load_target()

def off(va):
    _ensure_loaded()
    for v, sz, raw, rsz, n in SECS:
        if v <= va < v + min(sz, rsz): return raw + va - v
    return None

def va_of(o):
    _ensure_loaded()
    for v, sz, raw, rsz, n in SECS:
        if raw <= o < raw + rsz: return v + o - raw
    return None

def rd(va, n):
    o = off(va); return data[o:o + n] if o is not None else b""
def q(va): return struct.unpack('<Q', rd(va, 8))[0]
def d(va): return struct.unpack('<I', rd(va, 4))[0]

# .pdata function table
_pd = None
def pdata():
    global _pd
    _ensure_loaded()
    if _pd is None:
        dd = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
        o = off(dd.VirtualAddress + BASE)
        if o is None or dd.Size < 12:
            _pd = (np.array([], dtype=np.int64), np.array([], dtype=np.int64))
            return _pd
        raw = np.frombuffer(data[o:o + dd.Size // 12 * 12], dtype='<u4').reshape(-1, 3)
        raw = raw[raw[:, 0] != 0]
        order = np.argsort(raw[:, 0])
        _pd = (raw[order, 0].astype(np.int64) + BASE, raw[order, 1].astype(np.int64) + BASE)
    return _pd

def func_of(va):
    st, en = pdata()
    i = np.searchsorted(st, va, side='right') - 1
    if i >= 0 and st[i] <= va < en[i]:
        # merge chained entries (same function split in pieces): start = first
        return int(st[i]), int(en[i])
    return None

def dis(va, n=None, end=None, show=True):
    if end is None and n is None:
        f = func_of(va); end = f[1] if f else va + 0x200
    code = rd(va, (end - va) if end else n * 15)
    out = []
    for i, ins in enumerate(md.disasm(code, va)):
        if n is not None and i >= n: break
        if end is not None and ins.address >= end: break
        line = '%x: %s %s' % (ins.address, ins.mnemonic, ins.op_str)
        # annotate rip-relative
        m = re.search(r'\[rip ([+-]) 0x([0-9a-f]+)\]', ins.op_str)
        if m:
            t = ins.address + ins.size + (int(m.group(2), 16) * (1 if m.group(1) == '+' else -1))
            line += '   ; -> %x%s' % (t, name_at(t))
        if ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x'):
            t = resolve(int(ins.op_str, 16))
            if t != int(ins.op_str, 16): line += '   ; => %x' % t
            line += name_at(t)
        out.append(line)
    if show: print('\n'.join(out))
    return out

def in_text(va):
    _ensure_loaded()
    return TEXT[0] <= va < TEXT[0] + TEXT[1]

def resolve(t):
    # protector call stubs: E9 rel32 (+junk) outside .text
    for _ in range(4):
        if in_text(t): return t
        b = rd(t, 5)
        if not b or b[0] != 0xE9: return t
        t = t + 5 + struct.unpack('<i', b[1:5])[0]
    return t

_stubs = None
def stubs_to(target):
    global _stubs
    _ensure_loaded()
    if _stubs is None:
        _stubs = {}
        for v, sz, raw, rsz, n in SECS[1:]:
            if n in (b'.rsrc', b'.reloc', b'.pdata', b'.data', b'.bss'): continue
            a = np.frombuffer(data[raw:raw + min(sz, rsz)], dtype=np.uint8)
            if len(a) < 6: continue
            idx = np.nonzero(a[:-5] == 0xE9)[0]
            disp = a[idx+1].astype(np.int64) | (a[idx+2].astype(np.int64) << 8) | (a[idx+3].astype(np.int64) << 16) | (a[idx+4].astype(np.int64) << 24)
            disp = np.where(disp >= 2**31, disp - 2**32, disp)
            tg = idx + v + 5 + disp
            ok = (tg >= TEXT[0]) & (tg < TEXT[0] + TEXT[1])
            for i, t in zip(idx[ok], tg[ok]):
                _stubs.setdefault(int(t), []).append(int(i + v))
    return _stubs.get(target, [])

def callers(target):
    res = calls_to(target)
    for s in stubs_to(target): res += calls_to(s)
    return sorted(set(res))

def show_callers(target):
    for c in callers(target):
        f = func_of(c)
        print('%x in %s%s' % (c, hex(f[0]) if f else '?', name_at(f[0]) if f else ''))

# Delphi method tables: [len:u16][addr:u64][name:shortstring]
_names = None
def names():
    global _names
    _ensure_loaded()
    if _names is None:
        _names = {}
        tv, tsz = TEXT[0], TEXT[1]
        for m in re.finditer(rb'[\x0b-\x7f]\x00([\x00-\xff]{8})([\x03-\x60])([A-Za-z_][A-Za-z0-9_]{2,95})', data):
            ln = m.group(2)[0]
            nm = m.group(3)
            if len(nm) < ln: continue
            nm = nm[:ln]
            if struct.unpack('<H', data[m.start():m.start()+2])[0] != 2 + 8 + 1 + ln: continue
            a = struct.unpack('<Q', m.group(1))[0]
            if tv <= a < tv + tsz: _names.setdefault(a, nm.decode())
    return _names

def name_at(va):
    n = names().get(va)
    return ('  <' + n + '>') if n else ''

def find_name(pat):
    r = re.compile(pat)
    return sorted((a, n) for a, n in names().items() if r.search(n))

# xrefs: rip-relative disp32 anywhere in .text pointing to target(s)
_tx = None
def text_arr():
    global _tx
    _ensure_loaded()
    if _tx is None:
        v, sz, raw, rsz, n = TEXT
        _tx = np.frombuffer(data[raw:raw + min(sz, rsz)], dtype=np.uint8)
    return _tx

def xrefs(target, kind='rip'):
    t = text_arr()
    v0 = TEXT[0]
    n = len(t) - 4
    disp = (t[:n].astype(np.int64) | (t[1:n+1].astype(np.int64) << 8) | (t[2:n+2].astype(np.int64) << 16) | (t[3:n+3].astype(np.int64) << 24))
    disp = np.where(disp >= 2**31, disp - 2**32, disp)
    pos = np.arange(n, dtype=np.int64) + v0
    res = []
    # disp32 at pos: next-instr = pos+4 (+ imm bytes for some instrs; try 0,1,4)
    for extra in (0, 1, 2, 4):
        hits = np.nonzero(pos + 4 + extra + disp == target)[0]
        for h in hits:
            res.append(int(pos[h]))
    return sorted(set(res))

def calls_to(target):
    t = text_arr(); v0 = TEXT[0]
    n = len(t) - 4
    disp = (t[1:n+1].astype(np.int64) | (t[2:n+2].astype(np.int64) << 8) | (t[3:n+3].astype(np.int64) << 16) | (t[4:n+4].astype(np.int64) << 24))[:n-1]
    disp = np.where(disp >= 2**31, disp - 2**32, disp)
    pos = np.arange(n - 1, dtype=np.int64) + v0
    ok = ((t[:n-1] == 0xE8) | (t[:n-1] == 0xE9)) & (pos + 5 + disp == target)
    return [int(x) for x in pos[ok]]

def ins_at(va):
    # find the instruction containing va by disassembling from the function start
    f = func_of(va)
    s = f[0] if f else va - 16
    for ins in md.disasm(rd(s, va - s + 16), s):
        if ins.address <= va < ins.address + ins.size: return ins
    return None

def show_xrefs(target, ctx=0):
    for x in xrefs(target):
        ins = ins_at(x)
        f = func_of(x)
        fn = (' in %x%s' % (f[0], name_at(f[0]))) if f else ''
        print('%x: %s %s%s' % (ins.address, ins.mnemonic, ins.op_str, fn) if ins else '%x ?' % x)

def strs(s, wide=True):
    _ensure_loaded()
    out = []
    for enc in (['ascii', 'utf-16le'] if wide else ['ascii']):
        for m in re.finditer(re.escape(s.encode(enc)), data):
            out.append((enc, va_of(m.start())))
    return out


def meth(nm):
    _ensure_loaded()
    out = []
    for m in re.finditer(bytes([len(nm)]) + nm.encode() + rb'(?![A-Za-z0-9_])', data):
        a = struct.unpack('<Q', data[m.start()-8:m.start()])[0]
        if in_text(a): out.append(a)
    return out

def near(va, before=0x60, after=0x80):
    # find a start that decodes to an instruction boundary at va
    for k in range(before, before + 40):
        s = va - k
        addrs = [i.address for i in md.disasm(rd(s, k + 16), s)]
        if va in addrs:
            return dis(s, end=va + after)
    return dis(va, end=va + after)

def main(argv=None):
    parser = argparse.ArgumentParser(description='Read-only static helpers for one explicitly selected FL DLL.')
    parser.add_argument('--dll', help='FLEngine_x64.dll path (default: FLIM_FL_DLL)')
    parser.add_argument('--info', action='store_true', help='print selected PE identity as JSON')
    parser.add_argument('code', nargs='?', help='Python expression or statements using the helper functions')
    args = parser.parse_args(argv)
    if not args.info and args.code is None:
        parser.error('provide Python code or --info')
    try:
        identity = load_target(args.dll)
        if args.info:
            print(json.dumps(identity, indent=2))
        if args.code is not None:
            print(f"Target: {DLL} ({identity['file_version']}); SHA-256 {identity['sha256']}", file=sys.stderr)
            exec(args.code, globals())
    except (OSError, ValueError, pefile.PEFormatError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
