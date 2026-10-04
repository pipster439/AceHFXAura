import importlib.util
from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path);loaded=importlib.util.module_from_spec(spec);spec.loader.exec_module(loaded);return loaded
model=module('characterize','tools/AuraEnumerationCharacterizer/characterize.py')
abi=module('readonly_abi','tools/AuraEnumerationCharacterizer/generate_readonly_abi.py')
guards=module('asus_call_guards','tools/ci/asus_call_guards.py')
def observation(count=3,enumeration='Succeeded',identity='0xA',category=0):
    return dict(category=category,enumeration=dict(execution=enumeration),count=dict(execution='Succeeded',value=count),
                collectionIdentity=dict(execution='Succeeded',value=identity),devices=[])

class CharacterizerTests(unittest.TestCase):
    def test_one_category_per_matrix_child(self):
        for row in model.matrix_plan():
            self.assertEqual(len(model.validate_arguments(row['arguments'])),1)
        for args in (['--single','MTA','0','0x10000'],['--single','MTA','0xdead'],['--single','BAD','0'],['--other','STA','0']):
            with self.assertRaises(ValueError):model.validate_arguments(args)

    def test_fresh_process_plan_is_66_independent_launches(self):
        plan=model.matrix_plan();self.assertEqual(len(plan),66)
        self.assertEqual(len({(p['apartment'],p['category'],p['repetition']) for p in plan}),66)
        self.assertTrue(all(p['arguments'][0]=='--single' for p in plan))
        with self.assertRaises(ValueError):model.matrix_plan(100)

    def test_apartment_labels_and_alternating_order(self):
        plan=model.matrix_plan();self.assertEqual(plan[0]['apartment'],'STA');self.assertEqual(plan[1]['apartment'],'MTA')
        self.assertEqual(plan[22]['apartment'],'MTA');self.assertEqual(plan[23]['apartment'],'STA')
        winrt=model.matrix_plan(winrt=True);self.assertEqual(len(winrt),66)
        self.assertTrue(all(p['initializationVariant']=='COM_plus_WinRT' and p['arguments'][0]=='--single-winrt' for p in winrt))
        self.assertEqual(model.validate_arguments(['--single-winrt','MTA','0']),[0])

    def test_nullable_count_when_vendor_fails(self):
        self.assertIsNone(model.bounded_count(observation(0,'Failed')))
        row=observation(0);row['count']['execution']='Failed';self.assertIsNone(model.bounded_count(row))
        self.assertEqual(model.bounded_count(observation(0)),0)

    def test_device_bound_fails_closed(self):
        for count in (-1,17,100000,None,True):self.assertIsNone(model.bounded_count(observation(count)))
        self.assertEqual(model.bounded_count(observation(16)),16)

    def test_light_sampling_intentionally_disabled_and_bounded(self):
        for count in (0,16,4096):
            result=model.sampling_plan(count);self.assertEqual(result['samples'],0);self.assertLessEqual(result['samples'],16)
            self.assertEqual(result['execution'],'NotAttempted')
        for count in (-1,4097,None):self.assertEqual(model.sampling_plan(count)['execution'],'Failed')
        with self.assertRaises(ValueError):model.sampling_plan(16,True)

    def test_cache_identity_requires_lifetime_overlap(self):
        rows=[observation(identity='0xA'),observation(identity='0xA',category=0x10000)]
        self.assertTrue(model.compare_cache(dict(mode='--same-object',observations=rows))['sameCollection'])
        rows[1]['collectionIdentity']['value']='0xB'
        self.assertFalse(model.compare_cache(dict(mode='--fresh-objects',observations=rows))['sameCollection'])
        self.assertIsNone(model.compare_cache(dict(mode='--fresh-objects-strict',observations=rows))['sameCollection'])
        rows[1]['collectionIdentity']['execution']='Failed'
        self.assertFalse(model.compare_cache(dict(mode='--same-object',observations=rows))['identityComparable'])
        self.assertEqual(model.compare_cache(dict(observations=[]))['execution'],'Unknown')

    def test_aggregation_requires_success_across_repetitions(self):
        rows=[dict(apartment='MTA',category=0,repetition=r,process=dict(pid=100+r),result=dict(observations=[observation(3)])) for r in range(1,4)]
        result=next(g for g in model.aggregate(rows) if g['apartment']=='MTA' and g['category']==0)
        self.assertTrue(result['reproducible']);self.assertEqual(result['counts'],[3]*3)
        rows[2]['result']['observations'][0]['count']['value']=0
        result=next(g for g in model.aggregate(rows) if g['apartment']=='MTA' and g['category']==0)
        self.assertFalse(result['reproducible'])
        rows[2]['result']=None
        result=next(g for g in model.aggregate(rows) if g['apartment']=='MTA' and g['category']==0)
        self.assertEqual(result['counts'],[3,3,None]);self.assertFalse(result['allSucceeded'])

    def test_identity_confidence_separates_cache_from_physical(self):
        device=dict(name=dict(execution='Succeeded',value='Board'),type=dict(execution='Succeeded',value=0x10000),lightCount=dict(execution='Succeeded',value=2))
        topology=[dict(name='Board',key='Mainboard_Master',lightCount=2)]
        identity=model.classify_identity(device,topology,dict(motherboard=[dict(Product='Board')]))
        self.assertEqual(identity['topologyConfidence'],'EXACT');self.assertEqual(identity['physicalConfidence'],'STRONG')
        self.assertTrue(identity['physicalModelExact'])
        self.assertEqual(model.classify_identity(device,[],{})['physicalConfidence'],'UNKNOWN')
        topology[0]['key']='WDL_Keyboard';self.assertNotEqual(model.classify_identity(device,topology,{})['physicalConfidence'],'EXACT')

    def test_reduced_abi_matches_exact_verified_prefixes(self):
        generated=abi.generate((ROOT/'src/AuraWorker/CanonicalAuraAbi.h').read_bytes())
        methods=re.findall(r'STDMETHODCALLTYPE\s+(\w+)\s*\(',generated)
        self.assertEqual(methods,[method for methods in abi.PREFIXES.values() for method in methods])
        self.assertEqual(len(methods),11)
        for name in ('SwitchMode','ReleaseControl','RequireTokenByType','RequireDeviceControlState','put_Color','put_Red','put_Green','put_Blue','Apply','SetLedMatrix'):
            self.assertNotIn(name,generated)
        with self.assertRaises(ValueError):abi.generate(b'altered ABI')

    def test_actual_type_bridge_does_not_conflate_wdl_with_argb(self):
        def device(name,kind,lights):return dict(name=dict(execution='Succeeded',value=name),type=dict(execution='Succeeded',value=kind),lightCount=dict(execution='Succeeded',value=lights))
        groups=[dict(key='Vga',type=131072,lightCount=23),dict(key='WaterCooler',type=856064,lightCount=1),dict(key='WDL_Keyboard',type=1015297,lightCount=1)]
        self.assertEqual(model.classify_identity(device('Vga 1',131072,23),groups,{})['physicalConfidence'],'STRONG')
        self.assertEqual(model.classify_identity(device('ROG STRIX LC III SERIES 1',856064,4),groups,{})['category'],'water cooler')
        wdl=model.classify_identity(device('WindowsLighting_LED',524288,114),groups,dict(lampArrayPnp=[{}]))
        self.assertEqual(wdl['physicalConfidence'],'UNKNOWN');self.assertEqual(wdl['keyboardCandidateConfidence'],'PROBABLE')

    def test_source_declarations_bypasses_and_product_reference_are_blocked(self):
        path='tools/AuraEnumerationCharacterizer/Bad.h'
        for code in ('virtual HRESULT SwitchMode()=0;','HRESULT ReleaseControl(int);','sdk->RequireTokenByType(t,1);','virtual HRESULT put_Color(int)=0;',
                     'virtual HRESULT Apply()=0;','m.SetLedMatrix(a);','m.SetFanDuty(a);','GetProcAddress(dll,m);','object->Invoke(a);','object->lpVtbl[a]();',
                     '#include "CanonicalAuraAbi.h"'):
            self.assertTrue(guards.violations(path,code),code)
        self.assertTrue(guards.violations('winui/Aura.WinUI.csproj','<ProjectReference Include="AuraEnumerationCharacterizer"/>'))
        guards.verify_tree(ROOT)

if __name__=='__main__':unittest.main()
