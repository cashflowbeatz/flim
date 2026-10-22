# sigscan.py - find candidates for flim's per-build FL addresses in another FL build.
#
#   py -3.11 sigscan.py <old FLEngine_x64.dll> <new FLEngine_x64.dll> [flim_engine.c] [--json out.json]
#
# The old DLL must exactly match a k_flBuilds row by PE file version. For each address:
#  - function (a .pdata function start, or code): a signature from the function's first bytes, with
#    call / jump targets and RIP-relative operands wildcarded; level 2 also wildcards memory
#    displacements and immediates (struct offsets moved). Searched in the new DLL's .text.
#  - data (.data / .bss / RTTI inside .text): every RIP-relative reference to it in the old code is
#    matched in the new code by a signature around the referencing instruction; the new instruction's
#    operand gives the new address. Majority vote over the references found.
# Reports per address: method, matches, new VA. Nothing here touches either FL installation.
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP

IMG_BASE = 0x400000


class Image:
    def __init__(self, path):
        self.path = Path(path).resolve()
        data = self.path.read_bytes()
        self.sha256 = hashlib.sha256(data).hexdigest()
        self.size = len(data)
        self.pe = pefile.PE(data=data, fast_load=True)
        if self.pe.FILE_HEADER.Machine != 0x8664 or self.pe.OPTIONAL_HEADER.Magic != 0x20b:
            raise ValueError(f'{self.path}: expected a Windows x64 PE image')
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
        versions = getattr(self.pe, 'VS_FIXEDFILEINFO', [])
        if not versions:
            raise ValueError(f'{self.path}: no fixed PE file version; cannot select a reference build')
        ver = versions[0]
        self.version = '.'.join(str(v) for v in (ver.FileVersionMS >> 16, ver.FileVersionMS & 0xffff,
                                                ver.FileVersionLS >> 16, ver.FileVersionLS & 0xffff))
        self.secs = {}
        for s in self.pe.sections:
            name = s.Name.rstrip(b'\0').decode(errors='replace')
            self.secs.setdefault(name, (s.VirtualAddress, s.Misc_VirtualSize, s))
        if '.text' not in self.secs:
            raise ValueError(f'{self.path}: no .text section')
        va, size, s = self.secs['.text']
        self.text_rva = va
        self.text = s.get_data()[:size]
        self.funcs = self._pdata()
        self.cs = Cs(CS_ARCH_X86, CS_MODE_64)
        self.cs.detail = True

    def identity(self):
        return dict(path=str(self.path), file_version=self.version, sha256=self.sha256,
                    size_bytes=self.size, image_base=hex(self.base), machine='AMD64')

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
        if not self.in_text(rva) or n <= 0:
            return b''
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


def build_xref_index(img):
    """map target -> [insn rva] for every RIP-relative operand in .text, by linear disassembly per function"""
    if hasattr(img, '_xref_index'): return img._xref_index
    idx = collections.defaultdict(list)
    cs = Cs(CS_ARCH_X86, CS_MODE_64); cs.detail = False
    starts = img.funcs if img.funcs else [img.text_rva]
    starts = [s for s in starts if img.in_text(s)] + [img.text_rva + len(img.text)]
    # direct calls / jumps: every E8 / E9 rel32 in .text (linear disassembly stops at the data Delphi keeps
    # between functions, so it would miss callers); a false hit only costs a signature that won't match
    import numpy as np
    t = np.frombuffer(img.text, dtype=np.uint8)
    if len(t) >= 5:
        pos = np.nonzero((t[:-4] == 0xe8) | (t[:-4] == 0xe9))[0]
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
    img._xref_index = idx
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


def strip_comments(text):
    # Preserve quoted strings; braces and assignments in comments are not C initializers.
    return re.sub(r'"(?:\\.|[^"\\])*"|/\*[\s\S]*?\*/|//[^\n]*',
                  lambda m: m[0] if m[0].startswith('"') else ' ', text)


