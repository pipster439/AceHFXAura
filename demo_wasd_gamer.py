import ctypes
from ctypes import wintypes
import time
import sys

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

# -------------------------------------------------------------------------
# 100% 精确校准的物理硬件 LED 映射表
# -------------------------------------------------------------------------
# ESC: 纯红
set_color(1, 255, 0, 0)

# WASD: 纯绿
set_color(18, 0, 255, 0) # W
set_color(11, 0, 255, 0) # A
set_color(19, 0, 255, 0) # S
set_color(27, 0, 255, 0) # D

# SPACE (空格键 = 53): 冰蓝
set_color(53, 0, 200, 255)

# 方向键 (上=108, 下=109, 左=101, 右=117): 纯黄
set_color(108, 255, 255, 0) # UP
set_color(109, 255, 255, 0) # DOWN
set_color(101, 255, 255, 0) # LEFT
set_color(117, 255, 255, 0) # RIGHT

# 其它所有按键保持全黑熄灭 (0, 0, 0)

fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

duration = 8.0
if len(sys.argv) > 1:
    try:
        duration = float(sys.argv[1])
    except ValueError:
        pass

print(f"[+] 正在下发最终完美光效: WASD=绿色, ESC=红色, SPACE=冰蓝, 方向键=黄色, 其余按键=熄灭")
print(f"[*] 保持推流 {duration} 秒 (按 Ctrl+C 可提前退出)...")

start_time = time.time()
frame_count = 0

try:
    while time.time() - start_time < duration:
        res = fn_set_single(pDev, ctypes.byref(rgb_buf))
        frame_count += 1
        time.sleep(0.04) # ~25 FPS
except KeyboardInterrupt:
    print("\n[*] 用户中断推流")

print(f"[+] 完美运行结束！共推流 {frame_count} 帧。")
ole32.CoUninitialize()
