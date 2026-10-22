"""Immutable project and sample handoff through a user-selected synced folder."""
import argparse
import hashlib
import json
import ntpath
import os
from pathlib import Path
import re
import stat
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'dev'))
from flp import event_spans, rewrite_payloads

SCHEMA = 'flim-file-snapshot-v1'
MAX_PROJECT = 64 * 1024 * 1024
MAX_ASSET = 1024 * 1024 * 1024
MAX_TOTAL = 8 * 1024 * 1024 * 1024
MAX_MANIFEST = 2 * 1024 * 1024
MAX_ASSETS = 4096
MAX_EVENTS = 1000000
HASH = re.compile(r'[0-9a-f]{64}\Z')
BUILD = re.compile(r'(?:25|26)\.\d{1,5}\.\d{1,5}\.\d{1,5}\Z')
SNAPSHOT = re.compile(r'([0-9a-f]{64})\.json\Z')


class CompanionError(ValueError):
    pass


class Pending(CompanionError):
    """A transfer has not yet delivered an object or a valid complete object."""


class SourceChanged(CompanionError):
    pass


def fail(message):
    raise CompanionError(message)


def valid_build(value):
    if not isinstance(value, str) or not BUILD.fullmatch(value):
        fail('Expected an exact FL 25/26 build such as 26.1.7.5654')
    if any(int(p) > 65535 for p in value.split('.')):
        fail('FL build component exceeds 65535')
    return value


def check_path(path, directory=False, create=False):
    """Reject symlinks and Windows reparse points in every existing component."""
    path = Path(os.path.abspath(path))
    for component in reversed((path, *path.parents)):
        try:
            info = component.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            fail(f'Symlink or reparse point is not allowed: {component}')
    if create:
        path.mkdir(parents=True, exist_ok=True)
        return check_path(path, directory=True)
    if path.exists() and (directory != path.is_dir()):
        fail(f'Expected {"directory" if directory else "regular file"}: {path}')
    if path.exists() and not directory and not path.is_file():
        fail(f'Expected regular file: {path}')
    return path


def within(path, root):
    return path == root or root in path.parents


def separate(share, other, label):
    if within(other, share) or within(share, other):
        fail(f'{label} must be separate from the shared folder')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('utf-8')


def read_bounded(path, maximum):
    path = check_path(path)
    try:
        with path.open('rb') as stream:
            data = stream.read(maximum + 1)
    except FileNotFoundError as exc:
        raise Pending(f'Waiting for file: {path}') from exc
    if len(data) > maximum:
        fail(f'File exceeds {maximum} bytes: {path}')
    return data


def fingerprint(info):
    # Windows fstat/stat disagree on legacy ctime (change time versus birth time).
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns


def copy_hash(source, target, maximum):
    source = check_path(source)
    hasher = hashlib.sha256()
    size = 0
    try:
        with source.open('rb') as inp, Path(target).open('xb') as out:
            before = os.fstat(inp.fileno())
            if not stat.S_ISREG(before.st_mode) or before.st_size > maximum:
                fail(f'Not a regular file within the size limit: {source}')
            while True:
                chunk = inp.read(1024 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                if size > maximum:
                    fail(f'File grew past the size limit while reading: {source}')
                hasher.update(chunk)
                out.write(chunk)
            out.flush()
            os.fsync(out.fileno())
            after = os.fstat(inp.fileno())
        if fingerprint(before) != fingerprint(after) or fingerprint(after) != fingerprint(source.stat()):
            raise SourceChanged(f'File changed while reading; save/close it and publish again: {source}')
    except FileNotFoundError as exc:
        raise Pending(f'Waiting for file: {source}') from exc
    return hasher.hexdigest(), size


def verify_file(path, expected, maximum):
    path = check_path(path)
    hasher = hashlib.sha256()
    size = 0
    try:
        with path.open('rb') as stream:
            before = os.fstat(stream.fileno())
            if before.st_size != expected['size'] or before.st_size > maximum:
                raise Pending(f'Incomplete or wrong-size object: {path}')
            while True:
                data = stream.read(1024 * 1024)
                if not data:
                    break
                size += len(data)
                if size > maximum:
                    raise Pending(f'Object exceeds size limit: {path}')
                hasher.update(data)
            after = os.fstat(stream.fileno())
        if fingerprint(before) != fingerprint(after) or fingerprint(after) != fingerprint(path.stat()):
            raise Pending(f'Object changed during verification: {path}')
    except FileNotFoundError as exc:
        raise Pending(f'Waiting for object: {path}') from exc
    if hasher.hexdigest() != expected['sha256'] or size != expected['size']:
        raise Pending(f'Object SHA256 mismatch; wait for transfer or republish: {path}')


def verify_local(path, expected, maximum, label):
    try:
        verify_file(path, expected, maximum)
    except Pending as exc:
        raise CompanionError(f'{label} conflicts with the snapshot; preserve your edits and move it aside before preparing again: {path}') from exc


def object_path(share, sha):
    if not isinstance(sha, str) or not HASH.fullmatch(sha):
        fail('Invalid SHA256 in manifest')
    return check_path(share / 'objects' / 'sha256' / sha[:2] / sha)


def install_immutable(temp, dest, expected, maximum):
    check_path(dest.parent, directory=True, create=True)
    check_path(dest)
    try:
        # Only our private copy is linked; the user's source file is never linked.
        os.link(temp, dest)
    except FileExistsError:
        verify_file(dest, expected, maximum)
    except OSError as exc:
        fail(f'Cannot atomically install {dest}; use a local filesystem supporting hard links: {exc}')


def store_source(share, source, scratch, maximum):
    temp = Path(scratch) / f'object-{os.urandom(12).hex()}'
    try:
        sha, size = copy_hash(source, temp, maximum)
        item = {'sha256': sha, 'size': size}
        install_immutable(temp, object_path(share, sha), item, maximum)
        return item
    finally:
        temp.unlink(missing_ok=True)


def parse_project(data):
    spans = []
    versions = set()
    try:
        for item in event_spans(data):
            if len(spans) >= MAX_EVENTS:
                fail('Project has too many FLP events')
            spans.append(item)
            event, _, start, end = item
            if event == 199:
                raw = data[start:end]
                if raw.endswith(b'\0'):
                    raw = raw[:-1]
                try:
                    versions.add(valid_build(raw.decode('ascii')))
                except (UnicodeError, ValueError) as exc:
                    raise CompanionError('Invalid FL project build event') from exc
    except CompanionError:
        raise
    except ValueError as exc:
        raise CompanionError(f'Invalid FLP event stream: {exc}') from exc
    if len(versions) != 1:
        fail('Project must contain one consistent exact FL build')
    return versions.pop(), spans


def sample_paths(data, spans):
    paths = {}
    for ordinal, (event, _, start, end) in enumerate(spans):
        if event != 196:
            continue
        raw = data[start:end]
        if len(raw) > 32768 or len(raw) % 2:
            fail('Invalid UTF-16 sample path event')
        try:
            value = raw.decode('utf-16-le')
        except UnicodeError as exc:
            raise CompanionError('Invalid UTF-16 sample path event') from exc
        if value.endswith('\0'):
            value = value[:-1]
        if '\0' in value or any(ord(c) < 32 for c in value):
            fail('Sample path contains control characters')
        if value:
            paths[ordinal] = value
    return paths


def safe_name(value):
    name = re.split(r'[\\/]', value)[-1] or 'sample.wav'
    name = ''.join('_' if ord(c) < 32 or c in '\\/:*?"<>|' else c for c in name)
    if name.endswith((' ', '.')) or name.split('.')[0].upper() in {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)), *(f'LPT{i}' for i in range(1, 10))}:
        fail('Unsupported Windows reserved basename or trailing dot/space; rename the source asset first')
    if len(name.encode('utf-16-le')) > 240:
        fail('Basename exceeds 120 UTF-16 units; rename the source asset first')
    return name