class BuildTable:
    """Read literal designated initializers, rejecting syntax we cannot safely interpret."""
    def __init__(self, src):
        self.path = Path(src).resolve()
        raw = self.path.read_bytes()
        self.sha256 = hashlib.sha256(raw).hexdigest()
        self.source = raw.decode('utf-8-sig')
        clean = strip_comments(self.source)
        base = re.search(r'^\s*#define\s+IMG_BASE\s+(0x[\da-fA-F]+|\d+)\b', clean, re.M)
        if not base or int(base[1], 0) != IMG_BASE:
            raise ValueError(f'{self.path}: expected IMG_BASE {hex(IMG_BASE)}')
        struct = re.search(r'typedef\s+struct\s*\{([^{}]*)\}\s*FlBuild\s*;', clean, re.S)
        table = re.search(r'\bk_flBuilds\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;', clean, re.S)
        if not struct or not table:
            raise ValueError(f'{self.path}: missing FlBuild / k_flBuilds; no addresses parsed')
        self.address_fields = self._fields(struct[1], 'uint64_t')
        self.offset_fields = self._fields(struct[1], 'uint32_t')
        if not self.address_fields or not self.offset_fields:
            raise ValueError(f'{self.path}: empty FlBuild address or offset declaration')
        expected = set(self.address_fields + self.offset_fields + ['build', 'msgbox'])
        self.builds = {}
        body = self._reference_rows(table[1])
        rows = re.findall(r'\{([^{}]*)\}', body, re.S)
        if re.sub(r'\{[^{}]*\}|[\s,]', '', body):
            raise ValueError(f'{self.path}: unsupported nested build initializer')
        for row in rows:
            values = {}
            for entry in row.split(','):
                if not entry.strip():
                    continue
                match = re.fullmatch(r'\s*\.([A-Za-z_]\w*)\s*=\s*(?:L"([\d.]+)"|(0x[\da-fA-F]+|\d+)[uUlL]*)\s*', entry)
                if not match or match[1] in values:
                    raise ValueError(f'{self.path}: unsupported or duplicate build assignment: {entry.strip()}')
                values[match[1]] = match[2] if match[2] is not None else int(match[3], 0)
            missing, extra = expected - values.keys(), values.keys() - expected
            if missing or extra:
                raise ValueError(f'{self.path}: incomplete build row; missing={sorted(missing)}, extra={sorted(extra)}')
            version = values['build']
            if not isinstance(version, str) or not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', version):
                raise ValueError(f'{self.path}: invalid build version {version!r}')
            if version in self.builds:
                raise ValueError(f'{self.path}: duplicate build {version}')
            if values['msgbox'] not in (1, 2):
                raise ValueError(f'{self.path}: unsupported msgbox mode for {version}')
            for field in self.address_fields:
                if not isinstance(values[field], int):
                    raise ValueError(f'{version}: nonnumeric address {field}')
                optional_zero = field == 'MSGBOX_CORE' and values['msgbox'] == 2 and values[field] == 0
                if values[field] < IMG_BASE and not optional_zero:
                    raise ValueError(f'{version}: invalid address {field}={values[field]!r}')
            if any(not isinstance(values[f], int) or not 0 <= values[f] <= 0xffffffff for f in self.offset_fields):
                raise ValueError(f'{version}: invalid object offset')
            self.builds[version] = values
        if not self.builds:
            raise ValueError(f'{self.path}: empty k_flBuilds; no addresses parsed')
        self.aliases = {}
        for line in self.source.splitlines():
            match = re.match(r'\s*#define\s+([A-Z0-9_]+)\s+\(g_fl\.([A-Z0-9_]+)(?:\s*-\s*IMG_BASE)?\)\s*(?://\s*(.*))?$', line)
            if match:
                self.aliases[match[2]] = (match[1], match[3] or '')

    @staticmethod
    def _reference_rows(body):
        # Candidate-only rows must not silently become trusted porting references.
        active, experimental = [], False
        for line in body.splitlines():
            directive = line.strip()
            if directive == '#ifdef FLIM_EXPERIMENTAL_FL26' and not experimental:
                experimental = True
            elif directive == '#endif' and experimental:
                experimental = False
            elif directive.startswith('#'):
                raise ValueError(f'unsupported build-table preprocessor directive: {directive}')
            elif not experimental:
                active.append(line)
        if experimental:
            raise ValueError('unterminated experimental build block')
        return '\n'.join(active)

    @staticmethod
    def _fields(body, typename):
        fields = []
        for declaration in re.findall(r'\b' + typename + r'\s+([^;]+);', body):
            names = [name.strip() for name in declaration.split(',')]
            if any(not re.fullmatch(r'[A-Za-z_]\w*', name) for name in names):
                raise ValueError(f'unsupported {typename} field declaration')
            fields.extend(names)
        if len(fields) != len(set(fields)):
            raise ValueError(f'duplicate {typename} field declaration')
        return fields

    def select(self, version):
        if version not in self.builds:
            raise ValueError(f'reference DLL version {version!r} has no k_flBuilds row; supported: '
                             + ', '.join(self.builds))
        return self.builds[version]

    def addresses(self, version):
        row = self.select(version)
        return [(self.aliases.get(f, (f + '_VA', ''))[0], row[f],
                 self.aliases.get(f, ('', ''))[1]) for f in self.address_fields]


