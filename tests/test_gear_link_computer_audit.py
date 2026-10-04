"""Offline Computer Use audit safety checks. No worker/process/device launch."""
import json
from datetime import datetime, timezone
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/research'))
from gear_link_case_scope import inspect_scope
from gear_link_live_bank_gate import inspect as inspect_live
import gear_link_audit_session as session
import test_gear_link_reference as fixtures
from gear_link_capture_privacy import PrivacyFilter


class ComputerAuditTests(unittest.TestCase):
    def test_rgb_ownership_counter_does_not_retain_rgb_body(self):
        filt=PrivacyFilter()
        filt.targets.add((1,2)); filt.epmaps[(1,2)]={0x0d:1}
        payload=b'\xc0\x81'+bytes(62)
        # Build a USBPcap target submission; only a counter reaches the manifest.
        raw=struct.pack('<HQIHBHHBBI',27,1,0,9,0,1,2,0x0d,1,64)+payload
        self.assertFalse(filt.accept(raw))
        self.assertEqual(filt.stats['direct_rgb_out_submissions'],1)
        completed=struct.pack('<HQIHBHHBBI',27,1,0,9,1,1,2,0x0d,1,64)+payload
        self.assertFalse(filt.accept(completed))
        self.assertEqual(filt.stats['direct_rgb_out_submissions'],1)
    def test_basic_info_drift_without_selection_still_blocks(self):
        p = bytearray(64); p[:2] = b'\x12\x00'; p[8:11] = bytes([6, 0, 3])
        result = inspect_scope([self.row(bytes(p), 'IN', True)], 5)
        self.assertEqual(result['scope'], 'contaminated')
        self.assertEqual(result['unexpected_bank_selections'], [])

    def test_reselecting_test_bank_does_not_erase_prior_observed_drift(self):
        def info(slot, frame):
            p = bytearray(64); p[:2] = b'\x12\x00'; p[8:11] = bytes([6, 0, slot])
            return self.row(bytes(p), 'IN', True, frame)
        rows = [info(5, 1), info(3, 2),
                self.row(b'\x51\x00\x00\x00\x05' + bytes(59), frame=3), info(5, 4)]
        self.assertEqual(inspect_scope(rows, 5)['scope'], 'contaminated')

    def live_capture(self, payloads):
        packets = [fixtures.usb(fixtures.DEVICE, ep=0x80, transfer=2),
                   fixtures.usb(fixtures.CONFIG, ep=0x80, transfer=2)] + payloads
        header = bytes.fromhex('d4 c3 b2 a1') + struct.pack('<HHiiII', 2, 4, 0, 0, 65535, 249)
        return header + b''.join(struct.pack('<IIII', 100, i * 1000, len(p), len(p)) + p
                                 for i, p in enumerate(packets))

    def inspect_synthetic(self, packets, age=1):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name)/'retained.pcap'
            path.write_bytes(self.live_capture(packets))
            return inspect_live(path, now=datetime.fromtimestamp(100 + age, timezone.utc))

    def basic_info(self, slot):
        p = bytearray(64); p[:2] = b'\x12\x00'; p[8:11] = bytes([6, 0, slot])
        return fixtures.usb(bytes(p), ep=0x85)

    def test_live_gate_requires_actual_device_observation(self):
        result = self.inspect_synthetic([fixtures.usb(b'\x51\x00\x00\x00\x05' + bytes(59))])
        self.assertFalse(result['gate_passed'])
        self.assertEqual(result['authority'], 'unknown')

    def test_fresh_slot5_basic_info_passes_discrete_gate(self):
        result = self.inspect_synthetic([self.basic_info(5)])
        self.assertTrue(result['gate_passed'])
        self.assertEqual(result['device_observations'][-1]['active_slot'], 5)

    def test_stale_or_future_basic_info_never_grants_authority(self):
        for age in (100, -1):
            self.assertFalse(self.inspect_synthetic([self.basic_info(5)], age)['gate_passed'])

    def test_live_drift_cannot_be_cleared_by_later_slot5(self):
        for drift in (self.basic_info(3), fixtures.usb(b'\x51\x00\x00\x00\x01' + bytes(59))):
            result = self.inspect_synthetic([drift, self.basic_info(5)])
            self.assertFalse(result['gate_passed'])
            self.assertEqual(result['authority'], 'drift')

    def test_live_gate_rejects_unreviewed_identity_response(self):
        with self.assertRaisesRegex(ValueError, 'Unreviewed'):
            self.inspect_synthetic([fixtures.usb(b'\x12\x14' + bytes(62), ep=0x85)])

    def test_missing_status_retry_is_bounded_and_not_json_retry(self):
        path = unittest.mock.Mock()
        path.read_text.side_effect = [FileNotFoundError(), '{"state":"ready"}']
        with patch.object(session.time, 'sleep') as sleep:
            self.assertEqual(session.read(path), {'state': 'ready'})
            sleep.assert_called_once_with(.025)
        path.read_text.side_effect = FileNotFoundError()
        with patch.object(session.time, 'sleep') as sleep:
            with self.assertRaises(FileNotFoundError): session.read(path)
            self.assertEqual(sleep.call_count, 2)
        path.read_text.side_effect = ['malformed']
        with patch.object(session.time, 'sleep') as sleep:
            with self.assertRaises(json.JSONDecodeError): session.read(path)
            sleep.assert_not_called()

    def test_recovery_drift_retained_frame_fixture(self):
        fixture = json.loads((ROOT/'tests/fixtures/gear_link/recovery_drift.json').read_text(encoding='utf8'))
        result = inspect_scope(fixture['records'], 5)
        self.assertEqual(result['scope'], 'contaminated')
        self.assertEqual([(x['frame'], x['slot']) for x in result['unexpected_bank_selections']], [(11, 3), (13, 1)])
        self.assertEqual(result['device_bank_observations'][-1]['active_slot'], 1)
        self.assertEqual(fixture['operator_confirmation'], '没有操作')
        self.assertEqual(fixture['sender_attribution'], 'NOT VERIFIED')

    def test_actual_bank_observation_and_contamination_fixtures(self):
        values = json.loads((ROOT/'tests/fixtures/gear_link/computer_use_reference.json').read_text(encoding='utf8'))
        for fixture in values['fixtures']:
            self.assertEqual(inspect_scope(fixture['records'],5),fixture['expected'])
            self.assertEqual(len(fixture['source_sha256']),64)
        self.assertEqual(values['fixtures'][0]['expected']['device_bank_observations'][-1]['active_slot'],5)
        self.assertTrue(all(f['expected']['scope']=='contaminated' for f in values['fixtures'][1:]))

    def row(self, payload, direction='OUT', completed=False, frame=1):
        return {'interface': 1, 'status': 0, 'payload_hex': payload.hex(),
                'direction': direction, 'completed': completed, 'frame': frame,
                'timestamp_utc': '2026-10-02T00:00:00Z'}

    def test_visible_test_slot_does_not_excuse_other_bank_selection(self):
        payload = b'\x51\x00\x00\x00\x01'+bytes(59)
        result = inspect_scope([self.row(payload)], 5)
        self.assertEqual(result['scope'], 'contaminated')
        self.assertEqual(result['unexpected_bank_selections'][0]['slot'], 1)

    def test_same_bank_selection_has_no_false_contamination_claim(self):
        payload = b'\x51\x00\x00\x00\x05'+bytes(59)
        result = inspect_scope([self.row(payload)], 5)
        self.assertEqual(result['unexpected_bank_selections'], [])
        self.assertNotEqual(result['scope'], 'verified')

    def test_only_completed_basic_info_response_reads_bank(self):
        p = bytearray(64); p[:2] = b'\x12\x00'; p[8:11] = bytes([6, 0, 5])
        rows = [self.row(bytes(p)), self.row(bytes(p), 'IN'),
                self.row(bytes(p), 'IN', True, 3)]
        result = inspect_scope(rows, 5)
        self.assertEqual(result['device_bank_observations'], [
            {'frame': 3, 'timestamp_utc': '2026-10-02T00:00:00Z',
             'profile_count': 6, 'active_slot': 5, 'lighting_effect': 0}])

    def test_zero_request_is_not_a_bank_readback(self):
        result = inspect_scope([self.row(b'\x12\x00'+bytes(62), 'IN', True)], 5)
        self.assertEqual(result['device_bank_observations'], [])

    def test_narrow_ownership_retention_rejects_unknown_body(self):
        guard = fixtures.GearReferenceTests().target()
        for opcode in (0x27, 0x74):
            for enabled in (0, 1):
                payload = bytes([opcode, 0, 0, 0, enabled])+bytes(59)
                self.assertTrue(guard.accept(fixtures.usb(payload)))
            for offset in (1, 3, 4, 8, 63):
                payload = bytearray(bytes([opcode, 0, 0, 0, 0])+bytes(59))
                payload[offset] = 2
                self.assertFalse(guard.accept(fixtures.usb(bytes(payload))))

    def test_begin_rejects_occupied_worker_before_launch(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); (root/'state').mkdir()
            session.write(root/'audit_session_state.json', {'test_slot': 5, 'current_capture': None})
            session.write(root/'slot_ledger.json', {'slots': {'5': {'writable': True}}})
            session.write(root/'state/capture-worker.json', {'state': 'capturing'})
            with patch.object(session.subprocess, 'Popen') as launch:
                with self.assertRaisesRegex(ValueError, 'not idle'):
                    session.begin(root, 'no_double_capture', 'read-only case')
                launch.assert_not_called()

    def test_unresolved_scope_failure_prevents_another_case(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name);self.initial_session(root)
            value=session.read(root/'audit_session_state.json')
            value['phase']='BLOCKED_EXTERNAL_WRITER'
            session.write(root/'audit_session_state.json',value)
            with patch.object(session.subprocess,'Popen') as launch:
                with self.assertRaisesRegex(ValueError,'safety review'):
                    session.begin(root,'blind_next_case','must not continue')
                launch.assert_not_called()

    def test_begin_rejects_unapproved_slot_before_launch(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); (root/'state').mkdir()
            session.write(root/'audit_session_state.json', {'test_slot': 5})
            session.write(root/'slot_ledger.json', {'slots': {'5': {'writable': False}}})
            with patch.object(session.subprocess, 'Popen') as launch:
                with self.assertRaisesRegex(ValueError, 'authorized'):
                    session.begin(root, 'not_authorized', 'case')
                launch.assert_not_called()

    def test_begin_rejects_path_escape(self):
        with patch.object(session.subprocess, 'Popen') as launch:
            with self.assertRaisesRegex(ValueError, 'case name'):
                session.begin(Path('.'), '../escape', 'case')
            launch.assert_not_called()

    def initial_session(self, root):
        (root/'state').mkdir(); (root/'logs').mkdir(); (root/'usb').mkdir()
        session.write(root/'audit_session_state.json', {'test_slot':5, 'current_capture':None})
        session.write(root/'slot_ledger.json', {'slots':{'5':{'writable':True}}})
        session.write(root/'state/capture-worker.json', {'state':'ready'})

    def test_descriptor_failure_stops_owned_capture_and_preserves_case(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); self.initial_session(root)
            capture = root/'usb/missing_descriptor.pcap'
            capture.write_bytes(b'original evidence')
            # begin refuses preexisting evidence; create it only after launch.
            capture.unlink()
            def launch(*args, **kwargs):
                capture.write_bytes(b'original evidence')
                return unittest.mock.Mock()
            with patch.object(session.subprocess,'Popen',side_effect=launch), \
                 patch.object(session,'wait_until'), patch.object(session,'records',return_value=(b'',[])), \
                 patch.object(session,'command',wraps=session.command) as command:
                with self.assertRaisesRegex(ValueError,'descriptor'):
                    session.begin(root,'missing_descriptor','No UI action')
                self.assertEqual([c.args[1] for c in command.call_args_list],['start','stop'])
            self.assertEqual(capture.read_bytes(), b'original evidence')
            case = session.read(root/'state/missing_descriptor/case.json')
            self.assertEqual(case['classification'],'NOT VERIFIED')
            self.assertEqual(case['cleanup'],'owned capture stopped')

    def test_readiness_failure_never_starts_capture(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); self.initial_session(root)
            with patch.object(session.subprocess,'Popen') as launch, \
                 patch.object(session,'wait_until',side_effect=TimeoutError('not ready')), \
                 patch.object(session,'command') as command:
                launch.return_value.poll.return_value = None
                with self.assertRaises(TimeoutError):
                    session.begin(root,'not_ready','No UI action')
                command.assert_not_called()
                launch.return_value.terminate.assert_called_once()
                self.assertEqual(session.read(root/'state/not_ready/case.json')['classification'],'NOT VERIFIED')


if __name__ == '__main__':
    unittest.main()