def shared_data_root(explicit):
    if explicit:
        return check_path(explicit, directory=True)
    if os.name == 'nt':
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Image-Line\Shared\Paths') as key:
                value, _ = winreg.QueryValueEx(key, 'Shared data')
                return check_path(os.path.expandvars(value), directory=True)
        except OSError:
            return check_path(Path(os.environ['USERPROFILE']) / 'Documents' / 'Image-Line', directory=True)
    fail('FL path macro requires --shared-data (Image-Line shared data directory)')


def resolve_sample(value, project, shared_data):
    for token, tail in (('%ILSharedData%', ''), ('%FLStudioUserData%', 'FL Studio')):
        if value.lower().startswith(token.lower()):
            root = shared_data_root(shared_data)
            if tail:
                root /= tail
            suffix = value[len(token):].lstrip('\\/').replace('\\', '/')
            if any(part == '..' for part in suffix.split('/')):
                fail('Parent traversal in FL macro sample path')
            return check_path(root / suffix)
    if value.startswith('%'):
        fail(f'Unsupported FL path macro: {value}')
    if ntpath.splitdrive(value)[0] and not ntpath.isabs(value):
        fail(f'Ambiguous drive-relative sample path; resave with an absolute path: {value}')
    if os.name != 'nt' and (ntpath.isabs(value) and not value.startswith('/')):
        fail(f'Windows sample path requires publishing on Windows: {value}')
    path = Path(value.replace('\\', os.sep))
    if not path.is_absolute():
        path = project.parent / path
    return check_path(path)


def publish(share, project, build=None, shared_data=None, extra_samples=(), skip_missing=False):
    share = check_path(share, directory=True, create=True)
    project = check_path(project)
    if project.suffix.lower() != '.flp':
        fail('Publish a saved .flp project, not a ZIP or an active autosave folder')
    if within(project, share):
        fail('Source project must be outside the shared folder')
    temp_root = check_path(share / '.flim-staging', directory=True, create=True)
    with tempfile.TemporaryDirectory(prefix='publish-', dir=temp_root) as scratch:
        project_item = store_source(share, project, scratch, MAX_PROJECT)
        data = read_bounded(object_path(share, project_item['sha256']), MAX_PROJECT)
        actual_build, spans = parse_project(data)
        if build and valid_build(build) != actual_build:
            fail(f'Project build {actual_build} differs from requested build {build}')
        paths = sample_paths(data, spans)
        if len(paths) + len(extra_samples) > MAX_ASSETS:
            fail(f'At most {MAX_ASSETS} sample references are supported')
        assets, skipped, total = [], [], project_item['size']
        grouped = {}
        for ordinal, value in paths.items():
            grouped.setdefault(value, []).append(ordinal)
        for value, ordinals in grouped.items():
            try:
                source = resolve_sample(value, project, shared_data)
                item = store_source(share, source, scratch, MAX_ASSET)
            except (CompanionError, OSError) as exc:
                if not skip_missing:
                    raise CompanionError(f'Cannot publish sample {value}: {exc}. Fix it or explicitly use --skip-missing.') from exc
                skipped.extend({'event': ordinal, 'reason': str(exc)[:512]} for ordinal in ordinals)
                continue
            total += item['size']
            if total > MAX_TOTAL:
                fail('Snapshot exceeds the 8 GiB total limit')
            assets.append(dict(item, name=safe_name(value), events=ordinals))
        for source in extra_samples:
            item = store_source(share, source, scratch, MAX_ASSET)
            total += item['size']
            if total > MAX_TOTAL:
                fail('Snapshot exceeds the 8 GiB total limit')
            assets.append(dict(item, name=safe_name(str(source)), events=[]))
        manifest = {'schema': SCHEMA, 'project': dict(project_item, name=safe_name(project.name), build=actual_build),
                    'assets': assets, 'skipped': skipped, 'coverage': 'standard-event196-and-explicit-extras'}
        raw = canonical(manifest)
        if len(raw) > MAX_MANIFEST:
            fail('Snapshot manifest exceeds 2 MiB')
        snapshot = digest(raw)
        destination = share / 'snapshots' / (snapshot + '.json')
        temp = Path(scratch) / 'manifest'
        with temp.open('xb') as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        install_immutable(temp, destination, {'sha256': snapshot, 'size': len(raw)}, MAX_MANIFEST)
    return {'status': 'published-incomplete' if skipped else 'published', 'snapshot': snapshot,
            'manifest': str(destination), 'build': actual_build, 'assets': len(assets), 'skipped': len(skipped),
            'coverage': manifest['coverage']}


