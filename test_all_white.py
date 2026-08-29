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
ole32.CoCreateInstance(
    ctypes.byref(to_guid(CLSID_ClaymoreHal)),
    None,
    1,
    ctypes.byref(to_guid(IID_IAsusAacLedDeviceHal)),
    ctypes.byref(pHal)
)

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

# Test ALL 128 possible LED IDs turned ON (White) except WASD (Green) and ESC (Red)
TOTAL_LEDS = 128
ctypes.cast(pDev + 0x6C, ctypes.POINTER(wintypes.DWORD))[0] = TOTAL_LEDS

led_id_table = ctypes.cast(pDev + 0x74, ctypes.POINTER(ctypes.c_ubyte))
for i in range(TOTAL_LEDS):
    led_id_table[i] = i

rgb_buf = (ctypes.c_ubyte * (TOTAL_LEDS * 3))()

# Set ALL 128 LEDs to White
for lid in range(TOTAL_LEDS):
    rgb_buf[lid * 3 + 0] = 200
    rgb_buf[lid * 3 + 1] = 200
    rgb_buf[lid * 3 + 2] = 200

# ESC: Red
rgb_buf[1 * 3 + 0] = 255
rgb_buf[1 * 3 + 1] = 0
rgb_buf[1 * 3 + 2] = 0

# WASD: Pure Green
for lid in [18, 11, 19, 27]:
    rgb_buf[lid * 3 + 0] = 0
    rgb_buf[lid * 3 + 1] = 255
    rgb_buf[lid * 3 + 2] = 0

fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

print("[*] Lighting ALL 128 LEDs (WASD=Green, ESC=Red, ALL OTHER KEYS=White) for 6 seconds...")
start_time = time.time()
frame_count = 0

while time.time() - start_time < 6.0:
    res = fn_set_single(pDev, ctypes.byref(rgb_buf))
    frame_count += 1
    time.sleep(0.04)

print(f"[+] All-LEDs test done! Sent {frame_count} frames.")
ole32.CoUninitialize()
