# sigscan.py - find flim's FL addresses (flim_engine.c's *_VA / *_RVA table) in another FL build.
#
#   py -3.11 sigscan.py <old FLEngine_x64.dll> <new FLEngine_x64.dll> [flim_engine.c] [--json out.json]
#
# The old DLL is the one the table was made for (25.2.3.5171). For each address:
#  - function (a .pdata function start, or code): a signature from the function's first bytes, with
#    call / jump targets and RIP-relative operands wildcarded; level 2 also wildcards memory
#    displacements and immediates (struct offsets moved). Searched in the new DLL's .text.
#  - data (.data / .bss / RTTI inside .text): every RIP-relative reference to it in the old code is
#    matched in the new code by a signature around the referencing instruction; the new instruction's
#    operand gives the new address. Majority vote over the references found.
# Reports per address: method, matches, new VA. Nothing here touches either FL installation.
import json, re, sys, collections
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP

IMG_BASE = 0x400000


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.secs = {}
        for s in self.pe.sections:
            name = s.Name.rstrip(b'\0').decode(errors='replace')
            self.secs.setdefault(name, (s.VirtualAddress, s.Misc_VirtualSize, s))
        va, size, s = self.secs['.text']
        self.text_rva = va
        self.text = s.get_data()[:size]
        self.funcs = self._pdata()
        self.cs = Cs(CS_ARCH_X86, CS_MODE_64)
        self.cs.detail = True

    def _pdata(self):
        try:
            self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
            return sorted({e.struct.BeginAddress for e in getattr(self.pe, 'DIRECTORY_ENTRY_EXCEPTION', [])})
        except Exception:
            return []

    def section_of(self, rva):
        for name, (va, size, _) in self.secs.items():
            if va <= rva < va + max(size, 1):
                return name
        return '?'

    def in_text(self, rva):
        return self.text_rva <= rva < self.text_rva + len(self.text)

    def code(self, rva, n):
        o = rva - self.text_rva
        return self.text[o:o + n]

    def insns(self, rva, n):
        return list(self.cs.disasm(self.code(rva, n), rva))


def mask_insns(insns, level):
    """bytes + mask (True = must match) for a run of instructions"""
    out, mask = bytearray(), []
    for ins in insns:
        b = bytes(ins.bytes)
        m = [True] * len(b)
        enc = ins  # capstone gives operand byte offsets
        disp_off, disp_size = enc.disp_offset, enc.disp_size
        imm_off, imm_size = enc.imm_offset, enc.imm_size
        rip = any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in ins.operands)
        branch = ins.group(1) or ins.group(7) or ins.mnemonic.startswith('j') or ins.mnemonic == 'call'
        if disp_size and (rip or level >= 2):
            for i in range(disp_off, disp_off + disp_size): m[i] = False
        if imm_size and (branch or level >= 2 or (imm_size >= 4 and IMG_BASE <= ins.operands[-1].imm < 0x10000000
                                                   if ins.operands and ins.operands[-1].type == X86_OP_IMM else False)):
            for i in range(imm_off, imm_off + imm_size): m[i] = False
        if branch and not imm_size and len(b) >= 5 and ins.mnemonic in ('call', 'jmp'):
            for i in range(len(b) - 4, len(b)): m[i] = False
        out += b
        mask += m
    return bytes(out), mask


def to_regex(b, mask):
    return re.compile(b''.join(re.escape(bytes([x])) if k else b'.' for x, k in zip(b, mask)), re.DOTALL)


def find_all(img, rx, limit=3):
    hits = []
    for m in rx.finditer(img.text):
        hits.append(img.text_rva + m.start())
        if len(hits) >= limit: break
    return hits


def func_sig(old, new, rva):
    """unique signature of the function at rva in old, searched in new -> (level, len, hits)"""
    best = None
    for level in (1, 2):
        for nbytes in (24, 40, 64, 96, 128):
            ins = old.insns(rva, nbytes + 16)
            acc, tot = [], 0
            for i in ins:
                acc.append(i); tot += i.size
                if tot >= nbytes: break
            b, m = mask_insns(acc, level)
            if sum(m) < 12: continue
            rx = to_regex(b, m)
            if len(find_all(old, rx, 2)) != 1: continue   # must be unique in the build it came from
            hits = find_all(new, rx, 3)
            if len(hits) == 1: return (level, tot, hits)
            if best is None or (hits and len(hits) < len(best[2])) or (not best[2] and hits): best = (level, tot, hits)
            if not hits: break                              # longer won't match either at this level
    return best or (0, 0, [])


