"""Draft and export reviewed, engine-bound compatibility profiles. Never runs FL."""
import argparse
import hashlib
import json
import pefile
from pathlib import Path
import re
import sys

from sigscan import BuildTable, Image, IMG_BASE

ROOT = Path(__file__).resolve().parents[1]
WIRE = 'flim-25-26-v1'
CHECKS = ('addresses', 'layouts', 'calling_conventions', 'wire_formats', 'runtime')


def version_ok(value):
    return (isinstance(value, str) and
            re.fullmatch(r'(?:0|[1-9][0-9]{0,4})(?:\.(?:0|[1-9][0-9]{0,4})){3}', value) is not None and
            value.split('.')[0] in ('25', '26') and all(int(p) <= 65535 for p in value.split('.')))


def field_hash(fields):
    return hashlib.sha256(json.dumps(fields, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def validate_fields(fields, table, image):
    names = table.address_fields + table.offset_fields + ['msgbox']
    if not isinstance(fields, dict) or set(fields) != set(names):
        raise ValueError('Every address, layout field and msgbox mode is required; unknown fields are rejected')
    if fields['msgbox'] not in (1, 2) or any(type(v) is not int for v in fields.values()):
        raise ValueError('Profile fields must be integers; msgbox must be 1 or 2')
    for name in table.offset_fields:
        n = fields[name]
        if not 0 < n <= 0x10000 or (name.endswith('_SLOT') and (n > 0x1000 or n % 8)):
            raise ValueError(f'Invalid layout field {name}')
    if fields['MIX_SLOTS_OFF'] >= fields['MIX_STRIDE'] or fields['MIX_ROUTE'] >= fields['MIX_STRIDE']:
        raise ValueError('Mixer fields must fit inside the mixer record')
    header = (ROOT / 'common/flim_profile.h').read_text(encoding='utf-8')
    code = {name for name, kind in re.findall(r'PF64\((\w+),\s*([01])\)', header) if kind == '1'}
    for name in table.address_fields:
        va = fields[name]
        if name == 'MSGBOX_CORE' and fields['msgbox'] == 2 and va == 0:
            continue
        rva = va - IMG_BASE
        if not 0 <= rva < image.pe.OPTIONAL_HEADER.SizeOfImage:
            raise ValueError(f'Address {name} is outside the engine image')
        flags = 0x20000000 if name in code else 0x40000000
        if not any(s.VirtualAddress <= rva < s.VirtualAddress + s.Misc_VirtualSize and
                   s.Characteristics & flags for s in image.pe.sections):
            raise ValueError(f'Address {name} has the wrong PE section permissions')


def export_review(review, image, table):
    if not isinstance(review, dict) or review.get('schema') != 1 or review.get('status') != 'reviewed':
        raise ValueError('Only a reviewed attestation can be exported; a draft/map is not a runnable profile')
    if not version_ok(image.version) or image.base != IMG_BASE:
        raise ValueError('Expected an FL 25/26 x64 engine at image base 0x400000')
    if review.get('target') != {'build': image.version, 'sha256': image.sha256} or review.get('wire') != WIRE:
        raise ValueError('The reviewed target identity or wire format does not match this engine')
    if not isinstance(review.get('reviewer'), str) or not review['reviewer'].strip():
        raise ValueError('The profile must name its reviewer')
    fields = review.get('fields')
    validate_fields(fields, table, image)
    if field_hash(fields) != review.get('fields_sha256'):
        raise ValueError('Fields changed after review; update the review and its field fingerprint')
    checks = review.get('checks', {})
    for name in CHECKS:
        check = checks.get(name, {})
        if check.get('passed') is not True or not isinstance(check.get('evidence'), str) or not check['evidence'].strip():
            raise ValueError(f'Missing successful {name} review and evidence reference')
    lines = ['schema=1', f'build={image.version}', f'sha256={image.sha256}', f'wire={WIRE}', f'msgbox={fields["msgbox"]}']
    lines += [f'{name}={hex(fields[name])}' for name in table.address_fields + table.offset_fields]
    return '\n'.join(lines) + '\n'


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['draft', 'export'])
    p.add_argument('engine', type=Path)
    p.add_argument('--review', type=Path, help='Reviewed JSON attestation (export only)')
    p.add_argument('--table', type=Path, default=ROOT / 'engine/flim_engine.c')
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args(argv)
    try:
        protected = [args.engine, args.table, ROOT / 'common/flim_profile.h']
        if args.review: protected.append(args.review)
        if args.output.resolve() in {path.resolve() for path in protected}:
            raise ValueError('Output must not overwrite an engine, source or review input')
        if args.output.exists():
            raise ValueError('Output already exists; choose a new path to preserve the previous review/profile')
        image = Image(args.engine)
        if not version_ok(image.version): raise ValueError('Only canonical FL 25/26 builds can have profiles')
        table = BuildTable(args.table, include_experimental=True)
        if args.action == 'draft':
            fields = {k: v for k, v in table.builds.get(image.version, {}).items() if k != 'build'}
            draft = dict(schema=1, status='draft', target=dict(build=image.version, sha256=image.sha256),
                         wire=WIRE, reviewer='', fields=fields, fields_sha256=field_hash(fields),
                         checks={k: dict(passed=False, evidence='') for k in CHECKS})
            args.output.write_text(json.dumps(draft, indent=2) + '\n', encoding='utf-8')
            print('Draft only. Review every field and complete the evidence before export.')
        else:
            if not args.review: raise ValueError('--review is required for export')
            content = export_review(json.loads(args.review.read_text(encoding='utf-8')), image, table)
            args.output.write_text(content, encoding='ascii', newline='\n')
            print(f'Wrote engine-bound profile for {image.version}; distribute only with its reviewed evidence.')
        return 0
    except (OSError, ValueError, TypeError, KeyError, pefile.PEFormatError) as error:
        print(f'Profile refused: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
