"""Offline USBPcap audit fixtures. Never opens HID or sends captured bytes."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "profile_switch_audit", ROOT / "tools/hardware/analyze_official_profile_switch.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def vendor(prefix):
    data = bytes.fromhex(prefix)
    return data + bytes(64 - len(data))


def usb(payload, endpoint=0x0D, info=0, transfer=1, stage=None,
        device=23, bus=4, irp=1):
    size = 28 if stage is not None else 27
    header = struct.pack('<HQIHBHHBBI', size, irp, 0, 9, info, bus,
                         device, endpoint, transfer, len(payload))
    return header + (bytes([stage]) if stage is not None else b'') + payload


def capture(rows, endian='<', nanos=False):
    magic = (b'\x4d\x3c\xb2\xa1' if nanos else b'\xd4\xc3\xb2\xa1')
    if endian == '>':
        magic = magic[::-1]
    header = magic + struct.pack(endian + 'HHiiII', 2, 4, 0, 0, 65535, 249)
    result = bytearray(header)
    for index, raw in enumerate(rows):
        result += struct.pack(endian + 'IIII', 1700000000,
                              index * (1000000 if nanos else 1000), len(raw), len(raw))
        result += raw
    return bytes(result)


def identity(device=23):
    descriptor = bytes.fromhex('12 01 00 02 00 00 00 40 05 0b 7e 1b 59 01 01 03 02 01')
    # Two interfaces; test an unknown OUT on MI_04 as well as magnetic MI_01.
    config = bytes.fromhex(
        '09 02 30 00 02 01 00 80 32 '
        '09 04 01 00 02 03 00 00 00 '
        '07 05 0d 03 40 00 04 07 05 85 03 40 00 01 '
        '09 04 04 00 01 03 00 00 00 07 05 0f 03 40 00 04')
    return [usb(descriptor, endpoint=0x80, transfer=2, stage=3, irp=0, device=device),
            usb(config, endpoint=0x80, transfer=2, stage=3, irp=0, device=device)]


class OfficialProfileSwitchCaptureTests(unittest.TestCase):
    def analyze(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'original.pcap'
            data = capture(rows)
            source.write_bytes(data)
            output = Path(directory) / 'decoded'
            result = AUDIT.analyze(source, output)
            timeline = json.loads((output / 'timeline.json').read_text())
            derived = (output / 'ace_hfx_all_interfaces.pcap').read_bytes()
            self.assertEqual(source.read_bytes(), data)
            return result, timeline, derived

    def test_captured_profile_and_apply_bytes(self):
        for selector in (1, 6):
            report = vendor(f'51 00 00 00 {selector:02x}')
            result = AUDIT.semantic(report)
            self.assertEqual(len(report), 64)
            self.assertEqual(result['profile_selector'], selector)
            self.assertTrue(result['reserved_zero'])
        self.assertTrue(AUDIT.semantic(vendor('50 55'))['reserved_zero'])
        malformed = bytearray(vendor('51 00 00 00 06'))
        malformed[63] = 1
        self.assertFalse(AUDIT.semantic(bytes(malformed))['reserved_zero'])

    def test_descriptor_identity_does_not_assume_old_address(self):
        result, _, _ = self.analyze(identity() + [usb(vendor('51 00 00 00 06'))])
        self.assertEqual(result['identities'][0]['address'], 23)
        self.assertEqual(result['identities'][0]['bcd_device'], '0x0159')

    def test_all_interfaces_unknown_out_and_control_preserved(self):
        rows = identity() + [usb(vendor('51 00 00 00 06')),
                             usb(vendor('aa bb'), endpoint=0x0F),
                             usb(bytes.fromhex('21 09 00 02 00 00 01 00 03'),
                                 endpoint=0, transfer=2, stage=0)]
        result, timeline, derived = self.analyze(rows)
        self.assertEqual(result['actual_target_out_count'], 3)
        self.assertEqual(result['opcode_counts']['aa bb'], 1)
        self.assertEqual(result['opcode_counts']['21 09'], 1)
        unknown = next(x for x in timeline if x['payload_hex'].startswith('aa bb'))
        self.assertEqual(unknown['interface'], 4)
        self.assertEqual(unknown['decoded']['evidence'], 'NOT VERIFIED')
        self.assertEqual(derived, capture(rows))

    def test_identical_echo_and_different_query_reply(self):
        select, apply, query = vendor('51 00 00 00 06'), vendor('50 55'), vendor('12 00')
        rows = identity() + [usb(select), usb(select, endpoint=0x85, info=1),
                             usb(query), usb(vendor('12 00 00 00 59 00 01 00 06 04 06'),
                                             endpoint=0x85, info=1),
                             usb(apply), usb(apply, endpoint=0x85, info=1)]
        result, timeline, _ = self.analyze(rows)
        group = result['transition_groups'][0]
        self.assertEqual(group['commands'][0]['identical_echo']['frame'], 4)
        self.assertIsNone(group['commands'][1]['identical_echo'])
        self.assertTrue(group['apply_observed'])
        self.assertEqual(group['select_to_apply_echo_ms'], 5)
        self.assertEqual(len([x for x in timeline if x['direction'] == 'IN']), 5)

    def test_selection_without_apply_is_not_discarded(self):
        result, _, _ = self.analyze(identity() + [usb(vendor('51 00 00 00 03')),
                                                 usb(vendor('12 00')),
                                                 usb(vendor('51 00 00 00 01')),
                                                 usb(vendor('50 55'))])
        self.assertEqual([g['profile_selector'] for g in result['transition_groups']], [3, 1])
        self.assertFalse(result['transition_groups'][0]['apply_observed'])
        self.assertTrue(result['transition_groups'][1]['apply_observed'])
        self.assertEqual(result['opcode_counts']['51 53'], 0)

    def test_non_target_device_excluded_only_from_derived(self):
        rows = identity() + [usb(vendor('51 53'), device=99), usb(vendor('51 00 00 00 01'))]
        result, _, derived = self.analyze(rows)
        self.assertEqual(result['record_count'], 4)
        self.assertEqual(result['target_record_count'], 3)
        self.assertEqual(result['opcode_counts']['51 53'], 0)
        self.assertEqual(len(AUDIT.records(derived)[1]), 3)

    def test_reused_address_refuses_false_identity(self):
        other = bytearray(bytes.fromhex('12 01 00 02 00 00 00 40 05 0b 7e 1b 59 01 01 03 02 01'))
        other[8] = 0xff
        with self.assertRaisesRegex(ValueError, 'address reused'):
            self.analyze(identity() + [usb(bytes(other), endpoint=0x80,
                                           transfer=2, stage=3, irp=0)])

    def test_missing_descriptor_refuses_address_guess(self):
        with self.assertRaisesRegex(ValueError, 'refusing to guess'):
            self.analyze([usb(vendor('51 00 00 00 01'))])

    def test_incomplete_descriptor_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            AUDIT.interfaces(bytes.fromhex('09 02 32 00 02 01 00 80 32'))

    def test_changed_configuration_rejected(self):
        rows = identity()
        extra = bytearray(rows[1])
        extra[-1] = 8
        with self.assertRaisesRegex(ValueError, 'Configuration changed'):
            self.analyze(rows + [bytes(extra)])

    def test_big_endian_and_nanosecond_container(self):
        for endian in ('<', '>'):
            for nanos in (False, True):
                _, rows = AUDIT.records(capture(identity(), endian, nanos))
                self.assertEqual(rows[1]['ns'] - rows[0]['ns'], 1000000)
                self.assertEqual(rows[0]['device'], 23)

    def test_malformed_and_truncated_capture_rejected(self):
        valid = capture(identity())
        for invalid in (b'', valid[:-1], valid + b'\x00'):
            with self.assertRaises(ValueError):
                AUDIT.records(invalid)
        wrong_link = bytearray(valid)
        struct.pack_into('<I', wrong_link, 20, 1)
        with self.assertRaisesRegex(ValueError, 'linktype'):
            AUDIT.records(bytes(wrong_link))

    def test_unverified_bulk_rt_never_claimed_validated(self):
        self.assertEqual(AUDIT.semantic(vendor('51 53'))['kind'], 'UnverifiedBulkRt')

    def test_official_captured_transition_fixtures(self):
        fixture = json.loads((ROOT / 'tests/fixtures/official_profile_switch.json').read_text())
        self.assertEqual(fixture['firmware_capture'], '1.00.59')
        self.assertEqual([x['profile_selector'] for x in fixture['transitions']], [6, 1])
        for group in fixture['transitions']:
            rows = identity()
            for item in group['commands']:
                payload = bytes.fromhex(item['payload_hex'])
                self.assertEqual(len(payload), 64)
                rows.append(usb(payload))
                if item['echo_frame'] is not None:
                    rows.append(usb(payload, endpoint=0x85, info=1))
            result, _, _ = self.analyze(rows)
            self.assertEqual(result['opcode_counts']['51 00'], 1)
            self.assertEqual(result['opcode_counts']['50 55'], 1)
            for opcode in ('51 50', '51 4f', '51 58', '51 59', '51 52', '51 53', '51 54', '51 23'):
                self.assertEqual(result['opcode_counts'][opcode], 0)
            self.assertTrue(result['transition_groups'][0]['apply_observed'])


if __name__ == '__main__':
    unittest.main()