def parse_table(src, reference_version=None):
    """Return all address fields from the explicitly selected reference build."""
    if reference_version is None:
        raise ValueError('reference_version is required; use the reference DLL fixed file version')
    return BuildTable(src).addresses(reference_version)


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


def parse_offsets(src, reference_version=None):
    """flim's struct offsets / strides / vmt slots: #define NAME 0x.. that aren't addresses"""
    table = src if isinstance(src, BuildTable) else BuildTable(src)
    row = table.select(reference_version)
    out = [(f, row[f], table.aliases.get(f, ('', ''))[1]) for f in table.offset_fields]
    skip = re.compile(r'_VA$|_RVA$|^(REC_NONE|IMG_BASE|WM_|QCAP|MAX_|BAR_|BTN_|HOST_|EV_|SAMPLE_|BLOB_|CUR_|TT|REC_PLUG)')
    for line in table.source.splitlines():
        m = re.match(r'#define\s+([A-Z0-9_]+)\s+\(?(0x[0-9a-fA-F]+|\d+)(\s*/\s*8\))?\s*(//\s*(.*))?$', line.strip())
        if not m or skip.search(m.group(1)): continue
        # Decimal macros are mostly capacities and timing constants; NOTE_SIZE is a layout size.
        if not m[2].startswith('0x') and not m[1].endswith(('_SIZE', '_STRIDE', '_OFF', '_OFS')): continue
        out.append((m.group(1), int(m.group(2), 0), (m.group(5) or '').strip()))
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


def offset_drift(old, new, src, reference_version=None):
    """pair functions by an offset-blind signature, align them, and report what each of flim's
    offsets / strides became in the new build"""
    import difflib
    offs = parse_offsets(src, reference_version or old.version)
    want = {v for _, v, _ in offs}
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
    # x86 can encode a displacement or immediate in 1, 2, or 4 bytes.
    packed = {v.to_bytes(width, 'little') for v in want for width in (1, 2, 4) if v < 1 << (8 * width)}
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
        if not any(value in code for value in packed):
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
    print(f'\n{"name":18} {old.version:>16}  {new.version} (aligned operand values; count)')
    results = []
    for name, v, comment in offs:
        c = seen.get(v)
        votes = [dict(value=hex(value), count=count) for value, count in (c.most_common() if c else [])]
        status = 'observed' if len(votes) == 1 else 'ambiguous' if votes else 'none'
        results.append(dict(name=name, old=hex(v), status=status, votes=votes, comment=comment))
        if not c: print(f'{name:18} {hex(v):>8}  (not seen in paired code)'); continue
        tot = sum(c.values())
        top = ', '.join(f'{hex(k)} x{n}' for k, n in c.most_common(4))
        flag = '' if c.most_common(1)[0][0] == v else '   <-- MOVED?'
        print(f'{name:18} {hex(v):>8}  {top}  (of {tot}){flag}')
    return results


