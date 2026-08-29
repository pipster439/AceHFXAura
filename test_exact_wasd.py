import ctypes
from ctypes import wintypes
import time

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

pDev = device_storage[0]
dev_vtable = ctypes.cast(
    ctypes.cast(pDev, ctypes.POINTER(ctypes.c_void_p))[0],
    ctypes.POINTER(ctypes.c_void_p)
)

# Full physical LED_ID layout from fp_3 config
# Exactly mapped:
ALL_PHYSICAL_LEDS = [
    1, 6, 7, 9, 17, 25, 33, 41, 49, 57, 65, 73, 81, 55, 70,
    2, 14, 15, 10, 18, 26, 34, 42, 50, 58, 66, 74, 82, 62, 71,
    3, 22, 23, 11, 19, 27, 35, 43, 51, 59, 67, 75, 63, 94,
    4, 31, 12, 13, 20, 28, 36, 44, 52, 60, 68, 84, 87, 95,
    5, 38, 39, 29, 61, 69, 85, 86, 93, 92
]

key_count = len(ALL_PHYSICAL_LEDS)
ctypes.cast(pDev + 0x6C, ctypes.POINTER(wintypes.DWORD))[0] = key_count

led_id_table = ctypes.cast(pDev + 0x74, ctypes.POINTER(ctypes.c_ubyte))
for i, lid in enumerate(ALL_PHYSICAL_LEDS):
    led_id_table[i] = lid

led_id_to_idx = {lid: i for i, lid in enumerate(ALL_PHYSICAL_LEDS)}

rgb_buf = (ctypes.c_ubyte * (key_count * 3))()

def set_color(led_id, r, g, b):
    if led_id in led_id_to_idx:
        idx = led_id_to_idx[led_id]
        rgb_buf[idx * 3 + 0] = r
        rgb_buf[idx * 3 + 1] = g
        rgb_buf[idx * 3 + 2] = b

# ESC: Red
set_color(1, 255, 0, 0)

# WASD: Green
set_color(18, 0, 255, 0)  # W
set_color(11, 0, 255, 0)  # A
set_color(19, 0, 255, 0)  # S
set_color(27, 0, 255, 0)  # D

# SPACE: Ice Blue
set_color(38, 0, 200, 255)

# ARROWS: Yellow
set_color(92, 255, 255, 0)  # UP
set_color(86, 255, 255, 0)  # LEFT
set_color(87, 255, 255, 0)  # DOWN
set_color(95, 255, 255, 0)  # RIGHT

# Method 19: Set_L_STD_SINGLE_XY
fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

print("[*] Streaming EXACT mapping (WASD=Green, ESC=Red, SPACE=IceBlue, ARROWS=Yellow) for 6 seconds...")
start_time = time.time()
frame_count = 0

while time.time() - start_time < 6.0:
    res = fn_set_single(pDev, ctypes.byref(rgb_buf))
    frame_count += 1
    time.sleep(0.04)

print(f"[+] Finished! Sent {frame_count} frames.")
ole32.CoUninitialize()
