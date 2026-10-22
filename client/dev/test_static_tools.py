"""Offline regression tests; no FL installation, process, or proprietary fixture needed."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import flre
import sigscan


SOURCE = '''
#define IMG_BASE 0x400000
typedef struct {
    const wchar_t *build;
    int msgbox;
    uint64_t REC, MSGBOX_CORE;
    uint32_t CH_SELECTED;
} FlBuild;
static const FlBuild k_flBuilds[] = {
    { .build = L"25.2.3.5171", .msgbox = 1,
      .REC = 0x401000, .MSGBOX_CORE = 0x401020, .CH_SELECTED = 0x18 },
    { .build = L"25.1.6.4997", .msgbox = 2,
      .REC = 0x402000, .MSGBOX_CORE = 0, .CH_SELECTED = 0x20 },
};
#define REC_RVA (g_fl.REC - IMG_BASE) // ProcessRECEvent
#define MSGBOX_CORE_VA (g_fl.MSGBOX_CORE)
#define CH_SELECTED (g_fl.CH_SELECTED) // selected byte
#define NOTE_SIZE 24
#define FLPROJ_LOAD_SLOT (0x38 / 8) // byte displacement in a VMT
'''


def synthetic_image(code=b'\x90' * 64, version='25.2.3.5171'):
    img = sigscan.Image.__new__(sigscan.Image)
    img.path = Path(version + '.dll').resolve()
    img.sha256 = hashlib.sha256(code).hexdigest()
    img.size = len(code)
    img.base = sigscan.IMG_BASE
    img.version = version
    img.text_rva = 0x1000
    img.text = code
    img.secs = {'.text': (0x1000, len(code), None)}
    img.funcs = [0x1000]
    img.cs = Cs(CS_ARCH_X86, CS_MODE_64)
    img.cs.detail = True
    return img


def fake_pe(raw, version=(26, 1, 6, 5639), machine=0x8664):
    section = SimpleNamespace(Name=b'.text\0', VirtualAddress=0x1000, Misc_VirtualSize=len(raw),
                              PointerToRawData=0, SizeOfRawData=len(raw), get_data=lambda: raw)
    return SimpleNamespace(FILE_HEADER=SimpleNamespace(Machine=machine),
                           OPTIONAL_HEADER=SimpleNamespace(Magic=0x20b, ImageBase=0x400000),
                           sections=[section], parse_data_directories=lambda **kwargs: None,
                           VS_FIXEDFILEINFO=[SimpleNamespace(FileVersionMS=version[0] << 16 | version[1],
                                                            FileVersionLS=version[2] << 16 | version[3])])


class StaticToolsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'engine.c'
        self.source.write_text(SOURCE, encoding='utf-8')
        self.output = self.root / 'evidence.json'

    def run_main(self, images, result=None, extra=()):
        with patch.object(sigscan, 'Image', side_effect=images), \
             patch.object(sigscan, 'map_addresses', return_value=result or []) as mapping, \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            code = sigscan.main(['reference.dll', 'target.dll', str(self.source), '--json', str(self.output), *extra])
        return code, json.loads(self.output.read_text(encoding='utf-8')), mapping

    def test_current_engine_reads_all_typed_fields_and_build_offsets(self):
        source = Path(__file__).resolve().parents[1] / 'engine' / 'flim_engine.c'
        table = sigscan.BuildTable(source)
        rows = dict((name, value) for name, value, _ in table.addresses('25.2.3.5171'))
        self.assertEqual(len(rows), 62)
        self.assertEqual(rows['REC_RVA'], 0xf4e910)  # output remains a normalized VA
        self.assertEqual(rows['CURPAT_VA'], 0x1499de0)
        offsets = dict((name, value) for name, value, _ in sigscan.parse_offsets(table, '25.1.6.4997'))
        self.assertEqual(offsets['CH_SELECTED'], 0x344)
        self.assertEqual(offsets['PAT_STRIDE'], 0xa4)
        self.assertEqual(offsets['NOTE_SIZE'], 24)
        self.assertEqual(offsets['CH_SETNAME_VSLOT'], 0x70)
        self.assertTrue(set(table.offset_fields) <= offsets.keys())

    def test_reference_version_is_required_and_never_guessed(self):
        with self.assertRaisesRegex(ValueError, 'reference_version is required'):
            sigscan.parse_table(self.source)
        with self.assertRaisesRegex(ValueError, 'has no k_flBuilds row'):
            sigscan.parse_table(self.source, '25.2.5.5319')

    def test_explicit_row_selection_including_optional_zero(self):
        self.assertEqual(sigscan.parse_table(self.source, '25.1.6.4997')[0][1], 0x402000)
        self.assertEqual(sigscan.parse_table(self.source, '25.1.6.4997')[1][1], 0)
        self.assertEqual(sigscan.parse_offsets(self.source, '25.1.6.4997')[0][1], 0x20)

    def test_incomplete_duplicate_empty_and_expression_tables_fail(self):
        invalid = [SOURCE.replace('.CH_SELECTED = 0x18', ''),
                   SOURCE.replace('.REC = 0x401000', '.REC = 0x401000, .REC = 0x401010'),
                   SOURCE.replace('.REC = 0x401000', '.REC = 0x401000 + 1'),
                   SOURCE.replace('.REC = 0x401000', '.REC = 0'),
                   SOURCE[:SOURCE.index('static const')] + 'static const FlBuild k_flBuilds[] = {};',
                   SOURCE.replace('25.1.6.4997', '25.2.3.5171')]
        for source in invalid:
            with self.subTest(source=source):
                self.source.write_text(source, encoding='utf-8')
                with self.assertRaises(ValueError):
                    sigscan.BuildTable(self.source)

    def test_comments_cannot_inject_addresses_or_braces(self):
        self.source.write_text(SOURCE.replace('.REC = 0x401000',
                              '/* } .REC = 0x999999, { */ .REC = 0x401000'), encoding='utf-8')
        self.assertEqual(sigscan.parse_table(self.source, '25.2.3.5171')[0][1], 0x401000)

    def test_experimental_build_is_not_a_trusted_reference(self):
        candidate = '''
#ifdef FLIM_EXPERIMENTAL_FL26
    { .build = L"26.1.6.5639", .msgbox = 1,
      .REC = 0x403000, .MSGBOX_CORE = 0x403020, .CH_SELECTED = 0x20 },
#endif
'''
        self.source.write_text(SOURCE.replace('};\n#define REC_RVA', candidate + '};\n#define REC_RVA'), encoding='utf-8')
        table = sigscan.BuildTable(self.source)
        self.assertEqual(len(table.builds), 2)
        with self.assertRaisesRegex(ValueError, 'has no k_flBuilds row'):
            table.select('26.1.6.5639')
        self.source.write_text(SOURCE.replace('};\n#define REC_RVA', '#if UNKNOWN\n#endif\n};\n#define REC_RVA'), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'unsupported build-table preprocessor'):
            sigscan.BuildTable(self.source)

    def test_wrong_reference_rejected_before_any_mapping_with_identity_evidence(self):
        old, new = synthetic_image(version='25.2.5.5319'), synthetic_image(version='26.1.6.5639')
        code, report, mapping = self.run_main([old, new])
        self.assertEqual(code, 2)
        self.assertEqual(report['status'], 'error')
        self.assertIn('has no k_flBuilds row', report['errors'][0])
        self.assertEqual(report['reference']['file_version'], old.version)
        self.assertEqual(report['target']['sha256'], new.sha256)
        mapping.assert_not_called()

    def test_reference_assertion_cannot_override_pe_version(self):
        code, report, mapping = self.run_main([synthetic_image(), synthetic_image()],
                                              extra=['--reference-version', '26.1.6.5639'])
        self.assertEqual(code, 2)
        self.assertIn('does not match PE version', report['errors'][0])
        mapping.assert_not_called()

    def test_empty_and_ambiguous_mapping_never_exit_success(self):
        code, report, _ = self.run_main([synthetic_image(), synthetic_image()])
        self.assertEqual(code, 2)
        self.assertIn('empty mapping', report['errors'][0])
        code, report, _ = self.run_main([synthetic_image(), synthetic_image()],
                                       [{'name': 'REC_RVA', 'status': 'weak'},
                                        {'name': 'MSGBOX_CORE_VA', 'status': 'unique'}])
        self.assertEqual(code, 1)
        self.assertEqual(report['summary']['unresolved'], ['REC_RVA'])
        self.assertFalse(report['runtime_validated'])

    def test_complete_candidate_evidence_includes_both_hashes_and_source(self):
        old, new = synthetic_image(), synthetic_image(b'\x90\xc3' * 32, '26.1.6.5639')
        code, report, _ = self.run_main([old, new], [{'name': 'REC_RVA', 'status': 'unique'},
                                                   {'name': 'MSGBOX_CORE_VA', 'status': 'confirmed'}])
        self.assertEqual(code, 0)
        self.assertEqual(report['reference']['sha256'], old.sha256)
        self.assertEqual(report['target']['sha256'], new.sha256)
        self.assertEqual(report['source']['sha256'], hashlib.sha256(self.source.read_bytes()).hexdigest())
        self.assertTrue(report['summary']['complete'])
        self.assertFalse(report['runtime_validated'])

    def test_missing_address_cannot_be_hidden_by_successful_remaining_candidates(self):
        code, report, _ = self.run_main([synthetic_image(), synthetic_image()],
                                       [{'name': 'REC_RVA', 'status': 'unique'}])
        self.assertEqual(code, 1)
        self.assertEqual(report['summary']['expected_total'], 2)
        self.assertEqual(report['summary']['missing'], ['MSGBOX_CORE_VA'])

    def test_json_cannot_overwrite_source(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            sigscan.main(['old.dll', 'new.dll', str(self.source), '--json', str(self.source)])
        self.assertEqual(error.exception.code, 2)
        self.assertEqual(self.source.read_text(encoding='utf-8'), SOURCE)

    def test_data_candidate_outside_target_sections_is_rejected(self):
        from collections import Counter
        with patch.object(sigscan, 'data_by_xrefs', return_value=([0x1000], 1, Counter({0x999999: 1}))), \
             contextlib.redirect_stdout(io.StringIO()):
            rows = sigscan.map_addresses(synthetic_image(), synthetic_image(), [('GLOBAL_VA', 0x401020, '')])
        self.assertEqual(rows[0]['status'], 'none')
        self.assertIsNone(rows[0]['new'])

    def test_cli_missing_arguments_is_argparse_error(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            sigscan.main([])
        self.assertEqual(error.exception.code, 2)

    def test_small_displacement_drift_is_not_dropped_by_four_byte_prefilter(self):
        old = synthetic_image(b'\x48\x8b\x41\x18\x48\x83\xc0\x01\xc3' + b'\x90' * 64)
        new = synthetic_image(b'\x48\x8b\x41\x20\x48\x83\xc0\x01\xc3' + b'\x90' * 64, '26.1.6.5639')
        with contextlib.redirect_stdout(io.StringIO()):
            rows = sigscan.offset_drift(old, new, self.source)
        selected = next(r for r in rows if r['name'] == 'CH_SELECTED')
        self.assertEqual(selected['votes'], [{'value': '0x20', 'count': 1}])
        self.assertEqual(selected['status'], 'observed')

    def test_xref_at_last_instruction_is_indexed_and_cache_is_per_image(self):
        old = synthetic_image(b'\xe8\x00\x00\x00\x00')
        new = synthetic_image(b'\xe8\x01\x00\x00\x00')
        self.assertEqual(sigscan.build_xref_index(old)[('call', 0x1005)], [0x1000])
        self.assertEqual(sigscan.build_xref_index(new)[('call', 0x1006)], [0x1000])
        self.assertEqual(sigscan.build_xref_index(synthetic_image(b'\xc3')), {})

    def test_code_read_outside_text_does_not_slice_from_end(self):
        self.assertEqual(synthetic_image().code(0xfff, 10), b'')

    def test_image_identity_comes_from_fixed_pe_metadata_and_file_bytes(self):
        target = self.root / 'engine.dll'
        raw = b'\x90' * 64
        target.write_bytes(raw)
        with patch.object(sigscan.pefile, 'PE', return_value=fake_pe(raw)):
            image = sigscan.Image(target)
        self.assertEqual(image.version, '26.1.6.5639')
        self.assertEqual(image.sha256, hashlib.sha256(raw).hexdigest())
        pe = fake_pe(raw)
        pe.VS_FIXEDFILEINFO = []
        with patch.object(sigscan.pefile, 'PE', return_value=pe), self.assertRaisesRegex(ValueError, 'no fixed PE file version'):
            sigscan.Image(target)
        with patch.object(sigscan.pefile, 'PE', return_value=fake_pe(raw, machine=0x14c)), self.assertRaisesRegex(ValueError, 'x64'):
            sigscan.Image(target)

    def test_flre_requires_a_target_and_switching_resets_all_caches(self):
        with patch.dict(os.environ, {}, clear=True), self.assertRaisesRegex(ValueError, 'select FLEngine'):
            flre.load_target()
        first, second = self.root / 'first.dll', self.root / 'second.dll'
        first.write_bytes(b'first')
        second.write_bytes(b'second')
        with patch.object(flre.pefile, 'PE', side_effect=lambda data, **kw: fake_pe(data)):
            flre.load_target(first)
            for cache in ('_pd', '_stubs', '_names', '_tx'):
                setattr(flre, cache, {'stale': True})
            with patch.dict(os.environ, {'FLIM_FL_DLL': str(second)}):
                identity = flre.load_target()
            self.assertEqual(identity['sha256'], hashlib.sha256(b'second').hexdigest())
            for cache in ('_pd', '_stubs', '_names', '_tx'):
                self.assertIsNone(getattr(flre, cache))
            self.assertEqual(flre.TEXT[4], b'.text')


if __name__ == '__main__':
    unittest.main()
