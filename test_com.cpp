#include <windows.h>
#include <iostream>

struct alignas(void*) FakeVector {
    void** first;
    void** last;
    void** end;
};

using PFN_CreateLedDevice = LONG (__stdcall *)(void* this_ptr, FakeVector* vec);
using PFN_Release = ULONG (__stdcall *)(void* this_ptr);

int main() {
    SetDllDirectoryW(L"C:\\Program Files\\ASUS\\Aac_Keyboard");
    LoadLibraryW(L"C:\\Program Files\\ASUS\\Aac_Keyboard\\AacKbHal_x64.dll");

    CoInitialize(NULL);

    GUID clsid_hal{}, iid_hal{};
    CLSIDFromString(L"{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}", &clsid_hal);
    CLSIDFromString(L"{F2C8D5B4-3854-4325-8A4F-FD7C5072E3BA}", &iid_hal);

    void* hal_ptr = nullptr;
    std::cout << "Calling CoCreateInstance...\n";
    HRESULT hr = CoCreateInstance(
        clsid_hal,
        nullptr,
        CLSCTX_INPROC_SERVER,
        iid_hal,
        &hal_ptr
    );
    std::cout << "CoCreateInstance hr: " << hr << ", hal_ptr: " << hal_ptr << "\n";
    if (hal_ptr) {
        void** hal_vtable = *reinterpret_cast<void***>(hal_ptr);
        void* dev_storage[64] = {nullptr};
        FakeVector vec{dev_storage, dev_storage, dev_storage + 64};
        PFN_CreateLedDevice fn_create = reinterpret_cast<PFN_CreateLedDevice>(hal_vtable[5]);
        LONG res = fn_create(hal_ptr, &vec);
        std::cout << "CreateLedDevice res: " << res << ", dev count: " << (vec.last - vec.first) << "\n";

        if (vec.last > vec.first && dev_storage[0]) {
            void** dev_vtable = *reinterpret_cast<void***>(dev_storage[0]);
            PFN_Release fn_dev_rel = reinterpret_cast<PFN_Release>(dev_vtable[2]);
            fn_dev_rel(dev_storage[0]);
        }
        PFN_Release fn_hal_rel = reinterpret_cast<PFN_Release>(hal_vtable[2]);
        fn_hal_rel(hal_ptr);
    }
    CoUninitialize();
    return 0;
}
