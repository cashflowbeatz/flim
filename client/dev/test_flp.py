import struct
import unittest

from flp import data_bounds, events, event_spans, rewrite_payloads


def image(stream, header=b'\0' * 6, trailer=b''):
    return b'FLhd' + struct.pack('<I', len(header)) + header + b'FLdt' + struct.pack('<I', len(stream)) + stream + trailer


class FlpTests(unittest.TestCase):
    def test_legacy_sizes_and_empty_variable_event(self):
        self.assertEqual(events(image(b'\x01\x2a\x40\x01\x02\x80\x01\x02\x03\x04\xc4\0')),
                         [(1, b'*'), (64, b'\x01\x02'), (128, b'\x01\x02\x03\x04'), (196, b'')])

    def test_all_extended_sizes(self):
        values = [(0x101, b'\x01'), (0x40000102, b'\x02\x03'),
                  (0x80000103, b'\x04\x05\x06\x07'), (0xc0000101, b'creator\0')]
        raw = b''.join(b'\xac' + struct.pack('<I', e) + (bytes([len(v)]) if e >> 28 == 12 else b'') + v for e, v in values)
        self.assertEqual(events(image(raw)), values)

    def test_actual_header_shape_does_not_fabricate_event_one(self):
        stream = b'\xac\x01\x01\x00\xc0\x04F\0L\0\x25\x01\xc4\x04X\0\0\0'
        self.assertEqual(events(image(stream)), [(0xc0000101, b'F\0L\0'), (37, b'\x01'), (196, b'X\0\0\0')])

    def test_large_variable_length(self):
        self.assertEqual(events(image(b'\xd5\x81\x01' + b'x' * 129)), [(213, b'x' * 129)])

    def test_data_chunk_end_not_file_end(self):
        data = image(b'\x01\x2a', trailer=b'ignored trailing chunk bytes')
        self.assertEqual(data_bounds(data), (22, 24))
        self.assertEqual(events(data), [(1, b'*')])

    def test_header_with_embedded_fldt_does_not_redirect_parser(self):
        self.assertEqual(events(image(b'\x01\x2a', b'FLdt\0\0\0\0')), [(1, b'*')])

    def test_every_truncation_of_declared_chunk_is_rejected(self):
        data = image(b'\xac\x01\x01\x00\xc0\x04F\0L\0\xc4\0')
        for n in range(len(data)):
            with self.subTest(n=n), self.assertRaises(ValueError):
                events(data[:n])

    def test_malformed_events(self):
        for stream in (b'\xac', b'\xac\1\1\0', b'\xac\1\1\0\x10\0',
                       b'\x80\1\2\3', b'\xc4\4\0', b'\xc4\x80',
                       b'\xc4\x80\x80\x80\x80\x10', b'\xc4\x80\x80\x80\x80\x80\0'):
            with self.subTest(stream=stream.hex()), self.assertRaises(ValueError):
                events(image(stream))

    def test_invalid_chunk_headers(self):
        for data in (b'xxxx' + image(b'')[4:], image(b'', b'\0' * 5),
                     b'FLhd\xff\xff\xff\xff' + b'\0' * 20,
                     image(b'')[:14] + b'xxxx' + b'\0' * 4,
                     image(b'')[:18] + b'\xff' * 4):
            with self.subTest(data=data.hex()), self.assertRaises(ValueError):
                events(data)

    def test_rewrite_preserves_extended_ids_header_and_trailing_bytes(self):
        raw = image(b'\xac\x01\x01\x00\xc0\x04F\0L\0\xc4\x04X\0\0\0\x01\x2a',
                    header=b'custom-header', trailer=b'FLdt-not-another-event\0')
        replacement = ('longer sample path.wav\0').encode('utf-16-le')
        changed = rewrite_payloads(raw, {1: replacement})
        self.assertEqual(events(changed), [(0xc0000101, b'F\0L\0'), (196, replacement), (1, b'*')])
        self.assertEqual(raw[:21], changed[:21])
        self.assertTrue(changed.endswith(b'FLdt-not-another-event\0'))
        self.assertEqual(changed.count(b'\xac\x01\x01\x00\xc0'), 1)

    def test_rewrite_extended_variable_payload_and_length_boundaries(self):
        raw = image(b'\xac\x01\x01\x00\xc0\0\xc4\0')
        replacement = b'v' * 129
        changed = rewrite_payloads(raw, {0: replacement})
        self.assertEqual(events(changed), [(0xc0000101, replacement), (196, b'')])
        self.assertIn(b'\xac\x01\x01\x00\xc0\x81\x01', changed)
        self.assertEqual(rewrite_payloads(raw, {}), raw)

    def test_rewrite_rejects_unknown_ordinal_and_fixed_payload(self):
        raw = image(b'\x01\x2a\xc4\0')
        for replacements in ({2: b'x'}, {-1: b'x'}, {0: b'x'}, {1: 'text'}):
            with self.subTest(replacements=replacements), self.assertRaises(ValueError):
                rewrite_payloads(raw, replacements)

    def test_rewrite_validates_whole_stream_and_spans_match_payloads(self):
        raw = image(b'\xc4\x04X\0\0\0\xac\x01\x01\x00\xc0\x02Y\0')
        self.assertEqual([(event, raw[p:end]) for event, _, p, end in event_spans(raw)], events(raw))
        with self.assertRaises(ValueError):
            rewrite_payloads(image(b'\xc4\0\x80\0'), {0: b'x'})


if __name__ == '__main__':
    unittest.main()
