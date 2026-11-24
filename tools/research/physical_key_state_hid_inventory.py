"""Present HID TLC metadata inventory, including keyboard and no-input TLCs.

Explicit optional companion to physical_key_state_metadata.py. Only cached
collection descriptors are requested. No ReadFile, GetInputReport, GetFeature,
WriteFile, SetFeature, Raw Input registration, HAL call or device command exists.
All handles request access 0, share READ|WRITE and are closed in finally blocks.
"""

from __future__ import annotations

import ctypes as c
import sys
from ctypes import wintypes as w

from physical_key_state_metadata import Caps, MAX_CAPS, MAX_DEVICES, MAX_METADATA
from physical_key_state_metadata import SUCCESS, ERROR, decode_capability, target_identity


class Guid(c.Structure):
    _fields_ = [("d1", w.DWORD), ("d2", w.WORD), ("d3", w.WORD), ("d4", c.c_ubyte * 8)]


class InterfaceData(c.Structure):
    _fields_ = [("size", w.DWORD), ("guid", Guid), ("flags", w.DWORD), ("reserved", c.c_size_t)]


def collect_all_hid_cached() -> dict:
    if sys.platform != "win32":
        raise RuntimeError("Windows cached metadata API required")
    if c.sizeof(Caps) != 64:
        raise RuntimeError("unexpected HIDP_CAPS ABI")
    setup = c.WinDLL("setupapi", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    hid = c.WinDLL("hid", use_last_error=True)
    hid.HidD_GetHidGuid.argtypes = [c.POINTER(Guid)]
    setup.SetupDiGetClassDevsW.argtypes = [c.POINTER(Guid), w.LPCWSTR, w.HWND, w.DWORD]
    setup.SetupDiGetClassDevsW.restype = w.HANDLE
    setup.SetupDiEnumDeviceInterfaces.argtypes = [w.HANDLE, c.c_void_p, c.POINTER(Guid), w.DWORD, c.POINTER(InterfaceData)]
    setup.SetupDiEnumDeviceInterfaces.restype = w.BOOL
    setup.SetupDiGetDeviceInterfaceDetailW.argtypes = [w.HANDLE, c.POINTER(InterfaceData), c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p]
    setup.SetupDiGetDeviceInterfaceDetailW.restype = w.BOOL
    setup.SetupDiDestroyDeviceInfoList.argtypes = [w.HANDLE]
    setup.SetupDiDestroyDeviceInfoList.restype = w.BOOL
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.CloseHandle.restype = w.BOOL
    hid.HidD_GetPreparsedData.argtypes = [w.HANDLE, c.POINTER(c.c_void_p)]
    hid.HidD_GetPreparsedData.restype = c.c_ubyte
    hid.HidD_FreePreparsedData.argtypes = [c.c_void_p]
    hid.HidD_FreePreparsedData.restype = c.c_ubyte
    hid.HidP_GetCaps.argtypes = [c.c_void_p, c.POINTER(Caps)]
    hid.HidP_GetCaps.restype = w.LONG
    for name in ("HidP_GetButtonCaps", "HidP_GetValueCaps"):
        api = getattr(hid, name)
        api.argtypes = [c.c_int, c.c_void_p, c.POINTER(w.USHORT), c.c_void_p]
        api.restype = w.LONG
    guid = Guid()
    hid.HidD_GetHidGuid(c.byref(guid))
    invalid = c.c_void_p(-1).value
    info_set = setup.SetupDiGetClassDevsW(c.byref(guid), None, None, 0x12)  # PRESENT | DEVICEINTERFACE
    if info_set == invalid or info_set is None:
        raise RuntimeError("HID class enumeration failed")
    rows = []
    opened = 0
    failures = 0
    try:
        for index in range(MAX_DEVICES):
            interface = InterfaceData()
            interface.size = c.sizeof(interface)
            if not setup.SetupDiEnumDeviceInterfaces(info_set, None, c.byref(guid), index, c.byref(interface)):
                if c.get_last_error() == 259:  # ERROR_NO_MORE_ITEMS
                    break
                raise RuntimeError("HID interface enumeration failed")
            size = w.DWORD()
            setup.SetupDiGetDeviceInterfaceDetailW(info_set, c.byref(interface), None, 0, c.byref(size), None)
            if not 8 <= size.value <= MAX_METADATA:
                failures += 1
                continue
            detail = (w.DWORD * ((size.value + 3) // 4))()
            detail[0] = 8 if c.sizeof(c.c_void_p) == 8 else 6
            if not setup.SetupDiGetDeviceInterfaceDetailW(info_set, c.byref(interface), detail, size.value, None, None):
                failures += 1
                continue
            # DevicePath starts at byte 4, independently of the cbSize ABI value.
            path = c.wstring_at(c.addressof(detail) + 4)
            identity = target_identity(path)
            if identity is None:
                continue
            row = dict(identity)
            rows.append(row)
            handle = kernel.CreateFileW(path, 0, 3, None, 3, 0, None)  # ACCESS=0; SHARE_READ|WRITE; OPEN_EXISTING
            if handle == invalid or handle is None:
                row["error"] = "metadata_handle_unavailable"
                continue
            opened += 1
            preparsed = c.c_void_p()
            try:
                if not hid.HidD_GetPreparsedData(handle, c.byref(preparsed)):
                    row["error"] = "cached_preparsed_data_unavailable"
                    continue
                caps = Caps()
                status = hid.HidP_GetCaps(preparsed, c.byref(caps)) & ERROR
                row["caps_status"] = f"0x{status:08x}"
                if status != SUCCESS:
                    continue
                row.update({name: int(getattr(caps, name)) for name in (
                    "usage", "usage_page", "input_bytes", "output_bytes", "feature_bytes", "links"
                )})
                row["capabilities"] = []
                for report_type, direction in enumerate(("input", "output", "feature")):
                    for kind, suffix, api in (("button", "buttons", "HidP_GetButtonCaps"), ("value", "values", "HidP_GetValueCaps")):
                        requested = int(getattr(caps, f"{direction}_{suffix}"))
                        if not requested:
                            continue
                        if requested > MAX_CAPS:
                            row.setdefault("errors", []).append(f"{direction}_{kind}_count_exceeds_bound")
                            continue
                        buffer = (w.DWORD * (18 * requested))()
                        length = w.USHORT(requested)
                        status = getattr(hid, api)(report_type, buffer, c.byref(length), preparsed) & ERROR
                        if status != SUCCESS or length.value > requested:
                            row.setdefault("errors", []).append(f"{direction}_{kind}_caps_unavailable")
                            continue
                        blob = bytes(buffer)
                        for cap_index in range(length.value):
                            item = decode_capability(blob[cap_index * 72:(cap_index + 1) * 72], kind)
                            row["capabilities"].append(dict(item, direction=direction))
            finally:
                if preparsed.value:
                    if not hid.HidD_FreePreparsedData(preparsed):
                        row.setdefault("errors", []).append("metadata_free_failed")
                if not kernel.CloseHandle(handle):
                    row.setdefault("errors", []).append("metadata_handle_close_failed")
        else:
            raise RuntimeError("HID interface count exceeds bound")
    finally:
        if not setup.SetupDiDestroyDeviceInfoList(info_set):
            raise RuntimeError("metadata_info_set_cleanup_failed")
    rows.sort(key=lambda row: (row["interface"], row["collection"] or ""))
    return {
        "schema_version": 1, "source": "Present HID class cached preparsed capabilities",
        "vid": "0x0B05", "pid": "0x1B7E", "collections": rows,
        "coverage": "Present SetupAPI HID interfaces; target-only metadata handles",
        "unreadable_interface_details": failures,
        "metadata_handles_opened": opened, "metadata_handle_access": 0,
        "metadata_handle_share": "READ|WRITE", "input_events_observed": 0,
        "raw_reports_retained": 0, "raw_input_registrations": 0,
    }
