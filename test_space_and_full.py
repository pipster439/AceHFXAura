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

def set_color(lid, r, g, b):
    if 0 <= lid < TOTAL_LEDS:
        rgb_buf[lid * 3 + 0] = r
        rgb_buf[lid * 3 + 1] = g
        rgb_buf[lid * 3 + 2] = b

# 1. ESC: 纯红 (Red)
set_color(1, 255, 0, 0)

# 2. WASD: 纯绿 (Green)
set_color(18, 0, 255, 0) # W
set_color(11, 0, 255, 0) # A
set_color(19, 0, 255, 0) # S
set_color(27, 0, 255, 0) # D

# 3. 4 个方向键: 纯黄 (Yellow) - 100% 确认！
set_color(108, 255, 255, 0) # UP
set_color(109, 255, 255, 0) # DOWN
set_color(101, 255, 255, 0) # LEFT
set_color(117, 255, 255, 0) # RIGHT

# 4. 空格键 3 个绝对候选:
set_color(52, 255, 0, 0)   # 52: 纯红 (Red)
set_color(53, 0, 255, 0)   # 53: 纯绿 (Green)
set_color(54, 0, 150, 255) # 54: 冰蓝 (Blue / Cyan)

fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

print("[*] Streaming calibrated keys (WASD=Green, ESC=Red, Arrows=Yellow, Space candidates: 52=Red, 53=Green, 54=IceBlue) for 8 seconds...")
start_time = time.time()
frame_count = 0

while time.time() - start_time < 8.0:
    res = fn_set_single(pDev, ctypes.byref(rgb_buf))
    frame_count += 1
    time.sleep(0.04)

print(f"[+] Done! Sent {frame_count} frames.")
ole32.CoUninitialize()
