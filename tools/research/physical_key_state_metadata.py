"""M605 cached HID capability inventory; never registers or reads input events.

Run explicitly on Windows. Output contains no device paths, serials, handles,
report payloads or key history. RIDI_PREPARSEDDATA is OS cached metadata, not a
device query. This is NOT a physical-key decoder or a capture utility.
"""

from __future__ import annotations

import ctypes as c
import argparse
import json
import re
import sys
from ctypes import wintypes as w

TARGET = re.compile(r"(?:^|[#&])vid_0b05&pid_1b7e(?=[&#])", re.I)
INTERFACE = re.compile(r"&mi_([0-9a-f]{2})(?=[&#])", re.I)
COLLECTION = re.compile(r"&col([0-9a-f]{2})(?=[&#])", re.I)
MAX_DEVICES = 4096
MAX_METADATA = 65536
MAX_CAPS = 256
SUCCESS = 0x00110000
ERROR = 0xFFFFFFFF


def target_identity(path: str) -> dict | None:
    """Strict target match; retain only non-identifying interface/TLC suffixes."""
    interface = INTERFACE.search(path)
    if not TARGET.search(path) or not interface:
        return None
    collection = COLLECTION.search(path)
    return {
        "interface": f"MI_{interface[1].upper()}",
        "collection": f"Col{collection[1].upper()}" if collection else None,
    }


def decode_capability(data: bytes, kind: str) -> dict:
    """Decode public 72-byte HIDP capability ABI, not an input report.

    Union at offset 56 is shared by HIDP_BUTTON_CAPS and HIDP_VALUE_CAPS.
    Button ReportCount at offset 16 is version-dependent, so not exported.
    """
    if len(data) != 72 or kind not in ("button", "value"):
        raise ValueError("invalid capability ABI")
    u16 = lambda offset: int.from_bytes(data[offset:offset + 2], "little")
    result = {
        "kind": kind, "usage_page": u16(0), "report_id": data[2],
        "main_item_flags": u16(4), "link_collection": u16(6),
        "is_range": bool(data[12]), "is_absolute": bool(data[15]),
        "usage_min": u16(56), "usage_max": u16(58) if data[12] else u16(56),
    }
    if kind == "value":
        result.update(bit_size=u16(18), report_count=u16(20))
    return result


class DeviceList(c.Structure):
    _fields_ = [("device", w.HANDLE), ("type", w.DWORD)]


class Caps(c.Structure):
    _fields_ = [(name, w.USHORT) for name in (
        "usage", "usage_page", "input_bytes", "output_bytes", "feature_bytes"
    )] + [("reserved", w.USHORT * 17)] + [(name, w.USHORT) for name in (
        "links", "input_buttons", "input_values", "input_data",
        "output_buttons", "output_values", "output_data",
        "feature_buttons", "feature_values", "feature_data"
    )]


