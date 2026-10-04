using System.Runtime.InteropServices;

namespace AceHFX.FanWorker;

internal static class FanComActivation
{
    internal static int Initialize() => CoInitializeEx(IntPtr.Zero, 2); // COINIT_APARTMENTTHREADED
    internal static void Uninitialize() => CoUninitialize();
    internal static (object Instance, int Hresult) Activate()
    {
        var clsid = FanLibraryContract.ClassId;
        var iid = typeof(IFanManagerRead).GUID;
        var result = CoCreateInstance(ref clsid, IntPtr.Zero, 4, ref iid, out var pointer); // CLSCTX_LOCAL_SERVER only
        Marshal.ThrowExceptionForHR(result);
        if (pointer == IntPtr.Zero) throw new InvalidDataException("NullFanManager");
        try { return (Marshal.GetTypedObjectForIUnknown(pointer, typeof(IFanManagerRead)), result); }
        finally { Marshal.Release(pointer); }
    }
    [DllImport("ole32.dll", PreserveSig = true)] private static extern int CoInitializeEx(IntPtr reserved, uint apartment);
    [DllImport("ole32.dll")] private static extern void CoUninitialize();
    [DllImport("ole32.dll", PreserveSig = true)] private static extern int CoCreateInstance(ref Guid clsid, IntPtr outer,
        uint context, ref Guid iid, out IntPtr instance);
}
