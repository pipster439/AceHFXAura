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

# 1. ESC: Red, WASD: Green
set_color(1, 255, 0, 0)
for lid in [18, 11, 19, 27]:
    set_color(lid, 0, 255, 0)

# 2. Space (candidate 55): Ice Blue (Cyan)
set_color(55, 0, 200, 255)

# 3. 4 basic colors for offset 0, 1, 2, 3
COLORS = [
    (255, 0, 0),     # 0: 纯红 (Red)
    (0, 255, 0),     # 1: 纯绿 (Green)
    (0, 0, 255),     # 2: 纯蓝 (Blue)
    (255, 255, 255), # 3: 纯白 (White)
]

# Left Arrow group (100..103):
for i in range(4):
    c = COLORS[i]
    set_color(100 + i, c[0], c[1], c[2])

# Up & Down Arrow group (108..111):
for i in range(4):
    c = COLORS[i]
    set_color(108 + i, c[0], c[1], c[2])

# Right Arrow group (116..119):
for i in range(4):
    c = COLORS[i]
    set_color(116 + i, c[0], c[1], c[2])

fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

print("[*] Streaming final calibration for Space and Arrows for 10 seconds...")
start_time = time.time()
frame_count = 0

while time.time() - start_time < 10.0:
    res = fn_set_single(pDev, ctypes.byref(rgb_buf))
    frame_count += 1
    time.sleep(0.04)

print(f"[+] Done! Sent {frame_count} frames.")
ole32.CoUninitialize()
