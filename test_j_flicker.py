import ctypes
from ctypes import wintypes
import time
import json

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

ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p)(hal_vtable[4])(pHal.value)

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

# Load real 68 keys
keymap = json.load(open('calibrated_keymap.json', encoding='utf-8'))['keys']
calibrated_led_ids = sorted([v['led_id'] for v in keymap.values()])
print(f"[+] Loaded {len(calibrated_led_ids)} calibrated LED IDs")

# Check current setting of pDev
current_count = ctypes.cast(pDev + 0x6C, ctypes.POINTER(wintypes.DWORD))[0]
print(f"[+] Current key_count in pDev: {current_count}")

# Let's inspect: What happens if we write only 68 keys?
# Or what if we write all 128?
# Let's define a test function
fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(dev_vtable[19])

def test_stream(name, led_ids, r, g, b, duration=3.0):
    print(f"\n--- Testing {name} ({len(led_ids)} keys) with RGB({r},{g},{b}) for {duration}s ---")
    count = len(led_ids)
    ctypes.cast(pDev + 0x6C, ctypes.POINTER(wintypes.DWORD))[0] = count
    led_table = ctypes.cast(pDev + 0x74, ctypes.POINTER(ctypes.c_ubyte))
    for i, led_id in enumerate(led_ids):
        led_table[i] = led_id

    buf = (ctypes.c_ubyte * (count * 3))()
    for i in range(count):
        buf[i * 3 + 0] = r
        buf[i * 3 + 1] = g
        buf[i * 3 + 2] = b

    start = time.time()
    frames = 0
    while time.time() - start < duration:
        fn_set_single(pDev, ctypes.byref(buf))
        frames += 1
        time.sleep(0.04)
    print(f"Done {frames} frames.")

# First, test the current 128-key stream with all black RGB(0,0,0)
print("\n>>> TEST 1: Current 128-key table with all black (0,0,0) for 2s")
test_stream("128 keys (0..127)", list(range(128)), 0, 0, 0, duration=2.0)

# Second, test ONLY 68 calibrated keys with all black (0,0,0) for 2s
print("\n>>> TEST 2: Only 68 calibrated keys with all black (0,0,0) for 2s")
test_stream("68 calibrated keys", calibrated_led_ids, 0, 0, 0, duration=2.0)

ole32.CoUninitialize()
print("[+] Test completed.")