def rip_target(ins):
    for op in ins.operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            return ins.address + ins.size + op.mem.disp
    if ins.mnemonic in ('call', 'jmp') and ins.operands and ins.operands[0].type == X86_OP_IMM:
        return ins.operands[0].imm
    return None


_xref_cache = {}
def build_xref_index(img):
    """map target -> [insn rva] for every RIP-relative operand in .text, by linear disassembly per function"""
    if id(img) in _xref_cache: return _xref_cache[id(img)]
    idx = collections.defaultdict(list)
    cs = Cs(CS_ARCH_X86, CS_MODE_64); cs.detail = False
    starts = img.funcs if img.funcs else [img.text_rva]
    starts = [s for s in starts if img.in_text(s)] + [img.text_rva + len(img.text)]
    # direct calls / jumps: every E8 / E9 rel32 in .text (linear disassembly stops at the data Delphi keeps
    # between functions, so it would miss callers); a false hit only costs a signature that won't match
    import numpy as np
    t = np.frombuffer(img.text, dtype=np.uint8)
    pos = np.nonzero((t[:-5] == 0xe8) | (t[:-5] == 0xe9))[0]
    b4 = np.lib.stride_tricks.sliding_window_view(t, 4)[pos + 1].astype(np.int64)
    rels = (b4[:, 0] | (b4[:, 1] << 8) | (b4[:, 2] << 16) | (b4[:, 3] << 24))
    rels = np.where(rels >= 1 << 31, rels - (1 << 32), rels)
    targets = img.text_rva + pos + 5 + rels
    for p, tg in zip(pos.tolist(), targets.tolist()):
        idx[('call', tg)].append(img.text_rva + p)
    for a, b in zip(starts, starts[1:]):
        code = img.code(a, b - a)
        for ins in cs.disasm_lite(code, a):
            addr, size, mnem, ops = ins
            if '[rip' in ops:
                m = re.search(r'\[rip ([+-]) (0x[0-9a-f]+|\d+)\]', ops)
                if m:
                    d = int(m.group(2), 0) * (1 if m.group(1) == '+' else -1)
                    idx[addr + size + d].append(addr)
    _xref_cache[id(img)] = idx
    return idx


def data_by_xrefs(old, new, rva, calls=False):
    """data: RIP-relative references; calls=True: direct call / jmp sites of a function"""
    idx = build_xref_index(old)
    refs = idx.get(('call', rva) if calls else rva, [])
    votes = collections.Counter()
    tried = 0
    for r in refs[:60]:
        # signature: up to 6 instructions before the reference + the reference + 4 after
        for back in (24, 16, 8, 0):
            start = r - back
            ins = [i for i in old.cs.disasm(old.code(start, back + 48), start)]
            ok = [i for i in ins if i.address <= r]
            if not ok or ok[-1].address != r: continue
            seq = ins[:len(ok) + 4]
            b, m = mask_insns(seq, 1)
            rx = to_regex(b, m)
            if len(find_all(old, rx, 2)) != 1: continue
            tried += 1
            hits = find_all(new, rx, 2)
            if len(hits) == 1:
                off = sum(i.size for i in seq[:len(ok) - 1])
                nins = list(new.cs.disasm(new.code(hits[0] + off, 16), hits[0] + off, 1))
                if nins:
                    t = rip_target(nins[0])
                    if t is not None: votes[t] += 1
            break
    return refs, tried, votes


def parse_table(src):
    rows = []
    for line in open(src, encoding='utf-8'):
        m = re.match(r'#define\s+([A-Z0-9_]+)\s+\(?(0x[0-9a-f]+)(\s*-\s*IMG_BASE\))?\s*(//\s*(.*))?', line)
        if not m or not (m.group(1).endswith('_VA') or m.group(1).endswith('_RVA')): continue
        v = int(m.group(2), 16)
        if m.group(1).endswith('_RVA') and not m.group(3): v += IMG_BASE   # REC_RVA is a plain RVA
        rows.append((m.group(1), v, (m.group(5) or '').strip()))
    return rows


def similarity(old, new, orva, nrva, n=24):
    """how alike two functions start: mnemonic sequence of the first n instructions (0..1)"""
    import difflib
    a = [i.mnemonic for i in old.cs.disasm(old.code(orva, n * 8), orva)][:n]
    b = [i.mnemonic for i in new.cs.disasm(new.code(nrva, n * 8), nrva)][:n]
    return difflib.SequenceMatcher(None, a, b).ratio() if a and b else 0.0


