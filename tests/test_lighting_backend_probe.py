import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/LightingBackendProbe'))
import model
import run
spec = importlib.util.spec_from_file_location('asus_guard', ROOT / 'tools/ci/asus_call_guards.py')
guard = importlib.util.module_from_spec(spec); spec.loader.exec_module(guard)


def bytes_header(**overrides):
    fields = dict(magic=model.MAGIC, version=1, width=2, height=2, stride=8, fmt=0, index=1, active=0, state=1, tick=7)
    fields.update(overrides)
    return struct.pack('<9Iq', *fields.values()) + bytes(20)


class LightingBackendProbeTests(unittest.TestCase):
    def test_activation_not_registration(self):
        r = model.mediator_result([dict(stage='Registered'), dict(stage='Completed')], 0, False)
        self.assertEqual(r['activation']['state'], 'NotAttempted')
        self.assertTrue(all(q['hresult'] is None for q in r['queries']))

    def test_activation_failure_queries_not_attempted(self):
        r = model.mediator_result([dict(stage='Activation', state='Failed', hresult=-2147024891), dict(stage='Completed')], 0, False)
        self.assertEqual(r['activation']['state'], 'Failed')
        self.assertTrue(all(q['state'] == 'NotAttempted' for q in r['queries']))

    def test_query_timeout_and_crash_separated(self):
        records = [dict(stage='Activation', state='Succeeded'), dict(stage='QueryPending', dispid=3)]
        for code, timeout, result in ((0xc0000409, False, 'Crash'), (1, True, 'Timeout'), (124, False, 'Timeout'), (5, False, 'Failed')):
            r = model.mediator_result(records, code, timeout)
            self.assertEqual(r['outcome'], result)
            self.assertIsNone(r['queries'][0]['payloadBytes'])
            self.assertEqual(r['queries'][0]['execution'], 'StartedNotReturned')
            self.assertEqual(r['queries'][1]['state'], 'NotAttempted')

    def test_malformed_mediator_responses(self):
        for payload in ('broken', '<root>', '<!DOCTYPE root><root/>', None, 'x' * (model.MAX_PAYLOAD + 1)):
            records = [dict(stage='QueryReturned', dispid=3, state='Succeeded', variantType=8, payload=payload), dict(stage='Completed')]
            self.assertEqual(model.mediator_result(records, 0, False)['queries'][0]['state'], 'MalformedResponse')
        self.assertEqual(model.mediator_result([1], 0, False)['outcome'], 'MalformedOutput')
        records = [dict(stage='QueryReturned', dispid=3, state='Succeeded', variantType=19, payload='<root/>')]
        self.assertEqual(model.mediator_result(records, 0, False)['queries'][0]['state'], 'MalformedResponse')

    def test_valid_mediator_response_topology_is_runtime_evidence(self):
        data = '<root><devicelist><device><type>GPU</type><index>0</index></device></devicelist></root>'
        r = model.mediator_result([dict(stage='QueryReturned', dispid=63, state='Succeeded', variantType=8, payload=data), dict(stage='Completed')], 0, False)
        self.assertEqual(r['queries'][2]['topology'][0]['confidence'], 'RuntimeExact')

    def test_wrong_magic_and_version(self):
        for h in (bytes_header(magic=0), bytes_header(version=2)):
            with self.assertRaisesRegex(ValueError, 'UnsupportedHeader'): model.header(h, 96)

    def test_invalid_mapping_size(self):
        for size in (None, -1, 0, 63, model.MAX_MAPPING + 1):
            with self.assertRaisesRegex(ValueError, 'InvalidMappingSize'): model.header(bytes_header(), size)
        with self.assertRaises(ValueError): model.header(bytes(60), 96)

    def test_offset_overflow_and_truncation_fail_closed(self):
        for h in (bytes_header(stride=0xfffffffc, height=16384), bytes_header(active=2), bytes_header(width=16385), bytes_header(stride=4)):
            with self.assertRaises(ValueError): model.header(h, model.MAX_MAPPING)
        with self.assertRaisesRegex(ValueError, 'BufferOutOfBounds'): model.header(bytes_header(), 95)

    def test_active_buffer_and_payload_changes(self):
        memory = bytes_header() + b'\x01' * 16 + b'\x02' * 16
        first = model.frame_sample(lambda off, size: memory[off:off+size], len(memory))
        memory = bytes_header(active=1, index=2) + b'\x01' * 16 + b'\x02' * 16
        second = model.frame_sample(lambda off, size: memory[off:off+size], len(memory))
        result = model.sample_summary([first, second])
        self.assertEqual(result['distinctHashes'], 2); self.assertEqual(result['activeBufferTransitions'], 1)
        self.assertEqual(second['header']['activeOffset'], 80)

    def test_read_only_sampling_never_mutates_fixture(self):
        memory = bytearray(bytes_header() + bytes(32)); before = bytes(memory); calls = []
        def read(off, size): calls.append((off, size)); return bytes(memory[off:off+size])
        model.frame_sample(read, len(memory))
        self.assertEqual(before, bytes(memory)); self.assertEqual(calls, [(0, 64), (64, 16), (0, 64)])

    def test_concurrent_change_is_not_coherent_frame_proof(self):
        memory = bytes_header() + bytes(32); n = 0
        def read(off, size):
            nonlocal n
            n += 1
            return bytes_header(index=2) if n == 3 else memory[off:off+size]
        result = model.frame_sample(read, len(memory))
        self.assertFalse(result['coherent']); self.assertEqual(model.sample_summary([result])['coherentCount'], 0)

    def test_wdl_zero_one_multiple(self):
        for count in (0, 1, 3):
            rows = model.wdl_descriptors([dict(interfaceId=f'interface-{i}', name='same') for i in range(count)])
            self.assertEqual(len(rows), count)
            self.assertEqual(len({r['id'] for r in rows}), count)
            self.assertTrue(all(r['physicalIdentityConfidence'] == 'Unknown' for r in rows))

    def test_topology_confidence_no_name_merge(self):
        xml = '<root><devicelist><device><model>same</model></device><device><model>same</model></device></devicelist></root>'
        rows = model.device_descriptors(xml, 'current', 'ConfigExact')
        self.assertNotEqual(rows[0]['id'], rows[1]['id'])
        self.assertTrue(all(r['confidence'] == 'ConfigExact' for r in rows))
        with self.assertRaises(ValueError): model.device_descriptors(xml, 'current', 'Invented')

    def test_selection_does_not_fake_output_or_restoration(self):
        c = dict(name='LightingServiceMediator', readOnlyValidated=True, restoreValidated=False, outputProtocolValidated=False)
        result = model.choose_backend([c])
        self.assertEqual(result['preferredMetadataBackend'], c['name'])
        self.assertEqual(result['status'], 'ALTERNATIVE_AURA_BACKEND_NOT_READY')
        self.assertEqual(result['auraSdk']['runtime'], 'EnumerationUnreliable')
        self.assertFalse(result['reviewedGateAExecutionEnabled'])
        ready = dict(c, restoreValidated=True, outputProtocolValidated=True, perDeviceAddressingValidated=True)
        self.assertEqual(model.choose_backend([ready])['selectedBackend'], c['name'])
        self.assertFalse(model.choose_backend([ready])['executableWriteGate'])

    def test_real_fixture_worker_timeout_and_malformed_output(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'audit_artifacts') as tmp:
            out = Path(tmp) / 'fixture.jsonl'
            r = run.supervise([sys.executable, '-c', 'import time;time.sleep(2)'], out, .1)
            self.assertTrue(r['timeout'])
            r = run.supervise([sys.executable, '-c', 'print("not-json")'], out, 2)
            self.assertTrue(r['malformedOrExcessiveOutput'])
            r = run.supervise([sys.executable, '-c', 'import sys;sys.stdout.buffer.write(b"x"*(5*1024*1024))'], out, 2)
            self.assertTrue(r['malformedOrExcessiveOutput']); self.assertEqual(r['records'], [])

    def test_source_guards_and_immutable_dispatch_allowlist(self):
        for symbol in ('SwitchMode', 'ReleaseControl', 'SetLedMatrix', 'Apply', 'put_Color', 'FromIdAsync', 'SetColors', 'CreateFileMappingW', 'SetEvent', 'FILE_MAP_WRITE', 'DeviceIoControl'):
            self.assertTrue(guard.violations('tools/LightingBackendProbe/Probe.cpp', symbol + '();'), symbol)
        adapter = (ROOT / guard.MEDIATOR_ADAPTER).read_text(encoding='utf-8')
        self.assertFalse(guard.violations(guard.MEDIATOR_ADAPTER, adapter))
        self.assertTrue(guard.violations(guard.MEDIATOR_ADAPTER, adapter.replace('ReadIds={3,4,63}', 'ReadIds={3,4,68}')))
        self.assertTrue(guard.violations(guard.MEDIATOR_ADAPTER, adapter.replace('DISPATCH_METHOD,&args', 'DISPATCH_PROPERTYPUT,&args')))
        self.assertTrue(guard.violations('winui/Aura.WinUI.csproj', '<Reference>LightingBackendProbe</Reference>'))
        self.assertTrue(guard.violations('CMakeLists.txt', 'add_subdirectory(tools/LightingBackendProbe)'))
        guard.verify_tree(ROOT)

    def test_observer_missing_remains_nullable_and_only_read_rights(self):
        source = (ROOT / 'tools/LightingBackendProbe/observe.py').read_text(encoding='utf-8')
        self.assertIn('mappingBytes=None, header=None', source)
        self.assertIn('MapViewOfFile(handle, FILE_MAP_READ', source)
        self.assertIn('k.UnmapViewOfFile(view)', source)
        self.assertNotIn('WaitForSingleObject', source)
        self.assertNotIn('memmove', source)
        self.assertNotIn('CreateFileMapping', source)

    def test_missing_mapping_and_event_native_error_are_preserved(self):
        import ctypes
        import observe
        calls = []
        class Function:
            def __init__(self, name): self.name = name
            def __call__(self, *args):
                calls.append((self.name, args)); return 0
        class Library:
            def __getattr__(self, name):
                value = Function(name); setattr(self, name, value); return value
        with patch.object(ctypes, 'WinDLL', side_effect=lambda *a, **k: Library(), create=True), patch.object(ctypes, 'get_last_error', return_value=2, create=True):
            result = observe.observe_windows()
        self.assertEqual([r['state'] for r in result['rows']], ['Unavailable', 'Unavailable'])
        self.assertTrue(all(r['mappingBytes'] is None and r['header'] is None and r['win32Error'] == 2 for r in result['rows']))
        self.assertFalse(any(name == 'MapViewOfFile' for name, _ in calls))

if __name__ == '__main__': unittest.main()
