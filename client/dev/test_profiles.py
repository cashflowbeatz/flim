"""Profiles must remain bound to an exact reviewed field set and engine identity."""
import copy
import contextlib
import io
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
        for value in ('25', '25.2.5', '25.02.5.5319', '26.1.7.65536', '24.1.2.3', '27.1.2.3', '26.1.7.5654\n', '../26.1.7.5654', '26.1٢.7.5654'):
            self.assertFalse(profiles.version_ok(value), value)
        self.assertTrue(profiles.version_ok('25.0.0.0'))

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


if __name__ == '__main__':
    unittest.main()
