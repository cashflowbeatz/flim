"""Bounded, read-only observations of candidate layouts in one FL 26 process.

No injection, remote allocation, writes, function calls, or process lifecycle actions.
Passing these checks does not establish compatibility or validate the whole layout.
"""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys

import pefile


TARGET_VERSION = '26.1.6.5639'
TARGET_SHA256 = 'bc3637c3e41694c820865e757d88a42bdc15b6db4011582a2ef8f4d1b05a2d95'
OFFSETS = dict(CH_SELECTED=0x32c, CH_GRID=0x7ac, FORM_BUSY=0x34d4, PAT_STRIDE=0xc0,
               PLFORM_PATS=0xd34, PL_MARKSEL=0xd8c, ED_TICKSPERPX=0xb04,
               ED_HSCROLL=0xb14, PR_GRID=0xc68, PL_GRID=0xd2c, GRID_VSCROLL=0x3fc,
               MIX_STRIDE=0x1478, MIX_SLOTS_OFF=0x134c, MIX_ROUTE=0x394)
FIXED = dict(MIX_ENABLED=0x18, ED_HWND=0x45c, CTL_BOUNDS=0x90, PR_TOPKEY=0x418,
             PR_KEYH=0x520, PAT_LENGTH=0x10, PAT_NOTES=0x28, NOTES_COUNT=0x14)
REQUIRED_GLOBALS = ('MIXCNT_VA', 'MIXARR_VA', 'CHANLIST_VA', 'CURPAT_VA', 'PATARR_VA',
                    'TSNUM_VA', 'TSDEN_VA', 'MAINFORM_VA', 'MAINOBJ_VA', 'PRFORM_VA', 'PLFORM_VA')


def pointer(value):
    return isinstance(value, int) and 0x10000 <= value < 0x0000800000000000


def read_evidence(path, mode):
    path = Path(path).resolve()
    raw = path.read_bytes()
    document = json.loads(raw)
    if document.get('schema_version') != 1 or document.get('mode') != mode:
        raise ValueError(f'{path}: expected sigscan schema 1 {mode} evidence')
    target = document.get('target', {})
    if target.get('file_version') != TARGET_VERSION or target.get('sha256') != TARGET_SHA256:
        raise ValueError(f'{path}: target does not match the pinned FL {TARGET_VERSION} DLL identity')
    rows = document.get('results', [])
    names = [row['name'] for row in rows]
    if len(names) != len(set(names)):
        raise ValueError(f'{path}: duplicate evidence names')
    return document, dict(path=str(path), sha256=hashlib.sha256(raw).hexdigest())


def load_profile(map_path, offsets_path):
    mapping, map_identity = read_evidence(map_path, 'addresses')
    offsets, offsets_identity = read_evidence(offsets_path, 'offsets')
    rows = {row['name']: row for row in mapping['results']}
    analysis_base = int(mapping['source']['analysis_image_base'], 0)
    addresses = {}
    for name in REQUIRED_GLOBALS:
        row = rows.get(name)
        if not row or row.get('status') not in ('unique', 'confirmed') or row.get('kind') != 'data':
            raise ValueError(f'{name}: a nonconflicting data candidate is required')
        va = int(row['new'], 0)
        if va < analysis_base:
            raise ValueError(f'{name}: candidate address is below the analysis image base')
        addresses[name] = va - analysis_base
    offset_rows = {row['name']: row for row in offsets['results']}
    for name, value in OFFSETS.items():
        votes = offset_rows.get(name, {}).get('votes', [])
        if not any(int(vote['value'], 0) == value for vote in votes):
            raise ValueError(f'{name}: pinned candidate {hex(value)} absent from offset evidence')
    return dict(target=mapping['target'], addresses=addresses, address_evidence=map_identity,
                offset_evidence=offsets_identity,
                offsets={name: hex(value) for name, value in OFFSETS.items()},
                fixed_offsets={name: hex(value) for name, value in FIXED.items()})


