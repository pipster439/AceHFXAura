import ctypes
from ctypes import wintypes
import time
import json
import os

ole32 = ctypes.oledll.ole32

class GUID(ctypes.Structure):
    _fields_ = [
        ('Data1', wintypes.DWORD),
        ('Data2', wintypes.WORD),
        ('Data3', wintypes.WORD),
        ('Data4', wintypes.BYTE * 8)
    ]

def to_guid(s):
    g = GUID()
    ole32.CLSIDFromString(ctypes.c_wchar_p(s), ctypes.byref(g))
    return g

ole32.CoInitialize(None)

CLSID_ClaymoreHal = '{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}'
IID_IAsusAacLedDeviceHal = '{F2C8D5B4-3854-4325-8A4F-FD7C5072E3BA}'

pHal = ctypes.c_void_p()
hr = ole32.CoCreateInstance(
    ctypes.byref(to_guid(CLSID_ClaymoreHal)),
    None,
    1,
    ctypes.byref(to_guid(IID_IAsusAacLedDeviceHal)),
    ctypes.byref(pHal)
)

if hr != 0:
    print(f"[-] CoCreateInstance failed: 0x{hr:X}")
    exit(1)

hal_vtable = ctypes.cast(
    ctypes.cast(pHal.value, ctypes.POINTER(ctypes.c_void_p))[0],
    ctypes.POINTER(ctypes.c_void_p)
)

# 1. Access()
print("[*] Calling HAL Access()...")
ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p)(hal_vtable[4])(pHal.value)

# 2. CreateLedDevice
class StdVector(ctypes.Structure):
    _fields_ = [("first", ctypes.c_void_p), ("last", ctypes.c_void_p), ("end", ctypes.c_void_p)]

device_storage = (ctypes.c_void_p * 16)()
vec = StdVector(
    ctypes.addressof(device_storage),
    ctypes.addressof(device_storage),
    ctypes.addressof(device_storage) + 16 * 8
)
ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.POINTER(StdVector))(hal_vtable[5])(
    pHal.value, ctypes.byref(vec)
)

dev_count = (vec.last - vec.first) // 8 if vec.last and vec.first else 0
print(f"[+] Found {dev_count} devices in HAL")
pDev = device_storage[0]
dev_vtable = ctypes.cast(
    ctypes.cast(pDev, ctypes.POINTER(ctypes.c_void_p))[0],
    ctypes.POINTER(ctypes.c_void_p)
)

# Load 74-key keymap
keymap_path = r"g:\Aura\falchion_keymap.json"
with open(keymap_path, 'r', encoding='utf-8') as f:
    keymap = json.load(f)

led_ids = [entry['led_id'] for entry in keymap.values()]
key_count = len(led_ids)

print(f"[+] Populating {key_count} keys into device object at [pDev + 0x6C] and [pDev + 0x74]...")

# Directly set key_count at [pDev + 0x6C]
ctypes.cast(pDev + 0x6C, ctypes.POINTER(wintypes.DWORD))[0] = key_count

# Directly write LED_IDs into [pDev + 0x74]
led_id_table = ctypes.cast(pDev + 0x74, ctypes.POINTER(ctypes.c_ubyte))
for i, lid in enumerate(led_ids):
    led_id_table[i] = lid

# Map LED_ID to index
led_id_to_idx = {lid: i for i, lid in enumerate(led_ids)}

# Prepare RGB buffer: key_count * 3 bytes
rgb_buf = (ctypes.c_ubyte * (key_count * 3))()

def set_key_color(led_id, r, g, b):
    if led_id in led_id_to_idx:
        idx = led_id_to_idx[led_id]
        rgb_buf[idx * 3 + 0] = r
        rgb_buf[idx * 3 + 1] = g
        rgb_buf[idx * 3 + 2] = b

# ESC: Red
set_key_color(1, 255, 0, 0)

# WASD: Green
for lid in [18, 32, 33, 34]:
    set_key_color(lid, 0, 255, 0)

# SPACE: Ice Blue
set_key_color(70, 0, 200, 255)

# ARROWS: Yellow
for lid in [92, 87, 86, 93]:
    set_key_color(lid, 255, 255, 0)

# Method 19: Set_L_STD_SINGLE_XY
fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

print("[*] Streaming per-key frames (WASD=Green, ESC=Red, SPACE=IceBlue, ARROWS=Yellow)...")
start_time = time.time()
frame_count = 0

while time.time() - start_time < 5.0:
    res = fn_set_single(pDev, ctypes.byref(rgb_buf))
    frame_count += 1
    time.sleep(0.04)

print(f"[+] Streaming finished! Total {frame_count} frames sent.")

ole32.CoUninitialize()
print("[+] Finished cleanly.")
