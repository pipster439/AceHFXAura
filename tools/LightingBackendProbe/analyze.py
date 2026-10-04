"""Recompute the fixed M2.8 decision from local read-only observations and metadata."""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import winreg
import xml.etree.ElementTree as ET
from model import choose_backend, device_descriptors, xml_root, wdl_descriptors

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'audit_artifacts/phase3-m2.8'


def file_metadata(path):
    row = dict(path=str(path), state='Unavailable', sha256=None, version=None)
    if not path.is_file(): return row
    data = path.read_bytes()
    row.update(state='Detected', bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    version = c.WinDLL('version', use_last_error=True)
    version.GetFileVersionInfoSizeW.argtypes = [w.LPCWSTR, c.c_void_p]; version.GetFileVersionInfoSizeW.restype = w.DWORD
    version.GetFileVersionInfoW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p]; version.GetFileVersionInfoW.restype = w.BOOL
    version.VerQueryValueW.argtypes = [c.c_void_p, w.LPCWSTR, c.POINTER(c.c_void_p), c.POINTER(w.UINT)]; version.VerQueryValueW.restype = w.BOOL
    size = version.GetFileVersionInfoSizeW(str(path), None)
    if 0 < size < 512*1024:
        buffer = c.create_string_buffer(size); ptr = c.c_void_p(); length = w.UINT()
        if version.GetFileVersionInfoW(str(path), 0, size, buffer) and version.VerQueryValueW(buffer, '\\', c.byref(ptr), c.byref(length)) and length.value >= 16:
            values = c.cast(ptr, c.POINTER(c.c_uint32))
            row['version'] = '.'.join(str(n) for n in (values[2] >> 16, values[2] & 65535, values[3] >> 16, values[3] & 65535))
    try:
        import pefile
        pe = pefile.PE(data=data, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        row['staticImports'] = [i.dll.decode('ascii', 'replace') for i in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', [])]
        pe.close()
    except ImportError:
        row['staticImports'] = None
    return row


def registry_metadata():
    records = []
    mediator = '{95775DC4-77AA-4E94-8CF6-68267EEF1856}'
    aura = '{05921124-5057-483E-A037-E9497B523590}'
    for hive, hive_name in ((winreg.HKEY_LOCAL_MACHINE, 'HKLM'), (winreg.HKEY_CURRENT_USER, 'HKCU')):
        for view, bits in ((winreg.KEY_WOW64_32KEY, 32), (winreg.KEY_WOW64_64KEY, 64)):
            for clsid in (mediator, aura):
                row = dict(hive=hive_name, registryBits=bits, clsid=clsid, state='Unavailable')
                try:
                    with winreg.OpenKey(hive, 'Software\\Classes\\CLSID\\'+clsid, 0, winreg.KEY_READ | view) as key:
                        row['state'] = 'Detected'
                        for name in ('AppID',):
                            try: row[name] = winreg.QueryValueEx(key, name)[0]
                            except FileNotFoundError: pass
                        for sub in ('LocalServer32', 'InprocServer32'):
                            try:
                                with winreg.OpenKey(key, sub) as server: row[sub] = winreg.QueryValueEx(server, '')[0]
                            except FileNotFoundError: pass
                    if row.get('AppID'):
                        with winreg.OpenKey(hive, 'Software\\Classes\\AppID\\'+row['AppID'], 0, winreg.KEY_READ | view) as key:
                            try: row['LocalService'] = winreg.QueryValueEx(key, 'LocalService')[0]
                            except FileNotFoundError: pass
                except FileNotFoundError: pass
                except PermissionError: row['state'] = 'PermissionDenied'
                records.append(row)
    return records


def canonical_xml(payload):
    xml_root(payload)
    return ET.canonicalize(payload, strip_text=True)


def main():
    pf = Path(os.environ['ProgramFiles(x86)']); pd = Path(os.environ['ProgramData'])
    files = [pf / 'LightingService/LightingService.exe', pf / 'ASUS/AURA lighting effect add-on/AuraLayerManager.dll',
             pf / 'ASUS/AURA lighting effect add-on/LM_Support.exe', Path(os.environ['ProgramFiles']) / 'ASUS/AuraSDK/AuraSdk_x64.dll']
    provenance = dict(files=[file_metadata(p) for p in files], comRegistry=registry_metadata(),
                      sdkEnumerationAttempted=False, vendorBinaryModificationAttempted=False)
    (OUT / 'provenance.json').write_text(json.dumps(provenance, indent=2), encoding='utf-8')
    configs = []
    for p in (pd / 'ASUS/RogAura30/GetDeviceCap.xml', pd / 'ASUS/RogAura30/GetDeviceStatus.xml',
              pd / 'ASUS/RogAura30/QueryAllDevice.xml', pf / 'LightingService/DevLastStatConfig.xml'):
        row = dict(path=str(p), state='Unavailable', sha256=None, payload=None)
        if p.is_file():
            data = p.read_bytes()
            if len(data) > 1024*1024: row['state'] = 'TooLarge'
            else:
                payload = data.decode('utf-8-sig'); xml_root(payload)
                row.update(state='Detected', sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), payload=payload,
                           confidence='ConfigExact', topology=device_descriptors(payload, p.name, 'ConfigExact'))
        configs.append(row)
    (OUT / 'configuration-observation.json').write_text(json.dumps(configs, indent=2), encoding='utf-8')
    service = json.loads((OUT / 'service-mediator-probe.json').read_text(encoding='utf-8'))
    wdl = json.loads((OUT / 'wdl-observation.json').read_text(encoding='utf-8'))
    observed = next((r for r in wdl['records'] if r.get('stage') == 'Wdl'), None)
    wdl_discovery_valid = wdl['exitCode'] == 0 and not wdl['timeout'] and observed is not None and observed['state'] == 'Succeeded'
    valid = len(service['trials']) == 3 and all(t['result']['outcome'] == 'Completed' and t['result']['activation']['state'] == 'Succeeded'
        and all(q['state'] == 'Succeeded' for q in t['result']['queries']) for t in service['trials'])
    candidates = [dict(name='LightingServiceMediator', readOnlyValidated=valid, perDeviceAddressingValidated=False,
                       outputProtocolValidated=False, restoreValidated=False, reason='Topology/status XML verified; write protocol/restoration not validated'),
                  dict(name='LightingServiceSharedMemory', readOnlyValidated=False, outputProtocolValidated=False,
                       restoreValidated=False, reason='No live mapping; current LMCAP protocol is desktop capture input'),
                  dict(name='WindowsDynamicLighting', readOnlyValidated=wdl_discovery_valid, discoveryLevel='DeviceInformationInterfacesOnly', outputProtocolValidated=False,
                       restoreValidated=False, reason='PnP LampArray interface discovered; opening/control/restoration not attempted')]
    aura_file = provenance['files'][-1]
    aura_registered = any(r['clsid'] == '{05921124-5057-483E-A037-E9497B523590}' and r['state'] == 'Detected' for r in provenance['comRegistry'])
    decision = choose_backend(candidates, aura_file['state'] == 'Detected', aura_registered); decision['candidates'] = candidates
    if aura_file['sha256'] != '0a94593c8c7f4e1af99abed0afe2803a6d615318ebe96d85529eb232f6a8b0af':
        decision['auraSdk']['runtime'] = 'CompatibilityUnknown; captured M2.7 binary identity differs'
    decision['topology'] = service['trials'][0]['result']['queries'][2].get('topology', []) if valid else []
    decision['queryConfigurationCorrespondence'] = []
    for q in service['trials'][0]['result']['queries']:
        filename = {3:'GetDeviceStatus.xml',4:'GetDeviceCap.xml',63:'QueryAllDevice.xml'}[q['dispid']]
        config = next(r for r in configs if Path(r['path']).name == filename)
        if config['payload'] and q.get('payload'):
            decision['queryConfigurationCorrespondence'].append(dict(dispid=q['dispid'], configuration=filename,
                canonicalXmlEqual=canonical_xml(q['payload']) == canonical_xml(config['payload'])))
    settings = []
    for name in ('AmbientLightingEnabled', 'IsLampArrayEnabled', 'Brightness', 'EffectType', 'EffectMode', 'UseSystemAccentColor'):
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, 'Software\\Microsoft\\Lighting', 0, winreg.KEY_READ) as key:
                value, kind = winreg.QueryValueEx(key, name)
            settings.append(dict(name=name, state='Detected', registryType=kind, value=value if kind == winreg.REG_DWORD else None))
        except FileNotFoundError: settings.append(dict(name=name, state='Unavailable', value=None))
        except PermissionError: settings.append(dict(name=name, state='PermissionDenied', value=None))
    wdl['registrySettings'] = settings
    wdl['pnpIdentityEvidence'] = [json.loads((OUT / 'raw' / name).read_text(encoding='utf-8-sig'))
        for name in ('wdl-pnp-properties.json', 'wdl-usb-parent.json') if (OUT / 'raw' / name).is_file()]
    (OUT / 'wdl-observation.json').write_text(json.dumps(wdl, indent=2), encoding='utf-8')
    decision['lampInterfaces'] = wdl_descriptors(observed['devices']) if observed and observed['state'] == 'Succeeded' else None
    decision['crossSourceMerge'] = 'NotPerformed; mediator has no Windows interface ID/container ID'
    decision['minimalRuntimeStatus'] = 'Unknown; ArmouryCrate.Service/UserSessionHelper remain running; no component stopped'
    services = json.loads((OUT / 'services-before.json').read_text(encoding='utf-8-sig'))
    decision['lightingServicePid'] = next((r['ProcessId'] for r in services if r['Name'] == 'LightingService'), None)
    (OUT / 'backend-decision.json').write_text(json.dumps(decision, indent=2), encoding='utf-8')
    print(decision['status'], 'metadata preference:', decision['preferredMetadataBackend'])


if __name__ == '__main__': main()