class ProcessReader:
    ACCESS = 0x1000 | 0x0010  # QUERY_LIMITED_INFORMATION | VM_READ

    def __init__(self, pid, expected_dll):
        if os.name != 'nt' or struct.calcsize('P') != 8:
            raise ValueError('requires 64-bit Python on Windows')
        self.pid = pid
        self.bytes_read = 0
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        k = self.kernel
        k.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        k.OpenProcess.restype = wintypes.HANDLE
        k.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        k.ReadProcessMemory.restype = wintypes.BOOL
        k.CloseHandle.argtypes = [wintypes.HANDLE]
        k.CloseHandle.restype = wintypes.BOOL
        k.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                               wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        k.QueryFullProcessImageNameW.restype = wintypes.BOOL
        k.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        k.GetProcessTimes.restype = wintypes.BOOL
        self.handle = k.OpenProcess(self.ACCESS, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            buf = ctypes.create_unicode_buffer(32768)
            length = wintypes.DWORD(len(buf))
            if not k.QueryFullProcessImageNameW(self.handle, 0, buf, ctypes.byref(length)):
                raise ctypes.WinError(ctypes.get_last_error())
            self.exe = Path(buf.value).resolve()
            self.dll = Path(expected_dll).resolve()
            if self.exe.name.lower() != 'fl64.exe' or self.exe.parent != self.dll.parent:
                raise ValueError('PID executable is not FL64.exe in the expected DLL installation')
            created, exited, kernel, user = (wintypes.FILETIME() for _ in range(4))
            if not k.GetProcessTimes(self.handle, ctypes.byref(created), ctypes.byref(exited),
                                     ctypes.byref(kernel), ctypes.byref(user)):
                raise ctypes.WinError(ctypes.get_last_error())
            self.created_filetime = (created.dwHighDateTime << 32) | created.dwLowDateTime
            self.base, self.module_size = self._module()
        except Exception:
            self.close()
            raise

    def _module(self):
        class ModuleEntry(ctypes.Structure):
            _fields_ = [('dwSize', wintypes.DWORD), ('th32ModuleID', wintypes.DWORD),
                        ('th32ProcessID', wintypes.DWORD), ('GlblcntUsage', wintypes.DWORD),
                        ('ProccntUsage', wintypes.DWORD), ('modBaseAddr', ctypes.c_void_p),
                        ('modBaseSize', wintypes.DWORD), ('hModule', wintypes.HMODULE),
                        ('szModule', wintypes.WCHAR * 256), ('szExePath', wintypes.WCHAR * 260)]
        k = self.kernel
        k.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
        k.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        k.Module32FirstW.argtypes = k.Module32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ModuleEntry)]
        k.Module32FirstW.restype = k.Module32NextW.restype = wintypes.BOOL
        snapshot = k.CreateToolhelp32Snapshot(0x08 | 0x10, self.pid)
        if snapshot == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            entry = ModuleEntry()
            entry.dwSize = ctypes.sizeof(entry)
            found = k.Module32FirstW(snapshot, ctypes.byref(entry))
            while found:
                if entry.szModule.lower() == 'flengine_x64.dll':
                    if entry.th32ProcessID != self.pid or Path(entry.szExePath).resolve() != self.dll:
                        raise ValueError('loaded FLEngine module path or PID differs from the expected target')
                    return entry.modBaseAddr, entry.modBaseSize
                found = k.Module32NextW(snapshot, ctypes.byref(entry))
            raise ValueError('FLEngine_x64.dll is not loaded in the requested process')
        finally:
            k.CloseHandle(snapshot)

    def read(self, address, size):
        if not pointer(address) or not 1 <= size <= 4096 or self.bytes_read + size > 1024 * 1024:
            raise ValueError('read outside bounded pointer, chunk, or observation budget')
        buffer = ctypes.create_string_buffer(size)
        count = ctypes.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, buffer, size, ctypes.byref(count)):
            raise ctypes.WinError(ctypes.get_last_error())
        if count.value != size:
            raise ValueError('partial ReadProcessMemory result')
        self.bytes_read += size
        return buffer.raw

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


