# =============================================================================
# 提示: 本脚本为历史阶段性验证/测试工具，包含独立的 ctypes/COM 虚表直调实现。
# 生产与维护环境推荐统一使用官方封装模块 tools.py.aura_hal (tools/py/aura_hal.py)，
# 该模块提供完整的 COM 生命周期管理、边界防御与权威实测键位映射。
# =============================================================================
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

# Access()
ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p)(hal_vtable[4])(pHal.value)

# CreateLedDevice
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

TOTAL_LEDS = 128
ctypes.cast(pDev + 0x6C, ctypes.POINTER(wintypes.DWORD))[0] = TOTAL_LEDS

led_id_table = ctypes.cast(pDev + 0x74, ctypes.POINTER(ctypes.c_ubyte))
for i in range(TOTAL_LEDS):
    led_id_table[i] = i

rgb_buf = (ctypes.c_ubyte * (TOTAL_LEDS * 3))()

def set_led(lid, r, g, b):
    if 0 <= lid < TOTAL_LEDS:
        rgb_buf[lid * 3 + 0] = r
        rgb_buf[lid * 3 + 1] = g
        rgb_buf[lid * 3 + 2] = b

# ESC: Red
set_led(1, 255, 0, 0)

# WASD: Green
set_led(18, 0, 255, 0)  # W
set_led(11, 0, 255, 0)  # A
set_led(19, 0, 255, 0)  # S
set_led(27, 0, 255, 0)  # D

# Space candidates:
set_led(29, 255, 255, 255) # 29: White
set_led(38, 0, 255, 255)   # 38: Cyan
set_led(39, 255, 0, 255)   # 39: Purple
set_led(31, 0, 0, 255)     # 31: Blue

# Arrow / Bottom-right candidates:
set_led(84, 255, 255, 0)   # 84: Yellow
set_led(85, 255, 128, 0)   # 85: Orange
set_led(86, 255, 50, 50)   # 86: Light Red
set_led(87, 50, 255, 50)   # 87: Light Green
set_led(92, 50, 50, 255)   # 92: Light Blue
set_led(93, 255, 0, 128)   # 93: Pink
set_led(94, 255, 255, 255) # 94: White
set_led(95, 200, 255, 0)   # 95: Lime
set_led(68, 0, 255, 200)   # 68: Turquoise
set_led(70, 150, 150, 255) # 70: Lavender
set_led(71, 255, 215, 0)   # 71: Gold

fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

print("[*] Probing 128 IDs with distinct color markers for 8 seconds...")
start_time = time.time()
frame_count = 0

while time.time() - start_time < 8.0:
    res = fn_set_single(pDev, ctypes.byref(rgb_buf))
    frame_count += 1
    time.sleep(0.04)

print(f"[+] Done probing! Sent {frame_count} frames.")
ole32.CoUninitialize()
