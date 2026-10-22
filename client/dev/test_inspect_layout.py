"""Read-only layout checks tested with bounded synthetic memory, without a process."""
import json
from pathlib import Path
import struct
import tempfile
import unittest

import inspect_layout as live


class Memory:
    base = 0x400000
    module_size = 0x10000

    def __init__(self):
        self.cells = {}
        self.reads = []

    def put(self, address, fmt, *values):
        self.cells[address] = struct.pack('<' + fmt, *values)

    def read(self, address, size):
        self.reads.append((address, size))
        if address not in self.cells or len(self.cells[address]) != size:
            raise ValueError('read outside expected synthetic fields')
        return self.cells[address]


class LayoutTests(unittest.TestCase):
    def test_global_chain_rejects_noncanonical_pointer_and_out_of_image(self):
        memory = Memory()
        memory.put(0x400100, 'Q', 0xffffffffffffffff)
        layout = live.Layout(memory, {'X': 0x100, 'OUT': 0x10000})
        with self.assertRaisesRegex(ValueError, 'noncanonical'):
            layout.global_pointer('X')
        with self.assertRaisesRegex(ValueError, 'outside the loaded module'):
            layout.global_pointer('OUT')
        self.assertEqual(memory.reads, [(0x400100, 8)])

    def test_channel_count_caps_reads_before_item_access(self):
        memory = Memory()
        memory.put(0x400100, 'Q', 0x500000)
        memory.put(0x500000, 'Q', 0x600000)
        memory.put(0x600010, 'i', 1000000)
        result = live.Layout(memory, {'CHANLIST_VA': 0x100}).channels()
        self.assertFalse(result['plausible'])
        self.assertEqual(len(memory.reads), 3)

    def test_unopened_editor_is_unavailable_not_validated(self):
        memory = Memory()
        memory.put(0x400100, 'Q', 0)
        result = live.Layout(memory, {'PRFORM_VA': 0x100}).editor(False)
        self.assertFalse(result['available'])
        self.assertIsNone(result['plausible'])
        self.assertEqual(len(memory.reads), 1)

    def test_busy_offset_uses_mainobj_matching_engine_fl_form(self):
        memory = Memory()
        for slot, holder, form in [(0x100, 0x500000, 0x600000), (0x200, 0x500100, 0x700000)]:
            memory.put(memory.base + slot, 'Q', holder)
            memory.put(holder, 'Q', form)
            memory.put(form, 'Q', memory.base + 0x300)
        memory.put(0x600000 + live.OFFSETS['FORM_BUSY'], 'Q', 0)
        result = live.Layout(memory, {'MAINOBJ_VA': 0x100, 'MAINFORM_VA': 0x200}).main_form()
        self.assertTrue(result['plausible'])
        self.assertFalse(result['busy_pointer_present'])
        self.assertNotIn((0x700000 + live.OFFSETS['FORM_BUSY'], 8), memory.reads)

    def test_mapped_identity_mismatch_is_rejected(self):
        memory = Memory()
        dos = bytearray(64)
        dos[:2] = b'MZ'
        struct.pack_into('<I', dos, 0x3c, 0x80)
        nt = bytearray(88)
        nt[:4] = b'PE\0\0'
        struct.pack_into('<H', nt, 4, 0x8664)
        struct.pack_into('<I', nt, 8, 123)
        struct.pack_into('<H', nt, 24, 0x20b)
        struct.pack_into('<I', nt, 80, memory.module_size)
        memory.cells[memory.base] = bytes(dos)
        memory.cells[memory.base + 0x80] = bytes(nt)
        self.assertTrue(live.verify_mapped_headers(memory, {'timestamp': 123, 'image_size': memory.module_size})['matches_disk_header'])
        with self.assertRaisesRegex(ValueError, 'does not match'):
            live.verify_mapped_headers(memory, {'timestamp': 124, 'image_size': memory.module_size})

    def test_profile_requires_exact_hash_and_nonconflicting_used_globals(self):
        with tempfile.TemporaryDirectory() as folder:
            mapping_path, offsets_path = Path(folder) / 'map.json', Path(folder) / 'offsets.json'
            target = dict(file_version=live.TARGET_VERSION, sha256=live.TARGET_SHA256)
            mapping = dict(schema_version=1, mode='addresses', target=target,
                           source={'analysis_image_base': '0x400000'},
                           results=[dict(name=name, status='unique', kind='data', new='0x401000')
                                    for name in live.REQUIRED_GLOBALS])
            offsets = dict(schema_version=1, mode='offsets', target=target,
                           results=[dict(name=name, votes=[{'value': hex(value), 'count': 1}])
                                    for name, value in live.OFFSETS.items()])
            offsets_path.write_text(json.dumps(offsets), encoding='utf-8')
            mapping_path.write_text(json.dumps(mapping), encoding='utf-8')
            profile = live.load_profile(mapping_path, offsets_path)
            self.assertEqual(len(profile['addresses']), len(live.REQUIRED_GLOBALS))
            mapping['results'][0]['status'] = 'CONFLICT'
            mapping_path.write_text(json.dumps(mapping), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'nonconflicting data candidate'):
                live.load_profile(mapping_path, offsets_path)
            mapping['target']['sha256'] = '0' * 64
            mapping_path.write_text(json.dumps(mapping), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'pinned FL'):
                live.load_profile(mapping_path, offsets_path)

    def test_requested_process_rights_are_query_and_read_only(self):
        self.assertEqual(live.ProcessReader.ACCESS, 0x1000 | 0x10)


if __name__ == '__main__':
    unittest.main()
