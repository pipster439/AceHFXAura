# =============================================================================
# 提示: 本脚本为历史阶段性验证/测试工具，包含独立的 ctypes/COM 虚表直调实现。
# 生产与维护环境推荐统一使用官方封装模块 tools.py.aura_hal (tools/py/aura_hal.py)，
# 该模块提供完整的 COM 生命周期管理、边界防御与权威实测键位映射。
# =============================================================================
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

if dev_count == 0:
    print("[-] No device found.")
    exit(1)

pDev = device_storage[0]
dev_vtable = ctypes.cast(
    ctypes.cast(pDev, ctypes.POINTER(ctypes.c_void_p))[0],
    ctypes.POINTER(ctypes.c_void_p)
)

# Inspect key count in [pDev + 0x6C]
key_count = ctypes.cast(pDev + 0x6C, ctypes.POINTER(wintypes.DWORD))[0]
print(f"[+] Keyboard key_count in device object: {key_count}")

# Read key table of LED_IDs at [pDev + 0x74]
led_id_table = ctypes.cast(pDev + 0x74, ctypes.POINTER(ctypes.c_ubyte))
table_led_ids = [led_id_table[i] for i in range(key_count)]
print(f"[+] First 20 LED_IDs in hardware table: {table_led_ids[:20]}")

# Load keymap
keymap_path = r"g:\Aura\falchion_keymap.json"
keymap = {}
if os.path.exists(keymap_path):
    with open(keymap_path, 'r', encoding='utf-8') as f:
        keymap = json.load(f)

# Map LED_ID to index in hardware table
led_id_to_idx = {led_id: idx for idx, led_id in enumerate(table_led_ids)}

# Prepare RGB buffer: key_count * 3 bytes (R, G, B for each entry in hardware table)
total_keys = max(key_count, 100)
rgb_buf = (ctypes.c_ubyte * (total_keys * 3))()

# Function to set RGB for an LED_ID
def set_led_color(led_id, r, g, b):
    if led_id in led_id_to_idx:
        idx = led_id_to_idx[led_id]
        rgb_buf[idx * 3 + 0] = r
        rgb_buf[idx * 3 + 1] = g
        rgb_buf[idx * 3 + 2] = b

# ESC: LED_ID 1 -> Pure Red
set_led_color(1, 255, 0, 0)

# WASD:
# W: 18, A: 32, S: 33, D: 34 -> Pure Green
for led_id in [18, 32, 33, 34]:
    set_led_color(led_id, 0, 255, 0)

# SPACE: 70 -> Pure Ice Blue
set_led_color(70, 0, 200, 255)

# ARROWS:
# UP: 92, DOWN: 87, LEFT: 86, RIGHT: 93 -> Pure Yellow
for led_id in [92, 87, 86, 93]:
    set_led_color(led_id, 255, 255, 0)

# Method 19: Set_L_STD_SINGLE_XY
fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

print("[*] Streaming per-key frames (WASD=Green, ESC=Red, SPACE=IceBlue, ARROWS=Yellow) for 6 seconds...")
start_time = time.time()
frame_count = 0

while time.time() - start_time < 6.0:
    res = fn_set_single(pDev, ctypes.byref(rgb_buf))
    frame_count += 1
    time.sleep(0.04)  # ~25 FPS

print(f"[+] Done streaming! Sent {frame_count} frames.")

ole32.CoUninitialize()
print("[+] Exited cleanly.")