def collect() -> dict:
    if sys.platform != "win32":
        raise RuntimeError("Windows cached metadata API required")
    if c.sizeof(Caps) != 64:
        raise RuntimeError("unexpected HIDP_CAPS ABI")
    user = c.WinDLL("user32", use_last_error=True)
    hid = c.WinDLL("hid", use_last_error=True)
    user.GetRawInputDeviceList.argtypes = [c.POINTER(DeviceList), c.POINTER(w.UINT), w.UINT]
    user.GetRawInputDeviceList.restype = w.UINT
    user.GetRawInputDeviceInfoW.argtypes = [w.HANDLE, w.UINT, c.c_void_p, c.POINTER(w.UINT)]
    user.GetRawInputDeviceInfoW.restype = w.UINT
    hid.HidP_GetCaps.argtypes = [c.c_void_p, c.POINTER(Caps)]
    hid.HidP_GetCaps.restype = w.LONG
    for name in ("HidP_GetButtonCaps", "HidP_GetValueCaps"):
        function = getattr(hid, name)
        function.argtypes = [c.c_int, c.c_void_p, c.POINTER(w.USHORT), c.c_void_p]
        function.restype = w.LONG
    count = w.UINT()
    if user.GetRawInputDeviceList(None, c.byref(count), c.sizeof(DeviceList)) == ERROR:
        raise RuntimeError("device enumeration failed")
    if count.value > MAX_DEVICES:
        raise RuntimeError("device count exceeds bound")
    devices = (DeviceList * count.value)()
    actual = user.GetRawInputDeviceList(devices, c.byref(count), c.sizeof(DeviceList))
    if actual == ERROR or actual > len(devices):
        raise RuntimeError("enumeration changed; rerun explicitly")
    rows = []
    skipped_names = 0
    for device in devices[:actual]:
        size = w.UINT()
        if (user.GetRawInputDeviceInfoW(device.device, 0x20000007, None, c.byref(size)) == ERROR
                or not 0 < size.value <= MAX_METADATA // 2):
            skipped_names += 1
            continue
        name = c.create_unicode_buffer(size.value)
        if user.GetRawInputDeviceInfoW(device.device, 0x20000007, name, c.byref(size)) == ERROR:
            skipped_names += 1
            continue
        identity = target_identity(name.value)
        # Names of unrelated devices are immediately discarded, never exported.
        if identity is None:
            continue
        row = dict(identity, raw_input_type=int(device.type))
        rows.append(row)
        size = w.UINT()
        if (user.GetRawInputDeviceInfoW(device.device, 0x20000005, None, c.byref(size)) == ERROR
                or not 0 < size.value <= MAX_METADATA):
            row["error"] = "cached_preparsed_data_unavailable"
            continue
        # DWORD-aligned metadata storage, also bounds the returned buffer size.
        metadata = (w.DWORD * ((size.value + 3) // 4))()
        allocated = c.sizeof(metadata)
        if (user.GetRawInputDeviceInfoW(device.device, 0x20000005, metadata, c.byref(size)) == ERROR
                or not 0 < size.value <= allocated):
            row["error"] = "cached_preparsed_data_read_failed"
            continue
        caps = Caps()
        status = hid.HidP_GetCaps(metadata, c.byref(caps)) & ERROR
        row["caps_status"] = f"0x{status:08x}"
        if status != SUCCESS:
            continue
        row.update({name: int(getattr(caps, name)) for name in (
            "usage", "usage_page", "input_bytes", "output_bytes", "feature_bytes", "links"
        )})
        row["capabilities"] = []
        for report_type, direction in enumerate(("input", "output", "feature")):
            for kind, suffix, api in (
                ("button", "buttons", "HidP_GetButtonCaps"),
                ("value", "values", "HidP_GetValueCaps"),
            ):
                requested = int(getattr(caps, f"{direction}_{suffix}"))
                if not requested:
                    continue
                if requested > MAX_CAPS:
                    row.setdefault("errors", []).append(f"{direction}_{kind}_count_exceeds_bound")
                    continue
                buffer = (w.DWORD * (18 * requested))()
                length = w.USHORT(requested)
                status = getattr(hid, api)(report_type, buffer, c.byref(length), metadata) & ERROR
                if status != SUCCESS or length.value > requested:
                    row.setdefault("errors", []).append(f"{direction}_{kind}_caps_unavailable")
                    continue
                blob = bytes(buffer)
                for index in range(length.value):
                    item = decode_capability(blob[index * 72:(index + 1) * 72], kind)
                    row["capabilities"].append(dict(item, direction=direction))
    rows.sort(key=lambda row: (row["interface"], row["collection"] or ""))
    return {
        "schema_version": 1,
        "source": "Windows Raw Input cached HID preparsed capabilities",
        "vid": "0x0B05", "pid": "0x1B7E", "collections": rows,
        "unreadable_device_names": skipped_names,
        "coverage": "Raw Input device list only; no-input TLCs may be absent",
        "input_events_observed": 0, "raw_reports_retained": 0,
        "device_handles_opened": 0, "raw_input_registrations": 0,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all-hid-cached", action="store_true", help=(
        "enumerate present HID TLCs with SetupAPI; open target metadata handles "
        "with desired access 0 and sharing READ|WRITE; no input reads or report queries"
    ))
    args = parser.parse_args()
    try:
        if args.all_hid_cached:
            from physical_key_state_hid_inventory import collect_all_hid_cached
            result = collect_all_hid_cached()
        else:
            result = collect()
        print(json.dumps(result, indent=2))
    except (OSError, RuntimeError) as error:
        # Do not export arbitrary paths/OS error messages or traceback locals.
        print(json.dumps({"error": type(error).__name__, "metadata_complete": False}))
        sys.exit(1)
