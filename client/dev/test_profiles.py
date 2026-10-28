"""Profiles must remain bound to an exact reviewed field set and engine identity."""
import copy
import contextlib
import io
import json
import tempfile
from unittest.mock import patch
from pathlib import Path
from types import SimpleNamespace
import unittest

import profiles
from sigscan import BuildTable


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.table = BuildTable(profiles.ROOT / 'engine/flim_engine.c', include_experimental=True)
        fields = dict(self.table.select('26.1.7.5654'))
        fields.pop('build')
        # A synthetic readable/executable section covering these VAs; no FL image is needed.
        self.image = SimpleNamespace(version='26.1.7.5654', sha256='a' * 64, base=0x400000,
            pe=SimpleNamespace(OPTIONAL_HEADER=SimpleNamespace(SizeOfImage=0x2000000),
                sections=[SimpleNamespace(VirtualAddress=0, Misc_VirtualSize=0x2000000, Characteristics=0x60000000)]))
        self.review = dict(schema=1, status='reviewed', target=dict(build=self.image.version, sha256=self.image.sha256),
            wire=profiles.WIRE, reviewer='Synthetic regression fixture', fields=fields,
            fields_sha256=profiles.field_hash(fields),
            checks={k: dict(passed=True, evidence='synthetic regression') for k in profiles.CHECKS})

    def export(self, review=None):
        return profiles.export_review(review or self.review, self.image, self.table)

    def test_cli_refuses_invalid_pe_without_writing_profile(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'result.profile'
            error = io.StringIO()
            with patch('profiles.Image', side_effect=profiles.pefile.PEFormatError('invalid PE')), contextlib.redirect_stderr(error):
                result = profiles.main(['draft', str(Path(folder) / 'bad.dll'), '--output', str(output)])
            self.assertEqual(result, 2)
            self.assertIn('Profile refused:', error.getvalue())
            self.assertFalse(output.exists())

    def test_exports_all_fields_for_the_exact_target(self):
        values = dict(line.split('=', 1) for line in self.export().splitlines())
        self.assertEqual(len(values), 88)
        self.assertEqual(values['build'], self.image.version)
        self.assertEqual(values['sha256'], self.image.sha256)
        self.assertEqual(int(values['SETPROJ'], 16), 0x11d9380)

    def test_draft_and_incomplete_evidence_cannot_be_exported(self):
        bad = copy.deepcopy(self.review); bad['status'] = 'draft'
        with self.assertRaisesRegex(ValueError, 'reviewed attestation'): self.export(bad)
        for name in profiles.CHECKS:
            bad = copy.deepcopy(self.review); bad['checks'][name]['passed'] = False
            with self.assertRaisesRegex(ValueError, name): self.export(bad)
            bad['checks'][name] = dict(passed=True, evidence='')
            with self.assertRaisesRegex(ValueError, name): self.export(bad)

    def test_version_aliases_outside_family_and_overflow_are_rejected(self):
        for value in ('25', '25.2.5', '25.02.5.5319', '26.1.7.65536', '24.1.2.3', '24.2.2.4596',
                      '24.2.2.4598', '24.02.2.4597', '27.1.2.3', '26.1.7.5654\n', '../26.1.7.5654', '26.1٢.7.5654'):
            self.assertFalse(profiles.version_ok(value), value)
        self.assertTrue(profiles.version_ok('25.0.0.0'))
        self.assertTrue(profiles.version_ok('24.2.2.4597'))

    def test_exact_fl24_zero_count_keeps_schema_and_requires_runtime_evidence(self):
        self.image.version = '24.2.2.4597'
        self.review['target']['build'] = self.image.version
        self.review['fields'] = dict(self.table.select(self.image.version))
        self.review['fields'].pop('build')
        self.review['fields_sha256'] = profiles.field_hash(self.review['fields'])
        values = dict(line.split('=', 1) for line in self.export().splitlines())
        self.assertEqual(len(values), 88)
        self.assertEqual(values['MIXCNT'], '0x0')
        self.assertEqual(values['PLREFRESH'], '0xd30840')
        self.assertEqual(len(self.table.address_fields), 62)
        self.assertEqual(len(self.table.offset_fields), 21)
        self.review['checks']['runtime']['passed'] = False
        with self.assertRaisesRegex(ValueError, 'runtime'):
            self.export()

    def test_zero_count_exception_does_not_allow_other_builds_or_other_addresses(self):
        for version in ('25.2.5.5319', '26.1.7.5654', '24.2.2.4598'):
            self.image.version = version
            self.review['target']['build'] = version
            self.review['fields']['MIXCNT'] = 0
            self.review['fields_sha256'] = profiles.field_hash(self.review['fields'])
            with self.subTest(version=version), self.assertRaises(ValueError):
                self.export()
        self.image.version = '24.2.2.4597'
        self.review['target']['build'] = self.image.version
        for field in ('REC', 'PLREFRESH', 'MAINFORM'):
            bad = copy.deepcopy(self.review)
            bad['fields'][field] = 0
            bad['fields_sha256'] = profiles.field_hash(bad['fields'])
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, field):
                self.export(bad)

    def test_exact_fl24_draft_is_unattested_and_unknown_fl24_writes_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            self.image.version = '24.2.2.4597'
            output = Path(folder) / 'draft.json'
            with patch('profiles.Image', return_value=self.image), contextlib.redirect_stdout(io.StringIO()):
                result = profiles.main(['draft', str(Path(folder) / 'engine.dll'), '--output', str(output)])
            self.assertEqual(result, 0)
            draft = profiles.read_review(output)
            self.assertEqual(draft['status'], 'draft')
            self.assertEqual(draft['target']['build'], '24.2.2.4597')
            expected = dict(self.table.select('24.2.2.4597'))
            expected.pop('build')
            self.assertEqual(draft['fields'], expected)
            self.assertEqual(draft['fields_sha256'], profiles.field_hash(expected))
            self.assertTrue(all(c == {'passed': False, 'evidence': ''} for c in draft['checks'].values()))
            self.image.version = '24.2.2.4598'
            refused = Path(folder) / 'unknown.json'
            with patch('profiles.Image', return_value=self.image), contextlib.redirect_stderr(io.StringIO()):
                result = profiles.main(['draft', str(Path(folder) / 'unknown.dll'), '--output', str(refused)])
            self.assertEqual(result, 2)
            self.assertFalse(refused.exists())

    def test_engine_and_field_fingerprints_cannot_be_reused(self):
        bad = copy.deepcopy(self.review); bad['target']['sha256'] = 'b' * 64
        with self.assertRaisesRegex(ValueError, 'identity'): self.export(bad)
        bad = copy.deepcopy(self.review); bad['fields']['SETPROJ'] += 16
        with self.assertRaisesRegex(ValueError, 'changed after review'): self.export(bad)

    def test_missing_unknown_wrong_type_and_invalid_abi_are_rejected(self):
        for name, value in [('msgbox', 3), ('REC', True), ('FORM_BUSY', 0), ('PATOP_CLONE_SLOT', 0x39),
                            ('MIX_SLOTS_OFF', 0x10000)]:
            bad = copy.deepcopy(self.review); bad['fields'][name] = value
            with self.subTest(name=name), self.assertRaises(ValueError): self.export(bad)
        bad = copy.deepcopy(self.review); bad['fields'].pop('REC')
        with self.assertRaisesRegex(ValueError, 'Every address'): self.export(bad)
        bad = copy.deepcopy(self.review); bad['fields']['UNREVIEWED'] = 1
        with self.assertRaisesRegex(ValueError, 'unknown fields'): self.export(bad)

    def test_addresses_must_land_in_the_correct_section(self):
        bad = copy.deepcopy(self.review); bad['fields']['REC'] = 0x3000000
        with self.assertRaisesRegex(ValueError, 'outside'): self.export(bad)
        self.image.pe.sections[0].Characteristics = 0x40000000
        with self.assertRaisesRegex(ValueError, 'permissions'): self.export()

    def test_wire_and_reviewer_are_required(self):
        bad = copy.deepcopy(self.review); bad['wire'] = 'new-format'
        with self.assertRaisesRegex(ValueError, 'wire'): self.export(bad)
        bad = copy.deepcopy(self.review); bad['reviewer'] = ' '
        with self.assertRaisesRegex(ValueError, 'reviewer'): self.export(bad)

    def test_malformed_review_metadata_is_refused(self):
        for value in (True, 1.0, '1', None):
            bad = copy.deepcopy(self.review); bad['schema'] = value
            with self.subTest(schema=value), self.assertRaisesRegex(ValueError, 'reviewed attestation'):
                self.export(bad)
        for value in (None, [], 'passed', {'runtime': {'passed': True}}):
            bad = copy.deepcopy(self.review); bad['checks'] = value
            with self.subTest(checks=value), self.assertRaisesRegex(ValueError, 'review check'):
                self.export(bad)
        for value in (None, [], True, 'passed'):
            bad = copy.deepcopy(self.review); bad['checks']['runtime'] = value
            with self.subTest(runtime=value), self.assertRaisesRegex(ValueError, 'runtime'):
                self.export(bad)

    def test_json_review_rejects_duplicate_fields_and_nonfinite_numbers(self):
        with tempfile.TemporaryDirectory() as folder:
            review = Path(folder) / 'review.json'
            for raw in ('{"status":"draft","status":"reviewed"}',
                        '{"checks":{"runtime":{"passed":false,"passed":true}}}',
                        '{"value":NaN}', '{"value":Infinity}', '{"value":-Infinity}'):
                review.write_text(raw, encoding='utf-8')
                with self.subTest(raw=raw), self.assertRaises(ValueError):
                    profiles.read_review(review)
            review.write_text(json.dumps(self.review), encoding='utf-8')
            self.assertEqual(profiles.read_review(review), self.review)

    def test_output_created_during_validation_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'result.json'
            output.write_text('existing review', encoding='utf-8')
            with patch.object(Path, 'exists', return_value=False), patch('profiles.Image', return_value=self.image), \
                    contextlib.redirect_stderr(io.StringIO()):
                result = profiles.main(['draft', str(Path(folder) / 'engine.dll'), '--output', str(output)])
            self.assertEqual(result, 2)
            self.assertEqual(output.read_text(encoding='utf-8'), 'existing review')


if __name__ == '__main__':
    unittest.main()
