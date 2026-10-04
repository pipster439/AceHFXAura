import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
model=module('mta_trials','tools/AuraMtaDebugLauncher/trials.py')
abi=module('mta_abi','tools/AuraMtaCrashProbe/generate_abi.py')
guard=module('mta_guard','tools/ci/asus_call_guards.py')
class TriageTests(unittest.TestCase):
    def test_exception_parser_parameters_preserved(self):
        value=model.exception_record(0xc0000409,1,0x123,[5,0xabcdef,0x123456789],7,11)
        self.assertEqual(value['parameters'],[5,0xabcdef,0x123456789]);self.assertEqual(value['fastFailReason'],5)
        self.assertEqual(value['numberParameters'],3);self.assertEqual(value['tid'],7)
        self.assertEqual(json.loads(json.dumps(value)),value)
        with self.assertRaises(ValueError):model.exception_record(0,0,0,list(range(16)),1,1)
    def test_no_fastfail_parameter_means_unknown(self):
        self.assertIsNone(model.exception_record(0xc0000409,1,1,[],1,1)['fastFailReason'])
        self.assertIsNone(model.exception_record(0xc0000005,0,1,[0,0],1,1)['fastFailReason'])
    def test_module_boundaries_and_rva(self):
        modules=[dict(path='A',base=0x1000,size=0x200)]
        self.assertEqual(model.address_module(0x11ff,modules)['rva'],0x1ff)
        self.assertIsNone(model.address_module(0x1200,modules));self.assertIsNone(model.address_module(0xfff,modules))
    def test_child_dies_before_return_has_null_count(self):
        source=(ROOT/'tools/AuraMtaDebugLauncher/Launcher.cpp').read_text()
        self.assertIn('{"count",nullptr}',source);self.assertIn('{"enumerateHresult",nullptr}',source)
        self.assertEqual(model.outcome(0xc0000409,[model.exception_record(0xc0000409,1,1,[5],1,1)],False,False),'FailFast')
    def test_fatal_is_not_reclassified_by_late_budget(self):
        e=model.exception_record(0xc0000409,1,1,[7],1,1)
        self.assertEqual(model.outcome(258,[e],True,False),'FailFast')
        self.assertEqual(model.outcome(258,[],True,False),'Timeout')
        self.assertEqual(model.outcome(0,[],False,True),'Completed')
        self.assertEqual(model.outcome(0,[],False,False),'Failed')
    def test_dump_metadata_hash_not_dump_payload(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'fixture.dmp';p.write_bytes(b'fixture bytes')
            d=model.dump_metadata(p);self.assertEqual(d['bytes'],13);self.assertEqual(len(d['sha256']),64);self.assertTrue(d['localOnly']);self.assertNotIn('payload',d)
    def test_cooldown_abnormal_longer(self):
        self.assertEqual(model.cooldown('Completed'),10)
        for s in ('FailFast','Timeout','Failed'):self.assertEqual(model.cooldown(s),30)
    def test_timing_schedule_fixed_single_category_and_repetitions(self):
        plan=model.timing_plan();self.assertEqual(len(plan),12)
        for delay in (0,250,1000,5000):self.assertEqual([r['repetition'] for r in plan if r['delayMs']==delay],[1,2,3])
        self.assertTrue(all(r['variant']=='COM_ONLY' for r in plan))
        self.assertEqual(len(model.differential_plan()),4)
    def test_aggregation_keeps_unknown_counts(self):
        rows=[dict(variant='COM_ONLY',delayMs=0,outcome='FailFast',count=None),dict(variant='COM_ONLY',delayMs=0,outcome='Completed',count=3)]
        g=model.summary(rows)[0];self.assertEqual(g['FailFast'],1);self.assertEqual(g['Completed'],1);self.assertEqual(g['counts'],[3])
    def test_reduced_surface(self):
        generated=abi.generate((ROOT/'src/AuraWorker/CanonicalAuraAbi.h').read_bytes())
        import re
        methods=re.findall(r'STDMETHODCALLTYPE\s+(\w+)\s*\(',generated)
        self.assertEqual(methods,['get__NewEnum','get_Count','Enumerate'])
        with self.assertRaises(ValueError):abi.generate(b'wrong')
        self.assertFalse(guard.violations('tools/AuraMtaCrashProbe/ProbeAbi.h',generated))
    def test_source_guards_reject_dangerous_surfaces(self):
        for token in ('SwitchMode','ReleaseControl','RequireTokenByType','RequireDeviceControlState','Apply','put_Color','SetLedMatrix','SetFanDuty','get_Item','DeviceIoControl','WriteProcessMemory','LoadLibraryW'):
            self.assertTrue(guard.violations('tools/AuraMtaCrashProbe/Probe.cpp',f'x->{token}();'),token)
        self.assertTrue(guard.violations('tools/AuraMtaDebugLauncher/Launcher.cpp','CoCreateInstance();'))
        for token in ('WriteProcessMemory','VirtualAllocEx','VirtualProtectEx','CreateRemoteThread','SetThreadContext','DebugActiveProcess'):
            self.assertTrue(guard.violations('tools/AuraMtaDebugLauncher/Launcher.cpp',token+'();'))
        guard.verify_tree(ROOT)
    def test_native_debugger_contract(self):
        text=(ROOT/'tools/AuraMtaDebugLauncher/Launcher.cpp').read_text()
        for word in ('DEBUG_ONLY_THIS_PROCESS','WaitForDebugEvent','ExceptionInformation[i]','GetThreadContext','MiniDumpWriteDump','MiniDumpWithThreadInfo','MiniDumpWithUnloadedModules','MiniDumpWithIndirectlyReferencedMemory','MiniDumpWithDataSegs','DBG_EXCEPTION_NOT_HANDLED','ResumeThread'):
            self.assertIn(word,text)
        primary=(ROOT/'tools/AuraMtaCrashProbe/Probe.cpp').read_text()
        self.assertEqual(primary.count('->Enumerate(category,'),1)
        self.assertIn('category=gpu?0x20000UL:0UL',primary)
        launcher=(ROOT/'tools/AuraMtaDebugLauncher/Launcher.cpp').read_text()
        self.assertNotIn('std::filesystem::absolute(argv[1])',launcher)
        self.assertNotIn('third_party/json',primary)
if __name__=='__main__':unittest.main()
