import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))
import companion as c


def variable(event, value):
    count = len(value)
    length = bytearray()
    while count >= 128:
        length.append((count & 127) | 128)
        count >>= 7
    length.append(count)
    event_id = bytes([event]) if event <= 255 else b'\xac' + struct.pack('<I', event)
    return event_id + length + value


def image(paths=(), build='26.1.7.5654', trailer=b'untouched trailer'):
    stream = variable(0xc0000101, b'creator\0') + variable(199, build.encode('ascii') + b'\0')
    for path in paths:
        stream += variable(196, (str(path) + '\0').encode('utf-16-le'))
    stream += b'\xac\x01\x01\x00\x80\x11\x22\x33\x44'
    return b'FLhd\x06\0\0\0' + b'\0' * 6 + b'FLdt' + struct.pack('<I', len(stream)) + stream + trailer


class CompanionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='flim-companion-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.share = self.root / 'share'
        self.local = self.root / 'local'
        self.cache = self.root / 'cache'
        self.sample = self.root / 'kick.wav'
        self.sample.write_bytes(b'RIFF' + b'sample' * 42)
        self.project = self.root / 'source.flp'
        self.project.write_bytes(image([self.sample]))

    def publish(self, **kwargs):
        return c.publish(self.share, self.project, **kwargs)

    def prepare(self, result, **kwargs):
        return c.prepare(self.share, result['snapshot'], self.local, kwargs.pop('build', '26.1.7.5654'), self.cache, **kwargs)

    def manifest(self, result):
        return c.load_manifest(self.share, result['snapshot'])

    def replace_manifest(self, result, mutate):
        manifest = self.manifest(result)
        mutate(manifest)
        raw = c.canonical(manifest)
        snapshot = c.digest(raw)
        (self.share / 'snapshots' / (snapshot + '.json')).write_bytes(raw)
        return dict(result, snapshot=snapshot)

    def test_publish_prepare_preserves_source_and_rewrites_only_sample_path(self):
        original = self.project.read_bytes()
        result = self.publish()
        self.assertEqual(result['status'], 'published')
        ready = self.prepare(result)
        staged = Path(ready['project']).read_bytes()
        build, spans = c.parse_project(staged)
        self.assertEqual(build, '26.1.7.5654')
        path = next(iter(c.sample_paths(staged, spans).values()))
        expected = self.cache / c.digest(self.sample.read_bytes())[:16] / self.sample.name
        self.assertEqual(Path(path), expected)
        self.assertEqual(expected.read_bytes(), self.sample.read_bytes())
        self.assertTrue(staged.endswith(b'untouched trailer'))
        before_events = [(event, original[start:end]) for event, _, start, end in c.event_spans(original) if event != 196]
        after_events = [(event, staged[start:end]) for event, _, start, end in c.event_spans(staged) if event != 196]
        self.assertEqual(before_events, after_events)
        self.assertEqual(self.project.read_bytes(), original)
        self.assertFalse(ready['opens_automatically'])

    def test_publish_is_immutable_and_deduplicated(self):
        first = self.publish()
        manifest = self.manifest(first)
        obj = c.object_path(self.share, manifest['assets'][0]['sha256'])
        self.assertNotEqual(obj.stat().st_ino, self.sample.stat().st_ino)
        self.assertEqual(first, self.publish())
        self.sample.write_bytes(b'new saved audio')
        second = self.publish()
        self.assertNotEqual(first['snapshot'], second['snapshot'])
        self.assertEqual(obj.read_bytes(), b'RIFF' + b'sample' * 42)

    def test_manifest_is_published_only_after_objects(self):
        original_install = c.install_immutable
        installed = []
        def record(temp, dest, expected, maximum):
            if dest.suffix == '.json':
                manifest = json.loads(Path(temp).read_bytes())
                for item in [manifest['project'], *manifest['assets']]:
                    c.verify_file(c.object_path(self.share, item['sha256']), item, c.MAX_ASSET)
                installed.append('manifest')
            else:
                installed.append('object')
            return original_install(temp, dest, expected, maximum)
        with mock.patch.object(c, 'install_immutable', side_effect=record):
            self.publish()
        self.assertEqual(installed, ['object', 'object', 'manifest'])

    def test_mutation_during_copy_prevents_ready_publication(self):
        original_fsync = c.os.fsync
        fired = []
        def mutate(fd):
            original_fsync(fd)
            if not fired:
                fired.append(True)
                self.project.write_bytes(image([self.sample]) + b'changed')
        with mock.patch.object(c.os, 'fsync', side_effect=mutate):
            with self.assertRaisesRegex(c.CompanionError, 'changed while reading'):
                self.publish()
        self.assertEqual(c.scan_snapshots(self.share), [])

    def test_missing_sample_blocks_manifest_unless_explicit_opt_in(self):
        self.sample.unlink()
        with self.assertRaisesRegex(c.CompanionError, 'skip-missing'):
            self.publish()
        self.assertEqual(c.scan_snapshots(self.share), [])
        result = self.publish(skip_missing=True)
        self.assertEqual(result['status'], 'published-incomplete')
        with self.assertRaisesRegex(c.CompanionError, 'allow-incomplete'):
            self.prepare(result)
        self.assertEqual(self.prepare(result, allow_incomplete=True)['status'], 'prepared-incomplete')

    def test_manifest_before_object_retries_and_never_stages_early(self):
        result = self.publish()
        manifest = self.manifest(result)
        obj = c.object_path(self.share, manifest['assets'][0]['sha256'])
        saved = obj.read_bytes()
        obj.unlink()
        completed = set()
        waiting = c.watch_scan(self.share, self.local, '26.1.7.5654', self.cache, False, completed)
        self.assertEqual(waiting[0]['status'], 'waiting')
        self.assertFalse((self.local / 'snapshots').exists())
        self.assertFalse(self.cache.exists())
        obj.write_bytes(saved)
        ready = c.watch_scan(self.share, self.local, '26.1.7.5654', self.cache, False, completed)
        self.assertEqual(ready[0]['status'], 'prepared')
        self.assertEqual(c.watch_scan(self.share, self.local, '26.1.7.5654', self.cache, False, completed), [])

    def test_corrupt_same_size_object_is_rejected(self):
        result = self.publish()
        obj = c.object_path(self.share, self.manifest(result)['assets'][0]['sha256'])
        obj.write_bytes(b'x' * obj.stat().st_size)
        with self.assertRaisesRegex(c.Pending, 'corrupt'):
            self.prepare(result)
        self.assertFalse(self.cache.exists())

    def test_receiver_object_mutation_is_retryable(self):
        result = self.publish()
        original = c.copy_hash
        def changed(source, target, maximum):
            if Path(source).is_relative_to(self.share):
                raise c.SourceChanged('arriving object changed')
            return original(source, target, maximum)
        with mock.patch.object(c, 'copy_hash', side_effect=changed):
            state = c.watch_scan(self.share, self.local, '26.1.7.5654', self.cache, False, set())
        self.assertEqual(state[0]['status'], 'waiting')
        self.assertIn('stable transfer', state[0]['error'])

    def test_no_cache_write_until_all_objects_verify(self):
        extra = self.root / 'extra.wav'
        extra.write_bytes(b'extra asset')
        result = self.publish(extra_samples=[extra])
        manifest = self.manifest(result)
        c.object_path(self.share, manifest['assets'][-1]['sha256']).unlink()
        with self.assertRaises(c.Pending):
            self.prepare(result)
        self.assertFalse(self.cache.exists())

    def test_published_project_must_have_consistent_build_and_valid_stream(self):
        for raw in (image(build='26.1.7.5654')[:-len(b'untouched trailer') - 1],
                    image(build='24.1.1.1')):
            self.project.write_bytes(raw)
            with self.subTest(raw=raw[:32]), self.assertRaises(c.CompanionError):
                self.publish()

    def test_watch_does_not_process_own_local_output_on_new_scan(self):
        result = self.publish()
        self.prepare(result)
        watched = c.watch_scan(self.share, self.local, '26.1.7.5654', self.cache, False, set())
        self.assertEqual(len(watched), 1)
        self.assertEqual(watched[0]['snapshot'], result['snapshot'])
        self.assertTrue(watched[0]['reused'])

    def test_build_mismatch_rejected_before_local_writes(self):
        result = self.publish()
        with self.assertRaisesRegex(c.CompanionError, 'requires FL'):
            self.prepare(result, build='25.2.5.5319')
        self.assertFalse(self.local.exists())
        self.assertFalse(self.cache.exists())

    def test_manifest_build_cannot_override_project_build(self):
        result = self.replace_manifest(self.publish(), lambda m: m['project'].update(build='25.2.5.5319'))
        with self.assertRaisesRegex(c.CompanionError, 'differs from the manifest'):
            self.prepare(result, build='25.2.5.5319')

    def test_25_build_is_supported(self):
        self.project.write_bytes(image([self.sample], build='25.2.5.5319'))
        result = self.publish(build='25.2.5.5319')
        self.assertEqual(self.prepare(result, build='25.2.5.5319')['build'], '25.2.5.5319')

    def test_manifest_name_hash_and_event_injection_are_rejected(self):
        result = self.publish()
        for mutation in (
            lambda m: m['assets'][0].update(name='../../escape.wav'),
            lambda m: m['assets'][0].update(sha256='../' + 'a' * 61),
            lambda m: m['assets'][0].update(name='C:\\escape.wav'),
            lambda m: m['assets'][0].update(events=[-1]),
            lambda m: m['assets'][0].update(events=[2, 2]),
            lambda m: m['assets'][0].update(events=[3]),
            lambda m: m['assets'][0].update(name='other.wav'),
            lambda m: m['assets'][0].update(size=True),
        ):
            with self.subTest(mutation=mutation):
                bad = self.replace_manifest(result, mutation)
                with self.assertRaises(c.CompanionError):
                    self.prepare(bad)

    def test_unaccounted_sample_reference_is_rejected(self):
        result = self.replace_manifest(self.publish(), lambda m: m.update(assets=[]))
        with self.assertRaisesRegex(c.CompanionError, 'every nonempty'):
            self.prepare(result)

    def test_manifest_digest_mismatch_is_rejected(self):
        result = self.publish()
        Path(result['manifest']).write_bytes(b'{}')
        with self.assertRaisesRegex(c.CompanionError, 'filename SHA256'):
            self.prepare(result)

    def test_share_objects_may_not_be_symlinks(self):
        result = self.publish()
        obj = c.object_path(self.share, self.manifest(result)['assets'][0]['sha256'])
        obj.unlink()
        try:
            obj.symlink_to(self.sample)
        except OSError as exc:
            self.skipTest(f'OS does not permit symlinks: {exc}')
        with self.assertRaisesRegex(c.CompanionError, 'Symlink'):
            self.prepare(result)

    def test_local_cache_and_projects_must_be_outside_share(self):
        result = self.publish()
        with self.assertRaisesRegex(c.CompanionError, 'separate'):
            c.prepare(self.share, result['snapshot'], self.share / 'bad', '26.1.7.5654', self.cache)
        with self.assertRaisesRegex(c.CompanionError, 'separate'):
            c.prepare(self.share, result['snapshot'], self.local, '26.1.7.5654', self.share / 'bad')

    def test_cache_hit_is_verified_and_reused(self):
        result = self.publish()
        first = self.prepare(result)
        asset = self.manifest(result)['assets'][0]
        target = self.cache / asset['sha256'][:16] / asset['name']
        before = target.stat().st_mtime_ns
        again = self.prepare(result)
        self.assertTrue(again['reused'])
        self.assertEqual(first['project'], again['project'])
        self.assertEqual(target.stat().st_mtime_ns, before)

    def test_cache_conflict_is_not_overwritten(self):
        result = self.publish()
        self.prepare(result)
        asset = self.manifest(result)['assets'][0]
        target = self.cache / asset['sha256'][:16] / asset['name']
        target.write_bytes(b'x' * asset['size'])
        with self.assertRaisesRegex(c.CompanionError, 'Local sample cache file conflicts'):
            self.prepare(result)
        self.assertEqual(target.read_bytes(), b'x' * asset['size'])

    def test_edited_local_project_is_not_overwritten(self):
        result = self.publish()
        ready = self.prepare(result)
        path = Path(ready['project'])
        path.write_bytes(b'local edits')
        with self.assertRaisesRegex(c.CompanionError, 'Prepared project conflicts'):
            self.prepare(result)
        self.assertEqual(path.read_bytes(), b'local edits')

    def test_macro_paths_and_relative_paths(self):
        shared = self.root / 'Image-Line'
        sample = shared / 'FL Studio' / 'Samples' / 'hat.wav'
        sample.parent.mkdir(parents=True)
        sample.write_bytes(b'audio')
        self.project.write_bytes(image(['%FLStudioUserData%\\Samples\\hat.wav', '%ILSharedData%\\FL Studio\\Samples\\hat.wav', 'kick.wav']))
        result = self.publish(shared_data=shared)
        self.assertEqual(result['assets'], 3)
        ready = self.prepare(result)
        data = Path(ready['project']).read_bytes()
        paths = c.sample_paths(data, c.parse_project(data)[1])
        self.assertEqual(len(paths), 3)
        self.assertTrue(all(Path(value).exists() for value in paths.values()))

    def test_unknown_macro_and_parent_macro_escape_block_publication(self):
        for path in ('%UNKNOWN%\\sample.wav', '%ILSharedData%\\..\\kick.wav', 'C:relative.wav'):
            self.project.write_bytes(image([path]))
            with self.subTest(path=path), self.assertRaises(c.CompanionError):
                self.publish(shared_data=self.root)

    def test_extra_samples_preseed_cache_without_claiming_plugin_rewrites(self):
        extra = self.root / 'plugin.wav'
        extra.write_bytes(b'plugin audio')
        result = self.publish(extra_samples=[extra])
        self.assertEqual(result['coverage'], 'standard-event196-and-explicit-extras')
        self.prepare(result)
        self.assertEqual((self.cache / c.digest(extra.read_bytes())[:16] / 'plugin.wav').read_bytes(), extra.read_bytes())

    def test_safe_cache_basename_matches_normal_engine_names(self):
        for source, expected in [('C:\\samples\\kick.wav', 'kick.wav'), ('a/b.wav', 'b.wav'),
                                 ('C:\\x\\bad?:file.wav', 'bad__file.wav')]:
            self.assertEqual(c.safe_name(source), expected)
        for source in ('NUL.wav', 'trailing.wav.', '\U0001f601' * 160 + '.wav'):
            with self.subTest(source=source), self.assertRaises(c.CompanionError):
                c.safe_name(source)

    def test_oversized_manifest_and_unsupported_build_are_bounded(self):
        result = self.publish()
        with mock.patch.object(c, 'MAX_MANIFEST', 16), self.assertRaises(c.CompanionError):
            self.prepare(result)
        for build in ('26.1.7', '27.1.1.1', '26.1.7.99999'):
            with self.subTest(build=build), self.assertRaises(c.CompanionError):
                c.valid_build(build)

    def test_status_is_explicitly_unverified_local_inventory(self):
        result = self.publish()
        state = c.status(self.share)
        self.assertFalse(state['network_checked'])
        self.assertEqual(state['snapshots'][0]['status'], 'manifest-present-objects-unverified')
        self.assertEqual(state['snapshots'][0]['snapshot'], result['snapshot'])


if __name__ == '__main__':
    unittest.main()