class Layout:
    def __init__(self, reader, addresses):
        self.reader = reader
        self.addresses = addresses

    def value(self, address, fmt):
        return struct.unpack('<' + fmt, self.reader.read(address, struct.calcsize('<' + fmt)))[0]

    def q(self, address):
        return self.value(address, 'Q')

    def i(self, address):
        return self.value(address, 'i')

    def global_pointer(self, name, levels=1):
        value = self.reader.base + self.addresses[name]
        if not self.reader.base <= value <= self.reader.base + self.reader.module_size - 8:
            raise ValueError(f'{name}: candidate is outside the loaded module')
        for _ in range(levels):
            value = self.q(value)
            if not value:
                return 0
            if not pointer(value):
                raise ValueError(f'{name}: noncanonical pointer')
        return value

    def object_shape(self, address):
        if not address:
            return dict(present=False, plausible=True, limitation='null does not validate the offset')
        if not pointer(address):
            return dict(present=True, plausible=False, vmt_in_engine=False)
        vmt = self.q(address)
        inside = self.reader.base <= vmt < self.reader.base + self.reader.module_size
        return dict(present=True, plausible=inside, vmt_in_engine=inside)

    def mixer(self):
        count = self.i(self.global_pointer('MIXCNT_VA'))
        array = self.global_pointer('MIXARR_VA')
        if not 1 <= count <= 512 or not array:
            return dict(plausible=False, count=count, array_present=bool(array))
        observations = []
        for index in sorted({0, min(1, count - 1), count - 1}):
            record = array + index * OFFSETS['MIX_STRIDE']
            enabled = self.value(record + FIXED['MIX_ENABLED'], 'B')
            slots = [self.q(record + OFFSETS['MIX_SLOTS_OFF'] + i * 8) for i in range(10)]
            routes = []
            for destination in sorted({0, min(1, count - 1)}):
                route = record + OFFSETS['MIX_ROUTE'] + destination * 8
                level, on = self.i(route), self.value(route + 4, 'B')
                routes.append(dict(destination=destination, level=level, enabled=on,
                                   plausible=0 <= level <= 12800 and on in (0, 1)))
            slots_plausible = all(slot == 0 or pointer(slot) for slot in slots)
            observations.append(dict(index=index, enabled=enabled, effect_slots_nonnull=sum(bool(s) for s in slots),
                                     slot_pointers_plausible=slots_plausible, routes=routes,
                                     plausible=enabled in (0, 1) and slots_plausible and all(r['plausible'] for r in routes)))
        return dict(count=count, array_present=True, sampled_tracks=observations,
                    plausible=all(r['plausible'] for r in observations))

    def channels(self):
        obj = self.global_pointer('CHANLIST_VA', 2)
        if not obj:
            return dict(available=False, plausible=None, reason='channel-list object is null')
        count = self.i(obj + 0x10)
        if not 0 <= count <= 256:
            return dict(count=count, plausible=False)
        items = self.q(obj + 8)
        observations = []
        for index in range(min(count, 8)):
            channel = self.q(items + index * 8)
            selected = self.value(channel + OFFSETS['CH_SELECTED'], 'B')
            grid = self.object_shape(self.q(channel + OFFSETS['CH_GRID']))
            observations.append(dict(index=index, selected=selected, grid=grid,
                                     plausible=selected in (0, 1) and grid['plausible']))
        return dict(count=count, sampled_channels=observations,
                    plausible=(count == 0 or pointer(items)) and all(r['plausible'] for r in observations))

    def pattern(self):
        current = self.i(self.global_pointer('CURPAT_VA'))
        array = self.global_pointer('PATARR_VA')
        if not 1 <= current <= 999 or not array:
            return dict(current=current, array_present=bool(array), plausible=False)
        record = array + current * OFFSETS['PAT_STRIDE']
        length = self.i(record + FIXED['PAT_LENGTH'])
        notes = self.q(record + FIXED['PAT_NOTES'])
        note_count = self.i(notes + FIXED['NOTES_COUNT']) if notes else 0
        return dict(current=current, length_ticks=length, notes_present=bool(notes), note_count=note_count,
                    plausible=-1 <= length <= 0x7fffffff and 0 <= note_count <= 1000000)

    def time_signature(self):
        numerator = self.i(self.global_pointer('TSNUM_VA'))
        denominator = self.i(self.global_pointer('TSDEN_VA'))
        return dict(numerator=numerator, denominator=denominator,
                    plausible=1 <= numerator <= 64 and denominator in (1, 2, 4, 8, 16, 32, 64))

    def main_form(self):
        # FORM_BUSY is read from fl_form(), which uses MAINOBJ rather than MAINFORM.
        form = self.global_pointer('MAINOBJ_VA', 2)
        if not form:
            return dict(available=False, plausible=None, reason='main form is null')
        busy = self.q(form + OFFSETS['FORM_BUSY'])
        shape = self.object_shape(form)
        main = self.object_shape(self.global_pointer('MAINFORM_VA', 2))
        return dict(form=shape, mainform_global=main, busy_pointer_present=bool(busy),
                    busy_value=hex(busy), plausible=shape['plausible'] and main['plausible'] and (busy == 0 or pointer(busy)),
                    limitation='a null busy pointer alone cannot validate FORM_BUSY')

    def editor(self, playlist):
        form = self.global_pointer('PLFORM_VA' if playlist else 'PRFORM_VA', 2)
        if not form:
            return dict(available=False, plausible=None, reason='editor form is null; editor may never have been opened')
        grid = self.q(form + OFFSETS['PL_GRID' if playlist else 'PR_GRID'])
        if not grid:
            return dict(available=False, plausible=None, reason='editor grid is null')
        hwnd = self.value(form + FIXED['ED_HWND'], 'I')
        left, top, width, height = struct.unpack('<4i', self.reader.read(grid + FIXED['CTL_BOUNDS'], 16))
        tpp = self.value(form + OFFSETS['ED_TICKSPERPX'], 'd')
        hscroll, vscroll = self.i(form + OFFSETS['ED_HSCROLL']), self.i(grid + OFFSETS['GRID_VSCROLL'])
        result = dict(available=True, hwnd_candidate=hex(hwnd), hwnd_os_validated=False,
                      bounds=dict(left=left, top=top, width=width, height=height),
                      ticks_per_pixel=tpp if math.isfinite(tpp) else None,
                      horizontal_scroll=hscroll, vertical_scroll=vscroll,
                      form=self.object_shape(form), grid=self.object_shape(grid))
        plausible = (hwnd != 0 and -32768 <= left <= 32768 and -32768 <= top <= 32768 and
                     0 < width <= 32768 and 0 < height <= 32768 and math.isfinite(tpp) and
                     0 < tpp <= 1000000 and 0 <= hscroll <= 100000000 and 0 <= vscroll <= 100000000)
        if playlist:
            result['pattern_panel'] = self.object_shape(self.q(form + OFFSETS['PLFORM_PATS']))
            result['selected_marker'] = self.i(form + OFFSETS['PL_MARKSEL'])
            plausible = plausible and result['pattern_panel']['plausible'] and -1 <= result['selected_marker'] <= 1000000
        else:
            result['top_key'], result['key_height'] = self.i(grid + FIXED['PR_TOPKEY']), self.i(grid + FIXED['PR_KEYH'])
            plausible = plausible and 0 <= result['top_key'] <= 256 and 0 < result['key_height'] <= 512
        result['plausible'] = bool(plausible and result['form']['plausible'] and result['grid']['plausible'])
        return result

    def inspect(self):
        checks = {}
        for name, inspect in [('mixer', self.mixer), ('channels', self.channels), ('pattern', self.pattern),
                              ('time_signature', self.time_signature), ('main_form', self.main_form),
                              ('piano_roll', lambda: self.editor(False)), ('playlist', lambda: self.editor(True))]:
            try:
                checks[name] = inspect()
            except (OSError, ValueError, struct.error) as error:
                checks[name] = dict(plausible=False, error=str(error))
        return checks


