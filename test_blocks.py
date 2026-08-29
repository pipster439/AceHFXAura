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

# 8 distinct block colors:
BLOCK_COLORS = [
    (255, 0, 0),     # Block 0 (0..15):   纯红 (Red)
    (0, 255, 0),     # Block 1 (16..31):  纯绿 (Green)
    (0, 0, 255),     # Block 2 (32..47):  纯蓝 (Blue)
    (255, 255, 0),   # Block 3 (48..63):  黄色 (Yellow)
    (255, 0, 255),   # Block 4 (64..79):  紫色 (Purple / Magenta)
    (0, 255, 255),   # Block 5 (80..95):  青色 (Cyan)
    (255, 255, 255), # Block 6 (96..111): 纯白 (White)
    (255, 128, 0),   # Block 7 (112..127): 橙色 (Orange)
]

for lid in range(TOTAL_LEDS):
    block_idx = lid // 16
    r, g, b = BLOCK_COLORS[block_idx]
    rgb_buf[lid * 3 + 0] = r
    rgb_buf[lid * 3 + 1] = g
    rgb_buf[lid * 3 + 2] = b

fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

print("[*] Probing 8 blocks of 16 LEDs with distinct colors for 8 seconds...")
start_time = time.time()
frame_count = 0

while time.time() - start_time < 8.0:
    res = fn_set_single(pDev, ctypes.byref(rgb_buf))
    frame_count += 1
    time.sleep(0.04)

print(f"[+] Probing done! Sent {frame_count} frames.")
ole32.CoUninitialize()
