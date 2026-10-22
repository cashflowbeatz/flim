"""Effect-state comparisons using synthetic FST data and no FL/probe imports."""
import importlib.util
from pathlib import Path
import struct
import sys
import types
import unittest
from unittest.mock import patch


def load_state_comparator():
    stubs = {name: types.ModuleType(name) for name in ('fl', 'mk', 'flbuild')}
    stubs['mk'].A = stubs['mk'].B = 0

    def reject_runtime_access(name):
        raise AssertionError(f'offline state tests attempted runtime access: {name}')

    for stub in stubs.values():
        stub.__getattr__ = reject_runtime_access
    spec = importlib.util.spec_from_file_location('_offline_feature_state', Path(__file__).with_name('feature_test.py'))
    module = importlib.util.module_from_spec(spec)
    with patch.dict(sys.modules, stubs):
        spec.loader.exec_module(module)
    return module.shared_effect_state


shared_effect_state = load_state_comparator()


def varint(value):
    encoded = bytearray()
    while value >= 0x80:
        encoded.append((value & 0x7f) | 0x80)
        value >>= 7
    encoded.append(value)
    return bytes(encoded)


def fst(*events):
    payload = b''.join(bytes([event]) + (varint(len(value)) if event >= 192 else b'') + value
                       for event, value in events)
    return b'FLhd' + struct.pack('<IHHH', 6, 0, 1, 96) + b'FLdt' + struct.pack('<I', len(payload)) + payload


def wrapper(flags=0xb0):
    value = bytearray(range(24))
    value[16] = flags
    return bytes(value)


def preset(flags=0xb0, plugin=b'plugin-parameters\0\x01\x02', wrapper_value=None):
    return fst((212, wrapper(flags) if wrapper_value is None else wrapper_value), (213, plugin))


class EffectStateTests(unittest.TestCase):
    def test_import_has_no_runtime_dependencies(self):
        self.assertEqual(shared_effect_state.__globals__['A'], 0)
        self.assertEqual(shared_effect_state.__globals__['B'], 0)
        for name in ('fl', 'mk', 'flbuild'):
            self.assertIsInstance(shared_effect_state.__globals__[name], types.ModuleType)
            self.assertNotIn('__file__', shared_effect_state.__globals__[name].__dict__)

    def test_editor_visibility_bit_is_the_only_excluded_flag(self):
        for flags in (0, 2, 0x80, 0xb0, 0xfe):
            with self.subTest(flags=flags):
                self.assertEqual(shared_effect_state(preset(flags)), shared_effect_state(preset(flags | 1)))

    def test_all_other_flag_bits_remain_significant(self):
        baseline = shared_effect_state(preset(0))
        for bit in range(1, 8):
            with self.subTest(bit=bit):
                self.assertNotEqual(baseline, shared_effect_state(preset(1 << bit)))
        for bit in range(8):
            other = bytearray(wrapper(0))
            other[17] ^= 1 << bit
            with self.subTest(second_flag_byte_bit=bit):
                self.assertNotEqual(baseline, shared_effect_state(preset(wrapper_value=bytes(other))))

    def test_other_wrapper_bytes_remain_significant(self):
        baseline = shared_effect_state(preset(0))
        for index in (0, 15, 18, 23):
            other = bytearray(wrapper(0))
            other[index] ^= 1
            with self.subTest(index=index):
                self.assertNotEqual(baseline, shared_effect_state(preset(wrapper_value=bytes(other))))

    def test_plugin_payload_differences_remain_significant(self):
        self.assertNotEqual(shared_effect_state(preset(plugin=b'\x00\x01')),
                            shared_effect_state(preset(plugin=b'\x00\x02')))
        self.assertNotEqual(shared_effect_state(preset(plugin=b'\x00\x01')),
                            shared_effect_state(preset(plugin=b'\x00\x01\x00')))

    def test_other_events_and_event_order_are_preserved(self):
        first = fst((18, b'\x07'), (212, wrapper()), (213, b'state'))
        second = fst((18, b'\x08'), (212, wrapper()), (213, b'state'))
        reversed_order = fst((18, b'\x07'), (213, b'state'), (212, wrapper()))
        self.assertEqual(shared_effect_state(first)[0], (18, b'\x07'))
        self.assertNotEqual(shared_effect_state(first), shared_effect_state(second))
        self.assertNotEqual(shared_effect_state(first), shared_effect_state(reversed_order))

    def test_normalization_does_not_mutate_input(self):
        source = bytearray(preset(0xb1))
        original = bytes(source)
        shared_effect_state(source)
        self.assertEqual(bytes(source), original)

    def test_escaped_extended_event_is_preserved_alongside_effect_data(self):
        base = preset()
        extended = b'\xac' + struct.pack('<I', 0xc0000101) + varint(4) + b'head'
        payload = extended + base[22:]
        data = base[:18] + struct.pack('<I', len(payload)) + payload
        normalized = shared_effect_state(data)
        self.assertEqual(normalized[0], (0xc0000101, b'head'))
        self.assertEqual(normalized[1:], shared_effect_state(base))

    def test_missing_saved_file_or_required_events_is_rejected(self):
        for source in (None, b'', fst(), fst((212, wrapper())), fst((213, b'plugin'))):
            with self.subTest(source=source):
                with self.assertRaises(ValueError):
                    shared_effect_state(source)

    def test_short_wrapper_is_rejected(self):
        for length in (0, 16, 17):
            with self.subTest(length=length):
                with self.assertRaisesRegex(ValueError, 'Truncated effect wrapper'):
                    shared_effect_state(fst((212, b'\0' * length), (213, b'plugin')))

    def test_truncated_plugin_payload_is_rejected(self):
        complete = preset(plugin=b'12345678')
        with self.assertRaises(ValueError):
            shared_effect_state(complete[:-1])

    def test_truncated_declared_wrapper_is_rejected(self):
        complete = fst((213, b'plugin'), (212, wrapper()))
        with self.assertRaises(ValueError):
            shared_effect_state(complete[:-1])

    def test_unterminated_event_length_is_rejected(self):
        data = preset() + bytes([213, 0x80])
        data = data[:18] + struct.pack('<I', len(data) - 22) + data[22:]
        with self.assertRaises(ValueError):
            shared_effect_state(data)


if __name__ == '__main__':
    unittest.main()