def map_addresses(old, new, rows):
    fset = set(old.funcs)
    results = []
    for name, va, comment in rows:
        if va == 0:
            results.append(dict(name=name, old='0x0', sec=None, kind='optional', status='not_applicable',
                                new=None, how='reference msgbox mode has no core wrapper', comment=comment))
            continue
        rva = va - IMG_BASE
        sec = old.section_of(rva)
        if sec == '?':
            raise ValueError(f'{name}: reference VA {hex(va)} is outside every PE section')
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
            for target in list(votes):
                if new.section_of(target) == '?':
                    del votes[target]
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
    return results


def main(argv=None):
    parser = argparse.ArgumentParser(description='Map static FL address candidates from an exact supported reference DLL.')
    parser.add_argument('reference', help='reference FLEngine_x64.dll whose fixed file version has a k_flBuilds row')
    parser.add_argument('target', help='target FLEngine_x64.dll')
    parser.add_argument('source', nargs='?', default=str(Path(__file__).resolve().parents[1] / 'engine' / 'flim_engine.c'))
    parser.add_argument('--reference-version', help='assert the reference DLL version; cannot override its PE metadata')
    parser.add_argument('--offsets', action='store_true', help='report layout operand drift instead of addresses')
    parser.add_argument('--json', type=Path, help='write an evidence object including identities, summary, and results')
    args = parser.parse_args(argv)
    if args.json and args.json.resolve() in {Path(path).resolve() for path in (args.reference, args.target, args.source)}:
        parser.error('--json must not overwrite a DLL or the engine source')
    report = dict(schema_version=1, mode='offsets' if args.offsets else 'addresses', status='error',
                  runtime_validated=False, results=[], errors=[],
                  limitations=['Static signature matches are candidates, not proof of runtime compatibility.',
                               'Offsets aggregate operand values across functions; verify each object layout in context.'])
    code = 2
    try:
        old, new = Image(args.reference), Image(args.target)
        report.update(reference=old.identity(), target=new.identity())
        if args.reference_version and args.reference_version != old.version:
            raise ValueError(f'--reference-version {args.reference_version} does not match PE version {old.version}')
        table = BuildTable(args.source)
        report.update(selected_build=old.version,
                      source=dict(path=str(table.path), sha256=table.sha256, analysis_image_base=hex(IMG_BASE)))
        table.select(old.version)
        rows = table.addresses(old.version)
        results = offset_drift(old, new, table, old.version) if args.offsets else map_addresses(old, new, rows)
        if not results:
            raise ValueError('empty mapping; no evidence produced')
        counts = dict(collections.Counter(r['status'] for r in results))
        accepted = {'observed'} if args.offsets else {'unique', 'confirmed', 'not_applicable'}
        unresolved = [r['name'] for r in results if r['status'] not in accepted]
        expected_names = [r[0] for r in parse_offsets(table, old.version)] if args.offsets else [r[0] for r in rows]
        actual_names = [r['name'] for r in results]
        missing = sorted(set(expected_names) - set(actual_names))
        unexpected = sorted(set(actual_names) - set(expected_names))
        duplicate = sorted(name for name, count in collections.Counter(actual_names).items() if count > 1)
        unresolved += missing + unexpected + duplicate
        complete = not unresolved and any(r['status'] != 'not_applicable' for r in results)
        report.update(results=results, status='candidates_complete' if complete else 'incomplete',
                      summary=dict(total=len(results), expected_total=len(expected_names), counts=counts,
                                   unresolved=unresolved, missing=missing, unexpected=unexpected,
                                   duplicate=duplicate, complete=complete))
        print('\nsummary:', counts, f'of {len(results)}; static candidates only')
        code = 0 if complete else 1
    except (OSError, ValueError, pefile.PEFormatError) as error:
        report['errors'].append(str(error))
        print(f'error: {error}', file=sys.stderr)
    if args.json:
        try:
            args.json.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        except OSError as error:
            print(f'error writing evidence: {error}', file=sys.stderr)
            return 2
    return code


if __name__ == '__main__':
    sys.exit(main())