def validate_item(item, maximum):
    if not isinstance(item, dict) or not isinstance(item.get('sha256'), str) or not HASH.fullmatch(item['sha256']):
        fail('Invalid object SHA256')
    if type(item.get('size')) is not int or not 0 <= item['size'] <= maximum:
        fail('Invalid object size')


def load_manifest(share, snapshot):
    if not isinstance(snapshot, str) or not HASH.fullmatch(snapshot):
        fail('Snapshot must be a 64-character lowercase SHA256 ID from publish/status')
    path = check_path(share / 'snapshots' / (snapshot + '.json'))
    raw = read_bounded(path, MAX_MANIFEST)
    if digest(raw) != snapshot:
        fail('Manifest filename SHA256 does not match its contents')
    try:
        manifest = json.loads(raw)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise CompanionError('Malformed snapshot JSON') from exc
    if not isinstance(manifest, dict) or manifest.get('schema') != SCHEMA:
        fail('Unsupported snapshot schema')
    project = manifest.get('project')
    validate_item(project, MAX_PROJECT)
    valid_build(project.get('build'))
    name = project.get('name')
    if not isinstance(name, str) or len(name) > 120 or safe_name(name) != name or not name.lower().endswith('.flp'):
        fail('Invalid project name')
    assets, skipped = manifest.get('assets'), manifest.get('skipped')
    if not isinstance(assets, list) or not isinstance(skipped, list) or len(assets) + len(skipped) > MAX_ASSETS:
        fail('Invalid or excessive sample list')
    if manifest.get('coverage') != 'standard-event196-and-explicit-extras':
        fail('Unsupported snapshot coverage')
    total, assigned = project['size'], set()
    for asset in assets:
        validate_item(asset, MAX_ASSET)
        total += asset['size']
        name, indexes = asset.get('name'), asset.get('events')
        if not isinstance(name, str) or len(name) > 120 or safe_name(name) != name:
            fail('Invalid sample basename')
        if not isinstance(indexes, list) or len(indexes) > MAX_ASSETS:
            fail('Invalid sample event indexes')
        for index in indexes:
            if type(index) is not int or not 0 <= index < MAX_EVENTS or index in assigned:
                fail('Duplicate or invalid sample event index')
            assigned.add(index)
    for skip in skipped:
        if not isinstance(skip, dict) or type(skip.get('event')) is not int or not 0 <= skip['event'] < MAX_EVENTS or skip['event'] in assigned:
            fail('Invalid skipped sample event')
        if not isinstance(skip.get('reason'), str) or len(skip['reason']) > 512:
            fail('Invalid skipped sample reason')
        assigned.add(skip['event'])
    if total > MAX_TOTAL or len(assigned) > MAX_ASSETS:
        fail('Snapshot exceeds total size/reference limit')
    return manifest


def cache_root_default():
    profile = os.environ.get('USERPROFILE')
    if not profile:
        fail('Use --cache-root; automatic engine cache location requires USERPROFILE on Windows')
    return Path(profile) / 'Documents' / 'flim' / 'samples'


def stage_verified(share, item, scratch, maximum):
    source = object_path(share, item['sha256'])
    target = Path(scratch) / ('verified-' + os.urandom(12).hex())
    try:
        sha, size = copy_hash(source, target, maximum)
    except SourceChanged as exc:
        raise Pending(f'Object changed while arriving; waiting for a stable transfer: {source}') from exc
    if sha != item['sha256'] or size != item['size']:
        raise Pending(f'Object is incomplete or corrupt: {source}')
    return target


