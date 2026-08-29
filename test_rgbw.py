import ctypes
from ctypes import wintypes
import time

ole32 = ctypes.oledll.ole32

class GUID(ctypes.Structure):
    _fields_ = [('Data1', wintypes.DWORD), ('Data2', wintypes.WORD), ('Data3', wintypes.WORD), ('Data4', wintypes.BYTE * 8)]
def to_guid(s):
    g = GUID()
    ole32.CLSIDFromString(ctypes.c_wchar_p(s), ctypes.byref(g))
    return g

ole32.CoInitialize(None)
CLSID_ClaymoreHal = '{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}'
IID_IAsusAacLedDeviceHal = '{F2C8D5B4-3854-4325-8A4F-FD7C5072E3BA}'

pHal = ctypes.c_void_p()
ole32.CoCreateInstance(ctypes.byref(to_guid(CLSID_ClaymoreHal)), None, 1, ctypes.byref(to_guid(IID_IAsusAacLedDeviceHal)), ctypes.byref(pHal))
hal_vtable = ctypes.cast(ctypes.cast(pHal.value, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p)(hal_vtable[4])(pHal.value)

class StdVector(ctypes.Structure):
    _fields_ = [("first", ctypes.c_void_p), ("last", ctypes.c_void_p), ("end", ctypes.c_void_p)]
device_storage = (ctypes.c_void_p * 16)()
vec = StdVector(ctypes.addressof(device_storage), ctypes.addressof(device_storage), ctypes.addressof(device_storage) + 16 * 8)
ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.POINTER(StdVector))(hal_vtable[5])(pHal.value, ctypes.byref(vec))

pDev = device_storage[0]
dev_vtable = ctypes.cast(ctypes.cast(pDev, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))

TOTAL_LEDS = 128
ctypes.cast(pDev + 0x6C, ctypes.POINTER(wintypes.DWORD))[0] = TOTAL_LEDS
led_id_table = ctypes.cast(pDev + 0x74, ctypes.POINTER(ctypes.c_ubyte))
for i in range(TOTAL_LEDS):
    led_id_table[i] = i

rgb_buf = (ctypes.c_ubyte * (TOTAL_LEDS * 3))()

# 4 DISTINCT UNMISTAKABLE COLORS:
COLOR_RED   = (255, 0, 0)     # 纯红
COLOR_GREEN = (0, 255, 0)     # 纯绿
COLOR_BLUE  = (0, 0, 255)     # 纯蓝
COLOR_WHITE = (255, 255, 255) # 纯白

def set_color(lid, c):
    rgb_buf[lid * 3 + 0] = c[0]
    rgb_buf[lid * 3 + 1] = c[1]
    rgb_buf[lid * 3 + 2] = c[2]

# Group 1: 0..3 -> Red
# Group 2: 4..7 -> Green
# Group 3: 8..11 -> Blue
# Group 4: 12..15 -> White

# 1. Space candidates (48..63):
for lid in range(48, 52):   set_color(lid, COLOR_RED)
for lid in range(52, 56):   set_color(lid, COLOR_GREEN)
for lid in range(56, 60):   set_color(lid, COLOR_BLUE)
for lid in range(60, 64):   set_color(lid, COLOR_WHITE)

# 2. Up/Down/Left candidates (96..111):
for lid in range(96, 100):  set_color(lid, COLOR_RED)
for lid in range(100, 104): set_color(lid, COLOR_GREEN)
for lid in range(104, 108): set_color(lid, COLOR_BLUE)
for lid in range(108, 112): set_color(lid, COLOR_WHITE)

# 3. Right Arrow candidates (112..127):
for lid in range(112, 116): set_color(lid, COLOR_RED)
for lid in range(116, 120): set_color(lid, COLOR_GREEN)
for lid in range(120, 124): set_color(lid, COLOR_BLUE)
for lid in range(124, 128): set_color(lid, COLOR_WHITE)

fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

print("[*] Probing with only 4 basic colors (Red, Green, Blue, White) for 8 seconds...")
start_time = time.time()
frame_count = 0

while time.time() - start_time < 8.0:
    res = fn_set_single(pDev, ctypes.byref(rgb_buf))
    frame_count += 1
    time.sleep(0.04)

print(f"[+] Probing done! Sent {frame_count} frames.")
ole32.CoUninitialize()