def by_position(old, new, rva, radius=40):
    """Delphi keeps a unit's functions in order: map rva by its index between the nearest neighbours
    (in .pdata order) whose own signatures match uniquely, if both builds have the same count between"""
    import bisect
    i = bisect.bisect_left(old.funcs, rva)
    if i >= len(old.funcs) or old.funcs[i] != rva: return None, 'not a .pdata function'
    before = after = None
    for j in range(i - 1, max(i - radius, 0), -1):
        lvl, n, hits = func_sig(old, new, old.funcs[j])
        if len(hits) == 1 and lvl == 1: before = (j, hits[0]); break
    for j in range(i + 1, min(i + radius, len(old.funcs))):
        lvl, n, hits = func_sig(old, new, old.funcs[j])
        if len(hits) == 1 and lvl == 1: after = (j, hits[0]); break
    if not before or not after: return None, 'no anchors nearby'
    nb = bisect.bisect_left(new.funcs, before[1]); na = bisect.bisect_left(new.funcs, after[1])
    if na - nb != after[0] - before[0]:
        return None, f'anchors {after[0] - before[0]} apart in old, {na - nb} in new'
    return new.funcs[nb + (i - before[0])], f'position {i - before[0]}/{after[0] - before[0]} between anchors'


def parse_offsets(src):
    """flim's struct offsets / strides / vmt slots: #define NAME 0x.. that aren't addresses"""
    out = []
    skip = re.compile(r'_VA$|_RVA$|^(REC_NONE|IMG_BASE|WM_|QCAP|MAX_|BAR_|BTN_|HOST_|EV_|SAMPLE_|BLOB_|CUR_|TT|REC_PLUG)')
    for line in open(src, encoding='utf-8'):
        m = re.match(r'#define\s+([A-Z0-9_]+)\s+\(?(0x[0-9a-f]+)(\s*/\s*8\))?\s*(//\s*(.*))?$', line.strip())
        if not m or skip.search(m.group(1)): continue
        out.append((m.group(1), int(m.group(2), 16), (m.group(5) or '').strip()))
    return out


def func_key(img, rva, nbytes=48):
    ins = []
    tot = 0
    for i in img.cs.disasm(img.code(rva, nbytes + 16), rva):
        ins.append(i); tot += i.size
        if tot >= nbytes: break
    if tot < 24: return None
    b, m = mask_insns(ins, 2)
    return bytes(x if k else 0 for x, k in zip(b, m)) + bytes(m)


