"""Recompute M2.9 conclusions from local evidence and fixed read-only vendor configuration files."""
import hashlib
import json
from pathlib import Path
import winreg
from datetime import datetime, timezone

from model import members, type_text, classify, map_devices, select_target, readiness, execution_states

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'audit_artifacts/phase3-m2.9'


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save(name, value):
    (OUT / name).write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding='utf-8')


def main():
    # Explicit file import avoids the two research modules' coincidentally identical filenames.
    import importlib.util
    spec = importlib.util.spec_from_file_location('lighting_evidence_model', ROOT / 'tools/LightingBackendProbe/model.py')
    parsing = importlib.util.module_from_spec(spec); spec.loader.exec_module(parsing)
    library = read_json(OUT / 'servicemediator-typelib.json')
    vtable = {r['dispid']: r for r in read_json(OUT / 'raw/vtable-members.json')}
    functors = {r['dispid']: r for r in read_json(OUT / 'raw/service-functors.json')}
    proof = {
        1: ('Profile parser/engine and LastProfile persistence; not a transient target handle', '0x2d9340; 0x2cfad0', 'AuraPlugin 0x24a5e0 -> slot 7; callers 0x13a550/0x13a6b0 use deviceId="1" plus XML'),
        2: ('Not implemented on installed native vtable: returns E_NOTIMPL (0x80004001)', '0x1cc3b0', 'No useful capture/restore payload; not invoked'),
        3: ('Metadata XML traversal/BSTR', '0x2dff30 -> 0x2c3eb0', 'M2.8 and M2.9 isolated read-only runtime verified'),
        4: ('Capability XML construction/BSTR', '0x2dfcb0 -> 0x2c27c0', 'M2.8 and M2.9 isolated read-only runtime verified'),
        15: ('Loads setting-store last profile; reads exclusivemode with fallback 1; not PID/SID ownership', '0x2e0af0', 'No matching end-to-end ASUS acquire/restore/release caller recovered'),
        16: ('AuraRequireToken internal call, global manager flag and exclusivemode persistence', '0x2e0dd0 -> 0x2b7e00', 'No matching end-to-end ASUS acquire/restore/release caller recovered'),
        60: ('Engine stop/rebuild script path', '0x2e0080 -> 0x2d0d30', 'AuraPlugin 0x248df0 slot 23: supplied XML, rhs=0; critical section in 0x12a330'),
        61: ('Engine manager mode/configuration branch', '0x2dbfe0', 'AuraPlugin 0x24ba50 slot 24; supplied engine string'),
        63: ('Logical controller metadata XML', '0x2df7d0 -> 0x2c2eb0', 'M2.8 and M2.9 isolated read-only runtime verified'),
        64: ('cmd=1 installs script/start path; cmd=0 stops and attempts lastscript load, then changes global mode', '0x2dc6c0', 'AuraPlugin 0x24bc80 slot 27: profile XML, cmd=1, caller rhs; critical section in 0x1383d0'),
        68: ('AP match and global matrix enable flag; special device-type matrix distribution; not nine-record routing', '0x2d7450 -> 0x2cf6d0', 'No SetLedMatrix caller/payload producer identified in scoped AuraPlugin scan'),
        69: ('control=1/0 changes global flag and persistent AP tag; no per-device index', '0x2d9e10 -> 0x2d17b0', 'AuraPlugin 0x24b0a0 slot 32; 0x1387c0 forwards (param2==0), caller AP string'),
        70: ('Script/control globals, LedMatrixConfig AP registry write and completion wait', '0x2e0590', 'End-to-end first-write/restore caller not identified'),
        102: ('type/model selection map; modifies MATRIX DATA/MatrixStatus.ini/script/status, no index parameter', '0x2d76c0', 'AuraPlugin 0x249040 slot 35; 0x129550 uses MATRIX_Laptop plus stored model and status; 0x129850 iterates matched records with status=1'),
        103: ('Reads map entry by type/model; meaning/side effects of all helper operations not completely audited', '0x2db340 -> 0x2c1df0', 'Unknown numeric status; not invoked'),
        108: ('Builds game-mode metadata; downstream semantics not fully audited', '0x2dfdd0 -> 0x2bf690', 'Not invoked'),
        101: ('Cancels a service request; not a demonstrated lighting restoration API', '0x2d5660', 'Not invoked'),
    }
    inventory = []
    for f in members(library):
        n = f['dispid']; category = classify(f)
        claim, static, caller = proof.get(n, ('PROPERTYPUT assignment declaration; payload/routing/restore untraced' if f['invokeKind'] in (4, 8) else 'ABI does not establish mutability, normal restoration or target isolation', 'NotAnalyzed: outside bounded S1 trace', 'NotEstablished in scoped caller evidence'))
        inventory.append(dict(member=f, classification=category,
            claim=claim, confidence='CONFIRMED_STATIC' if (n in proof and n not in (103, 108)) or f['invokeKind'] in (4, 8) else 'UNKNOWN',
            typeLibEvidence=dict(dispid=n, memberKind=f['memberKind'], returnType=type_text(f['returnType']),
                parameters=[dict(order=p['order'], name=p['name'] or None, type=type_text(p['type']), flags=p['flags']) for p in f['parameters']]),
            nativeWrapper=vtable[n]['rva'], functor=functors.get(n), staticCallerEvidence=static,
            observedAsusCallerSequence=caller, hostInvocation='Succeeded' if n in (3, 4, 63) else 'NotAttempted',
            openQuestion='Physical addressing, prerequisite control and deterministic restore are not established' if category != 'READ_ONLY_CONFIRMED' else ('No payload: E_NOTIMPL' if n == 2 else 'Metadata is not ownership/physical-state proof')))
    configs = []
    for path in [Path(r'C:\ProgramData\ASUS\RogAura30') / n for n in ('GetDeviceCap.xml', 'GetDeviceStatus.xml', 'QueryAllDevice.xml')] + [Path(r'C:\Program Files (x86)\LightingService') / n for n in ('LastProfile.xml', 'DevLastStatConfig.xml', 'script/LastScript.xml', 'script/LedMatrix_LastScript.xml')]:
        row = dict(path=str(path), state='Unavailable', sha256=None, payload=None)
        if path.is_file():
            data = path.read_bytes()
            if len(data) > 1024 * 1024: row['state'] = 'TooLarge'
            else:
                text = data.decode('utf-8-sig'); parsing.xml_root(text)
                row.update(state='CapturedReadOnly', sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), payload=text)
        configs.append(row)
    save('configuration-evidence.json', configs)
    registry = []
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\ASUS\LedMatrixConfig', 0, winreg.KEY_READ | winreg.KEY_WOW64_32KEY) as key:
            value, kind = winreg.QueryValueEx(key, 'AP')
        registry.append(dict(hive='HKLM', view=32, key=r'SOFTWARE\ASUS\LedMatrixConfig', name='AP', value=value, kind=kind, state='Read'))
    except (FileNotFoundError, PermissionError) as e:
        registry.append(dict(name='AP', value=None, state=type(e).__name__))
    save('registry-evidence.json', registry)
    runtime = read_json(OUT / 'readonly-runtime-probe.json')
    if runtime['result']['outcome'] != 'Completed' or runtime['result']['activation']['state'] != 'Succeeded':
        raise ValueError('ReadOnlyRuntimeNotValidated')
    queries = {r['dispid']: r for r in runtime['result']['queries']}
    if any(queries[n]['state'] != 'Succeeded' for n in (3, 4, 63)): raise ValueError('QueryFailed')
    runtime['executionStates'] = execution_states(any(r['clsid'] == '{95775DC4-77AA-4E94-8CF6-68267EEF1856}' and r['hive'] == 'HKLM' and r['state'] == 'Detected' for r in runtime['registeredEvidence']), runtime['records'], runtime['result'])
    runtime['additionalGetters'] = dict(state='NotAttempted', reason='GetProfile E_NOTIMPL; exclusive/control getter meaning/helpers incomplete; no additional query required to resolve blocking facts')
    runtime['capturedAt'] = runtime.pop('startedOrCapturedAt', runtime.get('capturedAt'))
    save('readonly-runtime-probe.json', runtime)
    status = parsing.xml_root(queries[3]['payload']); cap = parsing.xml_root(queries[4]['payload'])
    addresses = map_devices(queries[63]['topology'], status, cap)
    representations = dict(topologyRecords=len(addresses), topologyDeclaredSlots=sum(int(r['fields']['count']) for r in addresses),
        capabilitySections=[dict(key=d.get('key'), declaredCount=d.findtext('./ledlist/ledcount'),
            explicitLedRecords=len(d.findall('./ledlist/led')), leds=[dict(key=l.get('key'), fields={c.tag:c.text for c in l}) for l in d.findall('./ledlist/led')]) for d in cap.findall('./device')],
        countSemantics='Distinct representations, not interchangeable. Group count is internal aggregate field at controller+0x3c; physical length/unit of 1122 unresolved. Three compressed strip zones declare 500 capacity each; QueryAllDevice declares 120 each.',
        matrixProtocol=dict(type='PTR(SAFEARRAY(UI4))', apNameRole='Exact match against global registry AP, not a device address',
            receiverTypes=['0x13000', '0xF8900'], specialLayout='0x13000: 28 rows x 68 logical input positions with triangular remap',
            generalBranch='0xF8900: min(width*height, ubound-lbound) loop; exact safe length unresolved',
            lowerBoundHandling='Vendor indexes from zero even after reading bounds; nonzero lower bound unsafe',
            elementSemantics='UNKNOWN', exactLength=None, wholeTopologyPayloadValidated=False))
    save('addressing-model.json', dict(records=addresses, representations=representations,
        profileSelector=dict(observedDeviceId='1', selection='XML device/led/viewport selectors; constant is not a unique physical address'),
        physicalUniquenessValidated=False, targetAddress=None))
    restore = dict(deterministicNormalRestore=False, sameProcessRestoreBoundValidated=False, restoreSequence=None,
        releaseSequence=None, observedAcquireWriteReleaseRestoreSequence=None,
        candidates=[dict(name='GetProfile baseline', state='Rejected', reason='Native E_NOTIMPL'),
            dict(name='StartEngine cmd=0', state='Incomplete', reason='Stops all threads/engine and reloads mutable lastscript; no exact prior owner/effect/phase guarantee'),
            dict(name='Acquire_ledmatrixControl control=0', state='Incomplete', reason='Global flag/AP label reset; no proved baseline script/ownership restore'),
            dict(name='Acquire_MatrixControl status=0', state='Incomplete', reason='Type/model configuration/script mutation; no unique index/previous state transaction'),
            dict(name='AuraExclusive status=0', state='RejectedAsAssumption', reason='Internal vendor token + global persistent setting; zero not demonstrated as previous-state restore'),
            dict(name='Saved XML/old colors replay', state='RejectedAsAssumption', reason='Current status and saved profile differ in shape and may be changed/persisted by writes')],
        missing=['Exact target/zone routing', 'Color packing and safe array length for a current device', 'One actual ASUS acquire->write->normal restore->release path', 'Baseline state snapshot immune to overwrite', 'Restore operation/result semantics and bounded completion'],
        failurePolicy='RECOVERY_REQUIRED; no second write, alternate API, AuraSdk/HID fallback, vendor restart or reboot',
        futureOwner='Self-contained medium process, same restore-capable COM object, autonomous <=5s hard TTL from before first state change; UI exit must not kill owner; unimplemented')
    save('restore-contract.json', restore)
    choice = select_target(addresses, restore)
    s1 = readiness({})
    s1.update(choice, evidenceReferences=['servicemediator-typelib.json','control-callsite-analysis.json','addressing-model.json','restore-contract.json','readonly-runtime-probe.json'])
    save('s1-readiness.json', s1)
    save('control-callsite-analysis.json', dict(inventory=inventory, observedSequenceKind='STATIC_ONLY; no ASUS setter executed by this task',
        nativeBase='0x400000 (x86)', pluginBase='0x180000000 (x64)',
        inputArtifacts=[dict(path=str(p.relative_to(ROOT)), sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted((OUT/'raw').glob('*.txt'))],
        unresolvedCaller='No SetLedMatrix payload construction nor AuraExclusive full acquire/release sequence found in bounded AuraPlugin scope; absence is not a global absence claim',
        failureCleanup='Inspected wrappers free BSTR and ordinary COM references on ordinary paths; no proven compensating lighting restore on error',
        count=len(inventory), derivedAt=datetime.now(timezone.utc).isoformat()))
    print(s1['status'], 'inventory', len(inventory), 'topology', len(addresses))


if __name__ == '__main__': main()
