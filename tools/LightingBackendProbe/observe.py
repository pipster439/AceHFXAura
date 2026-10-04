"""Fixed-name passive MMF observer. FILE_MAP_READ, copied bytes and scoped view only."""
import ctypes as c
from ctypes import wintypes as w
import json
import time
from datetime import datetime, timezone
from pathlib import Path
from model import MAX_MAPPING, frame_sample, sample_summary

FILE_MAP_READ = 4
SECTION_QUERY = 1
READ_CONTROL = 0x20000


def observe_windows():
    k = c.WinDLL('kernel32', use_last_error=True)
    a = c.WinDLL('advapi32', use_last_error=True)
    n = c.WinDLL('ntdll')
    k.OpenFileMappingW.argtypes = [w.DWORD, w.BOOL, w.LPCWSTR]; k.OpenFileMappingW.restype = w.HANDLE
    k.OpenEventW.argtypes = [w.DWORD, w.BOOL, w.LPCWSTR]; k.OpenEventW.restype = w.HANDLE
    k.MapViewOfFile.argtypes = [w.HANDLE, w.DWORD, w.DWORD, w.DWORD, c.c_size_t]; k.MapViewOfFile.restype = c.c_void_p
    k.UnmapViewOfFile.argtypes = [c.c_void_p]; k.UnmapViewOfFile.restype = w.BOOL
    k.CloseHandle.argtypes = [w.HANDLE]; k.CloseHandle.restype = w.BOOL
    k.LocalFree.argtypes = [c.c_void_p]; k.LocalFree.restype = c.c_void_p
    a.GetSecurityInfo.argtypes = [w.HANDLE, c.c_int, w.DWORD, c.c_void_p, c.c_void_p, c.c_void_p, c.c_void_p, c.POINTER(c.c_void_p)]
    a.GetSecurityInfo.restype = w.DWORD
    a.ConvertSecurityDescriptorToStringSecurityDescriptorW.argtypes = [c.c_void_p, w.DWORD, w.DWORD, c.POINTER(w.LPWSTR), c.POINTER(w.DWORD)]
    a.ConvertSecurityDescriptorToStringSecurityDescriptorW.restype = w.BOOL
    class Section(c.Structure):
        _fields_ = [('base', c.c_void_p), ('attributes', w.ULONG), ('maximumSize', c.c_longlong)]
    n.NtQuerySection.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.ULONG, c.POINTER(w.ULONG)]; n.NtQuerySection.restype = w.LONG

    def acl(handle):
        sd = c.c_void_p()
        error = a.GetSecurityInfo(handle, 6, 7, None, None, None, None, c.byref(sd))
        if error:
            return dict(state='PermissionDenied' if error == 5 else 'Failed', win32Error=error, sddl=None)
        text = w.LPWSTR(); length = w.DWORD()
        try:
            if not a.ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, 1, 7, c.byref(text), c.byref(length)):
                return dict(state='Failed', win32Error=c.get_last_error(), sddl=None)
            try:
                return dict(state='Succeeded', sddl=text.value)
            finally:
                k.LocalFree(text)
        finally:
            k.LocalFree(sd)

    rows = []
    for prefix in ('Local\\', 'Global\\'):
        name = prefix + 'LMCAP_FRAME'
        row = dict(name=name, startedAt=datetime.now(timezone.utc).isoformat(), accessMask=FILE_MAP_READ | SECTION_QUERY,
                   mappingBytes=None, header=None, samples=[], state='NotAttempted',
                   passiveSamplingState='NotAttempted',
                   protocolRole='DesktopCaptureInput; not validated RGB output', writes=0,
                   feedback='NotAttempted', mappingRecreation='NotAttempted; no service/topology manipulation')
        event = k.OpenEventW(READ_CONTROL, False, prefix + 'LMCAP_FRAME_READY')
        if event:
            try:
                row['event'] = dict(state='Detected', accessMask=READ_CONTROL, acl=acl(event), waitOrSignalAttempted=False)
            finally:
                k.CloseHandle(event)
        else:
            error = c.get_last_error()
            row['event'] = dict(state='PermissionDenied' if error == 5 else 'Unavailable', win32Error=error, waitOrSignalAttempted=False)
        handle = k.OpenFileMappingW(FILE_MAP_READ | SECTION_QUERY | READ_CONTROL, False, name)
        if handle:
            row['acl'] = acl(handle)
        else:
            row['acl'] = dict(state='PermissionDenied' if c.get_last_error() == 5 else 'Unavailable', win32Error=c.get_last_error(), sddl=None)
            handle = k.OpenFileMappingW(FILE_MAP_READ | SECTION_QUERY, False, name)
        if not handle:
            error = c.get_last_error()
            row['state'] = 'PermissionDenied' if error == 5 else 'Unavailable'
            row['win32Error'] = error; rows.append(row); continue
        view = None
        try:
            section = Section(); returned = w.ULONG()
            status = n.NtQuerySection(handle, 0, c.byref(section), c.sizeof(section), c.byref(returned))
            row['sectionQueryNtstatus'] = status
            if status < 0 or not 64 <= section.maximumSize <= MAX_MAPPING:
                row['state'] = 'InvalidMappingSize'; continue
            size = section.maximumSize; row['mappingBytes'] = size
            view = k.MapViewOfFile(handle, FILE_MAP_READ, 0, 0, size)
            if not view:
                row['state'] = 'Failed'; row['win32Error'] = c.get_last_error(); continue
            def read(offset, count):
                if offset < 0 or count < 0 or count > size or offset > size - count:
                    raise ValueError('ReadOutOfBounds')
                return c.string_at(view + offset, count)
            start = time.monotonic()
            for index in range(100):
                if time.monotonic() >= start + 5:
                    break
                sample = frame_sample(read, size)
                sample['elapsedMs'] = (time.monotonic() - start) * 1000
                row['samples'].append(sample)
                time.sleep(max(0, min(start + 5, start + (index + 1) * .05) - time.monotonic()))
            row['header'] = row['samples'][0]['header']; row['state'] = 'Succeeded'
            row['passiveSamplingState'] = 'Succeeded'
            row['summary'] = sample_summary(row['samples'])
        except (ValueError, OSError) as error:
            row['state'] = 'Failed'; row['diagnostic'] = str(error)
        finally:
            if view:
                k.UnmapViewOfFile(view)
            k.CloseHandle(handle)
            row['endedAt'] = datetime.now(timezone.utc).isoformat(); rows.append(row)
    return dict(observationMode='ReadOnly', maximumObservationSeconds=5, maximumSamplesPerSecond=20,
                expectedMagic=0x46434D4C, expectedVersion=1, headerContractEvidence='Static current AuraLayerManager/LM_Support; live fields remain nullable', rows=rows,
                visualCorrelation='NotAttempted; no human observation supplied')


if __name__ == '__main__':
    path = Path(__file__).resolve().parents[2] / 'audit_artifacts/phase3-m2.8/lmcap-observation.json'
    path.write_text(json.dumps(observe_windows(), indent=2), encoding='utf-8')
    print('Saved fixed-name read-only MMF observation')
