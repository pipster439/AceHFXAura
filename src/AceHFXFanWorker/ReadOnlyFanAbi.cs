using System.Runtime.InteropServices;

namespace AceHFX.FanWorker;

// Reduced automation interfaces, revalidated against the installed TypeLib in M3.0.
// InterfaceIsIDispatch intentionally uses verified DISPIDs, not an invented sparse vtable.
// No mutable member is declared. Interface identities remain inside this executable.
[ComImport, Guid("BB327645-E3EC-4B7D-90EE-67FCB949E584"), InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
internal interface IFanManagerRead
{
    [DispId(2)] IFanCollectionRead Controls { get; }
    [DispId(11)] uint FanServiceVersion { get; }
    [DispId(15)] uint FanCount { get; }
    [DispId(31)] bool IsSupport_10_point { [return: MarshalAs(UnmanagedType.VariantBool)] get; }
    [DispId(34)] int getNumberOfCurvePoint { get; }
}
[ComImport, Guid("05F22B4C-27D5-4488-A7EE-B712FE447426"), InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
internal interface IFanCollectionRead
{
    [DispId(2)] int Count { get; }
    [DispId(0)] IFanRead this[int index] { get; }
}
[ComImport, Guid("D20E7B8F-C878-4538-B624-9BC08FE7D54F"), InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
internal interface IFanRead
{
    [DispId(1)] uint Id { get; }
    [DispId(2)] string Name { [return: MarshalAs(UnmanagedType.BStr)] get; }
    [DispId(3)] byte DutyCycle { get; }
    [DispId(4)] IFanProfilesRead Profiles { get; }
    [DispId(10)] byte MinimalDuty { get; }
    [DispId(13)] IFanCurveRead CurrentFanCurve { get; }
    [DispId(19)] bool IsRpmMode { [return: MarshalAs(UnmanagedType.VariantBool)] get; }
    [DispId(21)] bool EcFanStop { [return: MarshalAs(UnmanagedType.VariantBool)] get; }
    [DispId(38)] uint FanSourceIndex { get; }
}
[ComImport, Guid("8CAF0DBE-D1E1-42A4-B0EC-1B365CF50982"), InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
internal interface IFanProfilesRead { [DispId(4)] uint Current { get; } }
[ComImport, Guid("E6BB07B4-F360-45AF-814E-EB0D12D55BEC"), InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
internal interface IFanCurveRead
{
    [DispId(2)] int Count { get; }
    [DispId(0)] IFanPointRead this[int index] { get; }
}
[ComImport, Guid("C3C24163-FC1B-431F-978D-44A9FBAC34EC"), InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
internal interface IFanPointRead
{
    [DispId(1)] int Temperature { get; }
    [DispId(2)] int Speed { get; }
}