def prepare(share, snapshot, local_root, build, cache_root=None, allow_incomplete=False):
    share = check_path(share, directory=True)
    build = valid_build(build)
    local_root = check_path(local_root, directory=True)
    cache_root = check_path(cache_root if cache_root is not None else cache_root_default(), directory=True)
    separate(share, local_root, 'Local project directory')
    separate(share, cache_root, 'Sample cache')
    manifest = load_manifest(share, snapshot)
    project = manifest['project']
    if build != project['build']:
        fail(f'Project requires FL {project["build"]}; your selected build is {build}. Use matching FL builds for this session.')
    if manifest['skipped'] and not allow_incomplete:
        fail('Snapshot has missing/unsupported sample references; republish complete or explicitly use --allow-incomplete')
    check_path(local_root, directory=True, create=True)
    with tempfile.TemporaryDirectory(prefix='.flim-prepare-', dir=local_root) as scratch:
        project_copy = stage_verified(share, project, scratch, MAX_PROJECT)
        data = read_bounded(project_copy, MAX_PROJECT)
        actual_build, spans = parse_project(data)
        if actual_build != build:
            fail('Verified project build differs from the manifest/selected FL build')
        paths = sample_paths(data, spans)
        assigned = {skip['event'] for skip in manifest['skipped']}
        rewritten = {}
        staged_assets = []
        for asset in manifest['assets']:
            for ordinal in asset['events']:
                if ordinal not in paths or safe_name(paths[ordinal]) != asset['name']:
                    fail('Manifest sample reference does not match the verified project')
                assigned.add(ordinal)
            target = cache_root / asset['sha256'][:16] / asset['name']
            check_path(target)
            if os.name == 'nt' and len(str(target).encode('utf-16-le')) >= 518:
                fail('Sample cache path exceeds the native client MAX_PATH limit; use a shorter --cache-root')
            staged = stage_verified(share, asset, scratch, MAX_ASSET)
            if target.exists():
                verify_local(target, asset, MAX_ASSET, 'Local sample cache file')
            staged_assets.append((staged, target, asset))
            for ordinal in asset['events']:
                rewritten[ordinal] = (str(target) + '\0').encode('utf-16-le')
        if assigned != set(paths):
            fail('Manifest does not account for every nonempty standard sample path event')
        staged_data = rewrite_payloads(data, rewritten)
        if len(staged_data) > MAX_PROJECT:
            fail('Rewritten project exceeds the 64 MiB limit')
        # No visible cache/project is installed until all arriving objects verify.
        for staged, target, asset in staged_assets:
            install_immutable(staged, target, asset, MAX_ASSET)
        final_parent = check_path(local_root / 'snapshots', directory=True, create=True)
        final = check_path(final_parent / snapshot, directory=True)
        project_path = final / 'project.flp'
        if final.exists():
            verify_local(project_path, {'sha256': digest(staged_data), 'size': len(staged_data)}, MAX_PROJECT, 'Prepared project')
            reused = True
        else:
            private = Path(scratch) / 'ready'
            private.mkdir()
            (private / 'project.flp').write_bytes(staged_data)
            (private / 'snapshot.json').write_bytes(canonical({'snapshot': snapshot, 'build': build, 'source_name': project['name']}))
            try:
                os.rename(private, final)
                reused = False
            except OSError:
                if not final.is_dir():
                    raise
                verify_local(project_path, {'sha256': digest(staged_data), 'size': len(staged_data)}, MAX_PROJECT, 'Prepared project')
                reused = True
    return {'status': 'prepared-incomplete' if manifest['skipped'] else 'prepared', 'snapshot': snapshot,
            'project': str(project_path), 'build': build, 'assets': len(manifest['assets']),
            'skipped': len(manifest['skipped']), 'reused': reused, 'opens_automatically': False}


def scan_snapshots(share):
    share = check_path(share, directory=True)
    directory = check_path(share / 'snapshots', directory=True)
    if not directory.exists():
        return []
    found = []
    for entry in directory.iterdir():
        match = SNAPSHOT.fullmatch(entry.name)
        if match:
            check_path(entry)
            found.append(match.group(1))
            if len(found) > 10000:
                fail('More than 10000 snapshots; archive old snapshots outside this share')
    return sorted(found)