def verify_disk_identity(path):
    path = Path(path).resolve()
    raw = path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != TARGET_SHA256:
        raise ValueError('on-disk target SHA-256 differs from the pinned FL 26 DLL')
    image = pefile.PE(data=raw, fast_load=True)
    if image.FILE_HEADER.Machine != 0x8664:
        raise ValueError('target PE is not AMD64')
    return dict(path=str(path), sha256=digest, file_version=TARGET_VERSION, size_bytes=len(raw),
                image_size=image.OPTIONAL_HEADER.SizeOfImage, timestamp=image.FILE_HEADER.TimeDateStamp)


def verify_mapped_headers(reader, disk):
    dos = reader.read(reader.base, 64)
    if dos[:2] != b'MZ':
        raise ValueError('mapped engine lacks an MZ header')
    nt_offset = struct.unpack_from('<I', dos, 0x3c)[0]
    if not 64 <= nt_offset <= 1024 * 1024:
        raise ValueError('mapped PE header offset is implausible')
    nt = reader.read(reader.base + nt_offset, 88)
    machine, timestamp = struct.unpack_from('<H', nt, 4)[0], struct.unpack_from('<I', nt, 8)[0]
    magic, size = struct.unpack_from('<H', nt, 24)[0], struct.unpack_from('<I', nt, 80)[0]
    if nt[:4] != b'PE\0\0' or machine != 0x8664 or magic != 0x20b:
        raise ValueError('mapped engine is not an AMD64 PE32+ image')
    if timestamp != disk['timestamp'] or size != disk['image_size'] or size != reader.module_size:
        raise ValueError('mapped PE timestamp/size does not match the on-disk target')
    return dict(timestamp=timestamp, image_size=size, matches_disk_header=True,
                limitation='mapped headers plus disk hash do not hash every live image byte')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--map', required=True, type=Path, dest='map_path')
    parser.add_argument('--offsets', required=True, type=Path)
    parser.add_argument('--json', required=True, type=Path)
    args = parser.parse_args(argv)
    if args.pid <= 0:
        parser.error('--pid must be positive')
    if args.json.resolve() in {args.map_path.resolve(), args.offsets.resolve()}:
        parser.error('--json must not overwrite input evidence')
    report = dict(schema_version=1, status='error', candidate_only=True, runtime_compatibility_validated=False,
                  pid=args.pid, observed_at_utc=datetime.now(timezone.utc).isoformat(), errors=[],
                  policy=dict(process_access=hex(ProcessReader.ACCESS), reads_only=True,
                              channel_and_project_names_read=False, max_read_bytes=1024 * 1024),
                  limitations=['One non-atomic snapshot; FL can change between reads.',
                               'Plausible values do not prove field meaning or support for a build.',
                               'Null/closed editors and idle fields cannot validate their offsets.',
                               'No OS window APIs are called; HWND values are memory candidates only.'])
    reader = None
    code = 2
    try:
        profile = load_profile(args.map_path, args.offsets)
        target = Path(profile['target']['path']).resolve()
        if args.json.resolve() in (target, target.with_name('FL64.exe')):
            raise ValueError('--json must not overwrite the target installation')
        disk = verify_disk_identity(target)
        reader = ProcessReader(args.pid, target)
        report.update(profile=profile, target=disk,
                      process=dict(executable=str(reader.exe), created_filetime=reader.created_filetime,
                                   module_base=hex(reader.base), module_size=reader.module_size),
                      mapped_identity=verify_mapped_headers(reader, disk))
        checks = Layout(reader, profile['addresses']).inspect()
        failures = [name for name, result in checks.items() if result.get('plausible') is False]
        unavailable = [name for name, result in checks.items() if result.get('plausible') is None]
        report.update(checks=checks, bytes_read=reader.bytes_read,
                      status='candidate_observations', summary=dict(failed=failures, unavailable=unavailable,
                                                                   all_available_checks_plausible=not failures))
        code = 1 if failures else 0
    except (OSError, ValueError, KeyError, pefile.PEFormatError) as error:
        report['errors'].append(str(error))
    finally:
        if reader:
            reader.close()
    # Protect all existing executable/library destinations, including on an error path.
    if args.json.suffix.lower() in ('.dll', '.exe'):
        print('error: output must not be a DLL or executable', file=sys.stderr)
        return 2
    args.json.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps(dict(status=report['status'], summary=report.get('summary'), errors=report['errors'])))
    return code


if __name__ == '__main__':
    sys.exit(main())