def offset_drift(old, new, src, min_pairs=1):
    """pair functions by an offset-blind signature, align them, and report what each of flim's
    offsets / strides became in the new build"""
    import difflib, struct
    offs = parse_offsets(src)
    want = {v for _, v, _ in offs if v >= 0x10}
    keys = {}
    for img, tag in ((old, 0), (new, 1)):
        d = collections.defaultdict(list)
        for f in img.funcs:
            if not img.in_text(f): continue
            k = func_key(img, f)
            if k: d[k].append(f)
        keys[tag] = d
    pairs = [(v[0], keys[1][k][0]) for k, v in keys[0].items() if len(v) == 1 and len(keys[1].get(k, [])) == 1]
    print(f'paired {len(pairs)} functions by offset-blind signatures', flush=True)
    packed = {v: (struct.pack('<I', v), struct.pack('<H', v) if v < 0x10000 else None) for v in want}
    seen = collections.defaultdict(collections.Counter)
    def fbytes(img, f):
        import bisect
        i = bisect.bisect_right(img.funcs, f)
        end = img.funcs[i] if i < len(img.funcs) else f + 0x1000
        return min(end - f, 0x3000)
    def operand_values(ins):
        vals = []
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.mem.base != X86_REG_RIP: vals.append(('d', op.mem.disp))
            elif op.type == X86_OP_IMM: vals.append(('i', op.imm))
        return vals
    for fo, fn in pairs:
        n = fbytes(old, fo)
        code = old.code(fo, n)
        if not any(p4 in code for p4, _ in packed.values()) and not any(v < 0x80 for v in want):
            continue
        if not any(p4 in code for p4, _ in packed.values()):
            continue
        a = list(old.cs.disasm(code, fo))
        b = list(new.cs.disasm(new.code(fn, fbytes(new, fn)), fn))
        sm = difflib.SequenceMatcher(None, [(i.mnemonic, i.op_str.split(',')[0][:3]) for i in a],
                                     [(i.mnemonic, i.op_str.split(',')[0][:3]) for i in b], autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag != 'equal': continue
            for x, y in zip(a[i1:i2], b[j1:j2]):
                vx, vy = operand_values(x), operand_values(y)
                if len(vx) != len(vy): continue
                for (kx, ox), (ky, oy) in zip(vx, vy):
                    if kx == ky and ox in want: seen[ox][oy] += 1
    print(f'\n{"name":18} {"5171":>8}  4997 (what aligned instructions use instead; count)')
    for name, v, comment in offs:
        c = seen.get(v)
        if not c: print(f'{name:18} {hex(v):>8}  (not seen in paired code)'); continue
        tot = sum(c.values())
        top = ', '.join(f'{hex(k)} x{n}' for k, n in c.most_common(4))
        flag = '' if c.most_common(1)[0][0] == v else '   <-- MOVED?'
        print(f'{name:18} {hex(v):>8}  {top}  (of {tot}){flag}')


def main():
    if '--offsets' in sys.argv:
        a = [x for x in sys.argv[1:] if not x.startswith('--')]
        src = a[2] if len(a) > 2 else 'flim_engine.c'
        offset_drift(Image(a[0]), Image(a[1]), src)
        return
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    out_json = sys.argv[sys.argv.index('--json') + 1] if '--json' in sys.argv else None
    if out_json in args: args.remove(out_json)
    old, new = Image(args[0]), Image(args[1])
    src = args[2] if len(args) > 2 else __file__.replace('dev\\sigscan.py', 'engine\\flim_engine.c').replace('dev/sigscan.py', 'engine/flim_engine.c')
    rows = parse_table(src)
    fset = set(old.funcs)
    results = []
    for name, va, comment in rows:
        rva = va - IMG_BASE
        sec = old.section_of(rva)
        kind = 'func' if rva in fset or (sec == '.text' and not fset) else 'data'
        if kind == 'func':
            level, n, hits = func_sig(old, new, rva)
            refs, tried, votes = data_by_xrefs(old, new, rva, calls=True)
            for k in [k for k in votes if not new.in_text(k)]: del votes[k]   # VMProtect call stubs
            top = votes.most_common(1)[0] if votes else None
            byref = f'callers: {len(refs)} sites, {sum(votes.values())} matched' + (f', top {hex(top[0] + IMG_BASE)} x{top[1]}' if top else '')
            if len(hits) == 1:
                nva = hits[0] + IMG_BASE
                status = 'unique' if not top or top[0] == hits[0] else 'CONFLICT'
                if top and top[0] == hits[0]: status = 'confirmed'
            elif top:
                nva, status = top[0] + IMG_BASE, ('callers' if top[1] >= 2 and top[1] / sum(votes.values()) >= 0.6 else 'weak')
            else:
                nva, status = None, 'none'
            how = (f'sig L{level} {n}B' if len(hits) == 1 else f'sig: {len(hits)} hits') + '; ' + byref
            pos, why = by_position(old, new, rva)
            if pos is not None:
                if nva is None: nva, status = pos + IMG_BASE, 'position'
                elif pos + IMG_BASE != nva: status = 'CONFLICT'
                elif status in ('unique', 'weak', 'callers'): status = 'confirmed'
            how += '; ' + why
            if nva: how += f'; similarity {similarity(old, new, rva, nva - IMG_BASE):.2f}'
        else:
            refs, tried, votes = data_by_xrefs(old, new, rva)
            if votes:
                (t, c), = votes.most_common(1)
                agree = c / sum(votes.values())
                status = 'unique' if agree == 1 else 'vote'
                nva = t + IMG_BASE
                how = f'{len(refs)} refs, {tried} sigs, votes {dict((hex(k + IMG_BASE), v) for k, v in votes.most_common(3))}'
            else:
                status, nva, how = 'none', None, f'{len(refs)} refs, {tried} unique sigs, 0 found'
        results.append(dict(name=name, old=hex(va), sec=sec, kind=kind, status=status,
                            new=hex(nva) if nva else None, how=how, comment=comment))
        print(f"{name:16} {hex(va):>10} {sec:6} {kind:4} {status:8} {hex(nva) if nva else '-':>10}  {how}", flush=True)
    c = collections.Counter(r['status'] for r in results)
    print('\nsummary:', dict(c), f'of {len(results)}')
    if out_json:
        json.dump(results, open(out_json, 'w'), indent=1)


if __name__ == '__main__':
    main()