def status(share):
    result = []
    for snapshot in scan_snapshots(share):
        try:
            manifest = load_manifest(Path(share), snapshot)
            result.append({'snapshot': snapshot, 'build': manifest['project']['build'],
                           'project': manifest['project']['name'], 'assets': len(manifest['assets']),
                           'skipped': len(manifest['skipped']), 'status': 'manifest-present-objects-unverified'})
        except (OSError, ValueError) as exc:
            result.append({'snapshot': snapshot, 'status': 'invalid', 'error': str(exc)})
    return {'status': 'local-inventory', 'snapshots': result, 'network_checked': False}


def watch_scan(share, local_root, build, cache_root, allow_incomplete, completed):
    results = []
    for snapshot in scan_snapshots(share):
        if snapshot in completed:
            continue
        try:
            result = prepare(share, snapshot, local_root, build, cache_root, allow_incomplete)
            completed.add(snapshot)
        except Pending as exc:
            result = {'status': 'waiting', 'snapshot': snapshot, 'error': str(exc)}
        except (OSError, ValueError) as exc:
            result = {'status': 'rejected', 'snapshot': snapshot, 'error': str(exc)}
        results.append(result)
    return results


def emit(value, as_json=True):
    if as_json:
        print(json.dumps(value, ensure_ascii=True), flush=True)
    else:
        for key, item in value.items():
            print(f'{key}: {json.dumps(item, ensure_ascii=True) if isinstance(item, (list, dict)) else item}', flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--text', action='store_true', help='Human-readable output instead of JSON lines')
    sub = parser.add_subparsers(dest='command', required=True)
    pub = sub.add_parser('publish', help='Copy a saved FLP and samples into an immutable snapshot')
    pub.add_argument('--share', required=True, type=Path)
    pub.add_argument('--project', required=True, type=Path)
    pub.add_argument('--build', help='Require this exact project build')
    pub.add_argument('--shared-data', type=Path, help='Image-Line shared data root, containing FL Studio')
    pub.add_argument('--extra-sample', type=Path, action='append', default=[])
    pub.add_argument('--skip-missing', action='store_true', help='Explicitly publish an incomplete snapshot')
    for name in ('prepare', 'watch'):
        command = sub.add_parser(name)
        command.add_argument('--share', required=True, type=Path)
        command.add_argument('--local-root', required=True, type=Path)
        command.add_argument('--cache-root', type=Path, help='Defaults to the Windows native flim sample cache')
        command.add_argument('--build', required=True, help='Exact build of the FL installation you will open')
        command.add_argument('--allow-incomplete', action='store_true')
        if name == 'prepare':
            command.add_argument('--snapshot', required=True)
        else:
            command.add_argument('--interval', type=float, default=3)
            command.add_argument('--once', action='store_true', help='One scan; nonzero if a snapshot waits or is rejected')
    inventory = sub.add_parser('status', help='List local manifests; does not query Syncthing or verify objects')
    inventory.add_argument('--share', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == 'publish':
            result = publish(args.share, args.project, args.build, args.shared_data, args.extra_sample, args.skip_missing)
        elif args.command == 'prepare':
            result = prepare(args.share, args.snapshot, args.local_root, args.build, args.cache_root, args.allow_incomplete)
        elif args.command == 'status':
            result = status(args.share)
            emit(result, not args.text)
            return int(any(s['status'] == 'invalid' for s in result['snapshots']))
        else:
            if not 0.2 <= args.interval <= 3600:
                fail('--interval must be between 0.2 and 3600 seconds')
            completed, last = set(), {}
            while True:
                results = watch_scan(args.share, args.local_root, args.build, args.cache_root, args.allow_incomplete, completed)
                for result in results:
                    if last.get(result['snapshot']) != result:
                        emit(result, not args.text)
                        last[result['snapshot']] = result
                if args.once:
                    return int(any(r['status'] in ('waiting', 'rejected') for r in results))
                time.sleep(args.interval)
        emit(result, not args.text)
        return 0
    except (OSError, ValueError) as exc:
        emit({'status': 'waiting' if isinstance(exc, Pending) else 'error', 'error': str(exc)}, not args.text)
        return 2 if isinstance(exc, Pending) else 1
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
