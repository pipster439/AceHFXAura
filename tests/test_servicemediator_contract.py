import importlib.util
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


model = load('mediator_contract', 'tools/ServiceMediatorContract/model.py')
guard = load('mediator_guard', 'tools/ci/asus_call_guards.py')


def member(n=3, kind=1, params=None):
    params = params or []
    return dict(dispid=n, invokeKind=kind, parameters=params, parameterCount=len(params), namesHresult=0)


class ServiceMediatorContractTests(unittest.TestCase):
    def test_typelib_preserves_nested_array_type(self):
        t = dict(vt=26, element=dict(vt=27, element=dict(vt=19)))
        self.assertEqual(model.type_text(t), 'PTR(SAFEARRAY(UI4))')

    def test_typelib_member_and_inherited_filter(self):
        lib = dict(loadHresult=0, typeInfoHresult=0, types=[dict(guid=model.IID, typeAttrHresult=0,
            functions=[member(), member(1610612736)])])
        self.assertEqual([f['dispid'] for f in model.members(lib)], [3])
        lib['types'][0]['functions'].append(member())
        with self.assertRaisesRegex(ValueError, 'Duplicate'): model.members(lib)

    def test_incomplete_typelib_is_not_success(self):
        with self.assertRaises(ValueError): model.members(dict(loadHresult=-1))
        lib = dict(loadHresult=0, typeInfoHresult=0, types=[dict(guid=model.IID, typeAttrHresult=0,
            functions=[dict(member(), parameters=[dict(order=1)], parameterCount=1)])])
        with self.assertRaisesRegex(ValueError, 'ParameterOrder'): model.members(lib)

    def test_classification_not_from_getter_name(self):
        self.assertEqual(model.classify(member(6)), 'UNKNOWN')
        self.assertEqual(model.classify(member(15)), 'UNKNOWN')
        self.assertEqual(model.classify(member(103)), 'UNKNOWN')
        self.assertEqual(model.classify(member(2, 2)), 'READ_ONLY_CONFIRMED')
        self.assertEqual(model.classify(member(68)), 'STATE_CHANGING_CONFIRMED')
        self.assertEqual(model.classify(member(112, 4)), 'STATE_CHANGING_CONFIRMED')

    def test_family_match_does_not_invent_unique_key(self):
        topology = [dict(id=f'dimm:{n}', fields=dict(lightingname='GSkillDram', index=str(n))) for n in range(2)]
        status = ET.fromstring('<root><connecteddevice key="GSkillDram">5</connecteddevice><device key="Group"><scene><led key="25"><DeviceName>GSkillDram</DeviceName></led></scene></device></root>')
        rows = model.map_devices(topology, status, ET.fromstring('<root/>'))
        self.assertEqual(rows[0]['connectedFamilyKey'], 'GSkillDram')
        self.assertEqual(rows[1]['familyMultiplicity'], 2)
        self.assertEqual(rows[0]['statusReferences'][0]['profileLedKeys'], ['25'])
        self.assertTrue(all(r['uniqueWriteAddress'] is None for r in rows))
        self.assertFalse(rows[0]['vendorIndexPassedToMatrixControl'])

    def test_missing_family_key_remains_unknown(self):
        rows = model.map_devices([dict(id='x', fields=dict(lightingname='SimilarGPU'))],
            ET.fromstring('<root><connecteddevice key="GPU">2</connecteddevice></root>'), ET.fromstring('<root/>'))
        self.assertIsNone(rows[0]['connectedFamilyKey'])
        self.assertEqual(rows[0]['confidence'], 'UNKNOWN')

    def test_matrix_shape_and_size_bounds(self):
        for lower, count, vt in ((1, 3, 19), (0, -1, 19), (0, 4097, 19), (0, 2, 17), (0, True, 19)):
            self.assertFalse(model.matrix_payload_bounds({}, lower, count, vt)['accepted'])
        self.assertFalse(model.matrix_payload_bounds({}, 0, 2, 19)['accepted'])
        r = model.matrix_payload_bounds(dict(elementSemantics='CONFIRMED_STATIC', exactLength=2), 0, 2, 19)
        self.assertEqual(r['bytes'], 8); self.assertFalse(r['executionAuthorized'])

    def test_small_motherboard_target_rejected(self):
        addresses = [dict(runtimeRecord='mb', fields=dict(count='2', lightingname='Mainboard_Master'), uniqueWriteAddress=None)]
        result = model.select_target(addresses, dict(deterministicNormalRestore=False))
        self.assertIsNone(result['selectedTarget'])
        self.assertIn('PhysicalZoneUnknown', result['rejected'][0]['reasons'])

    def test_restore_incomplete_and_capacity_keyboard_rejected(self):
        rows = [dict(runtimeRecord=n, fields=dict(lightingname=n)) for n in ('AddressableStrip', 'WDL_Keyboard')]
        r = model.select_target(rows, {})
        self.assertIn('AttachedLengthUnknown', r['rejected'][0]['reasons'])
        self.assertIn('ExistingKeyboardPathPreserved', r['rejected'][1]['reasons'])
        self.assertIn('RestoreContractIncomplete', r['rejected'][0]['reasons'])

    def test_readiness_unknown_false_and_even_ready_not_authorization(self):
        result = model.readiness({})
        self.assertEqual(result['status'], 'SERVICE_MEDIATOR_S1_BLOCKED')
        evidence = {n: True for n in result['missing']}
        r = model.readiness(evidence)
        self.assertEqual(r['status'], 'SERVICE_MEDIATOR_S1_READY_FOR_CANDIDATE_PREPARATION')
        self.assertFalse(r['executionAuthorized']); self.assertFalse(r['candidateExecutable'])
        evidence['deterministicRestore'] = None
        self.assertIn('deterministicRestore', model.readiness(evidence)['missing'])

    def test_runtime_stages_do_not_default_to_success(self):
        r = model.execution_states(True, [], {})
        self.assertTrue(r['registered']); self.assertIsNone(r['activated'])
        self.assertTrue(all(q['invocationSucceeded'] is None and q['payloadValid'] is None for q in r['queries']))

    def test_mutable_methods_and_allowlist_changes_are_guarded(self):
        path = 'tools/LightingBackendProbe/Probe.cpp'
        for name in ('SetProfile', 'SetScript', 'StartEngine', 'SetLedMatrix', 'Acquire_MatrixControl',
                     'Acquire_ledmatrixControl', 'set_AuraExclusive_Status', 'Oled_RestoreLastProfile'):
            self.assertTrue(guard.violations(path, f'client->{name}(0);'), name)
        p = ROOT / guard.MEDIATOR_ADAPTER
        changed = p.read_text(encoding='utf-8').replace('ReadIds={3,4,63}', 'ReadIds={3,4,16}')
        self.assertTrue(guard.violations(guard.MEDIATOR_ADAPTER, changed))

    def test_fallback_and_producer_remain_prohibited(self):
        path = 'tools/ServiceMediatorContract/model.py'
        for text in ('IAuraSdk* p;', 'CoCreateInstance(0);', 'CreateFileMappingW(0);',
                     'SetEvent(0);', 'FILE_MAP_WRITE', 'client->SwitchMode();'):
            self.assertTrue(guard.violations(path, text), text)

    def test_real_source_surface_stays_read_only(self):
        guard.verify_tree(ROOT)

    def test_guard_handles_python_comments_and_docstrings(self):
        text = '\"\"\"SetLedMatrix evidence only\"\"\"\n# caller\'s evidence\nname = "SetLedMatrix"\nclient->SetProfile(0);'
        code = guard.code_only(text, python=True)
        self.assertNotIn('SetLedMatrix', code)
        self.assertIn('SetProfile', code)
        self.assertTrue(guard.violations('tools/LightingBackendProbe/Probe.cpp', text))
        self.assertTrue(guard.violations('tools/LightingBackendProbe/Probe.cpp', '#define WRITE() client->SetProfile(0)'))


if __name__ == '__main__': unittest.main()
