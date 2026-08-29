import ctypes

kernel32 = ctypes.windll.kernel32
kernel32.LoadLibraryW.restype = ctypes.c_void_p
kernel32.LoadLibraryW.argtypes = [ctypes.c_wchar_p]
kernel32.GetProcAddress.restype = ctypes.c_void_p
kernel32.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

dll_path = r"C:\Program Files\ASUS\Aac_Keyboard\AacKbHal_x64.dll"
hMod = kernel32.LoadLibraryW(dll_path)
print(f"Loaded at 0x{hMod:016X}")

CLSID_ClaymoreHal = bytes.fromhex("C84B9DAE2A4F56479B112F6D78C61F1A")
IID_IClassFactory = bytes.fromhex("0100000000000000C000000000000046")
IID_IAsusAacLedDeviceHal = bytes.fromhex("B4D5C8F2543825438A4FFD7C5072E3BA")

pfn_DllGetClassObject = kernel32.GetProcAddress(hMod, b"DllGetClassObject")
print("DllGetClassObject at", hex(pfn_DllGetClassObject))

DllGetClassObject = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p))(pfn_DllGetClassObject)

pFactory = ctypes.c_void_p()
hr = DllGetClassObject(CLSID_ClaymoreHal, IID_IClassFactory, ctypes.byref(pFactory))
print("DllGetClassObject result:", hex(hr), "pFactory:", hex(pFactory.value or 0))

if hr == 0 and pFactory.value:
    vtable_ptr = ctypes.cast(pFactory.value, ctypes.POINTER(ctypes.c_void_p)).contents.value
    create_instance_ptr = ctypes.cast(vtable_ptr + 3 * 8, ctypes.POINTER(ctypes.c_void_p)).contents.value
    CreateInstance = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p))(create_instance_ptr)
    pHal = ctypes.c_void_p()
    hr = CreateInstance(pFactory, None, IID_IAsusAacLedDeviceHal, ctypes.byref(pHal))
    print("CreateInstance result:", hex(hr), "pHal:", hex(pHal.value or 0))

    if hr == 0 and pHal.value:
        hal_vtable = ctypes.cast(pHal.value, ctypes.POINTER(ctypes.c_void_p)).contents.value
        access_ptr = ctypes.cast(hal_vtable + 4 * 8, ctypes.POINTER(ctypes.c_void_p)).contents.value
        create_dev_ptr = ctypes.cast(hal_vtable + 5 * 8, ctypes.POINTER(ctypes.c_void_p)).contents.value
        Access = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p)(access_ptr)
        Access(pHal)

        dev_storage = (ctypes.c_void_p * 64)()
        class FakeVec(ctypes.Structure):
            _fields_ = [
                ("first", ctypes.POINTER(ctypes.c_void_p)),
                ("last", ctypes.POINTER(ctypes.c_void_p)),
                ("end", ctypes.POINTER(ctypes.c_void_p)),
            ]
        vec = FakeVec()
        vec.first = ctypes.cast(dev_storage, ctypes.POINTER(ctypes.c_void_p))
        vec.last = ctypes.cast(dev_storage, ctypes.POINTER(ctypes.c_void_p))
        vec.end = ctypes.cast(ctypes.byref(dev_storage, 64 * 8), ctypes.POINTER(ctypes.c_void_p))

        CreateLedDevice = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.POINTER(FakeVec))(create_dev_ptr)
        res = CreateLedDevice(pHal, ctypes.byref(vec))
        print("CreateLedDevice result:", res, "dev0:", hex(dev_storage[0] or 0))

        if dev_storage[0]:
            pDev = dev_storage[0]
            dev_vtable = ctypes.cast(pDev, ctypes.POINTER(ctypes.c_void_p)).contents.value
            for i in range(25):
                fn = ctypes.cast(dev_vtable + i * 8, ctypes.POINTER(ctypes.c_void_p)).contents.value
                rva = fn - hMod
                print(f"dev_vtable[{i:2d}] = 0x{fn:016X} (RVA 0x{rva:08X})")
