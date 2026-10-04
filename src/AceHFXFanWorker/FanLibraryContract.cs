using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text.Json;
using Microsoft.Win32;

namespace AceHFX.FanWorker;

internal sealed record FanValidationDiagnostic(string Stage, string? InterfaceName = null, Guid? InterfaceIid = null,
    string? Property = null, int? Dispid = null, short? ExpectedVartype = null, short? ObservedVartype = null,
    string? HRefTypeBits = null, string? Reason = null, string? ExceptionType = null, int? ExceptionHresult = null);

internal sealed class FanValidationException(FanValidationDiagnostic diagnostic, Exception cause)
    : Exception("FanTypeLibValidationFailed", cause)
{
    internal FanValidationDiagnostic Diagnostic { get; } = diagnostic with
        { ExceptionType = cause.GetType().Name, ExceptionHresult = cause.HResult };
}

internal static class FanLibraryContract
{
    internal static readonly Guid ClassId = new("14083C53-B8E7-48E4-9320-811F3478C4A4");
    private static readonly Guid LibraryId = new("DF5522FB-119C-428D-A62E-8BFE4A2FBC9B");
    internal static string DescribeFailure(Exception error) => JsonSerializer.Serialize(error is FanValidationException validation
        ? validation.Diagnostic : new FanValidationDiagnostic("LibraryIdentity", ExceptionType: error.GetType().Name, ExceptionHresult: error.HResult));

    // HREFTYPE is ULONG, not a pointer or signed identifier. The int is only the
    // bit-preserving carrier required by ComTypes.ITypeInfo.GetRefTypeInfo.
    internal static int ExtractHRefType(TYPEDESC description)
    {
        if (description.vt != 29) throw new InvalidDataException("ExpectedUserDefinedType");
        return HRefTypeCarrier(unchecked((ulong)description.lpValue.ToInt64()));
    }
    internal static int HRefTypeCarrier(ulong unionStorage) => unchecked((int)(uint)unionStorage);

    internal static void Verify()
    {
        using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry32);
        using var registration = machine.OpenSubKey(@"SOFTWARE\Classes\CLSID\{" + ClassId + @"}\LocalServer32");
        var command = registration?.GetValue(null) as string ?? throw new FileNotFoundException("FanServerRegistrationMissing");
        var first = command.Trim();
        var end = first.StartsWith('"') ? first.IndexOf('"', 1) : first.IndexOf(".exe", StringComparison.OrdinalIgnoreCase) + 4;
        if (end <= 0) throw new InvalidDataException("FanServerPathInvalid");
        var path = first.StartsWith('"') ? first[1..end] : first[..end];
        var allowed = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "ASUS", "AsusFanControlService") + Path.DirectorySeparatorChar;
        if (!Path.IsPathFullyQualified(path) || !path.StartsWith(allowed, StringComparison.OrdinalIgnoreCase) ||
            Path.GetFileName(path) != "AsusFanControlService.exe") throw new InvalidDataException("FanServerPathRejected");
        VerifyFile(path);
    }

    // Loads metadata only: REGKIND_NONE neither registers nor activates the vendor server.
    internal static void VerifyFile(string path)
    {
        Marshal.ThrowExceptionForHR(LoadTypeLibEx(path, 2, out var library));
        try { VerifyLibrary(library); }
        finally { Release(library); }
    }

    internal static void VerifyLibrary(ITypeLib library)
    {
        var context = new FanValidationDiagnostic("LibraryIdentity");
        try
        {
            library.GetLibAttr(out var attribute);
            try
            {
                RequirePointer(attribute);
                if (Marshal.PtrToStructure<TYPELIBATTR>(attribute).guid != LibraryId)
                    throw new InvalidDataException("FanLibraryIdentityMismatch");
            }
            finally { if (attribute != IntPtr.Zero) library.ReleaseTLibAttr(attribute); }
            foreach (var contract in new[] { typeof(IFanManagerRead), typeof(IFanCollectionRead), typeof(IFanRead),
                typeof(IFanProfilesRead), typeof(IFanCurveRead), typeof(IFanPointRead) })
            {
                var iid = contract.GUID;
                context = new("InterfaceLookup", contract.Name, iid);
                library.GetTypeInfoOfGuid(ref iid, out var info);
                try
                {
                    info.GetTypeAttr(out var ptr);
                    try
                    {
                        RequirePointer(ptr);
                        var attr = Marshal.PtrToStructure<TYPEATTR>(ptr);
                        if (attr.guid != iid || attr.cFuncs < 0 || attr.cFuncs > 4096)
                            throw new InvalidDataException("FanInterfaceIdentityOrSizeMismatch");
                        foreach (var property in contract.GetProperties())
                        {
                            var id = property.GetCustomAttributes(typeof(DispIdAttribute), false).Cast<DispIdAttribute>().Single().Value;
                            context = new("PropertyLookup", contract.Name, iid, property.Name, id, ExpectedType(property.PropertyType));
                            var found = false;
                            for (var i = 0; i < attr.cFuncs; i++)
                            {
                                info.GetFuncDesc(i, out var descriptor);
                                try
                                {
                                    RequirePointer(descriptor);
                                    var f = Marshal.PtrToStructure<FUNCDESC>(descriptor);
                                    if (f.memid != id) continue;
                                    context = context with { ObservedVartype = f.elemdescFunc.tdesc.vt };
                                    if (f.invkind != INVOKEKIND.INVOKE_PROPERTYGET) continue;
                                    if (f.cParams != property.GetIndexParameters().Length)
                                        throw new InvalidDataException("FanGetterParameterCountMismatch");
                                    ValidateReturn(info, f.elemdescFunc.tdesc, property.PropertyType, ref context);
                                    for (var parameterIndex = 0; parameterIndex < f.cParams; parameterIndex++)
                                    {
                                        context = context with { Stage = "ParameterType", ExpectedVartype = ExpectedType(property.GetIndexParameters()[parameterIndex].ParameterType),
                                            ObservedVartype = null, HRefTypeBits = null, Reason = "IndexParameter" };
                                        RequirePointer(f.lprgelemdescParam);
                                        var parameter = Marshal.PtrToStructure<ELEMDESC>(IntPtr.Add(f.lprgelemdescParam, checked(parameterIndex * Marshal.SizeOf<ELEMDESC>())));
                                        context = context with { ObservedVartype = parameter.tdesc.vt };
                                        if (parameter.tdesc.vt != context.ExpectedVartype || parameter.desc.paramdesc.wParamFlags != PARAMFLAG.PARAMFLAG_FIN)
                                            throw new InvalidDataException("FanGetterParameterContractMismatch");
                                    }
                                    found = true;
                                    break;
                                }
                                finally { if (descriptor != IntPtr.Zero) info.ReleaseFuncDesc(descriptor); }
                            }
                            if (!found) throw new InvalidDataException("FanGetterMissing");
                        }
                    }
                    finally { if (ptr != IntPtr.Zero) info.ReleaseTypeAttr(ptr); }
                }
                finally { Release(info); }
            }
        }
        catch (Exception error) { throw new FanValidationException(context, error); }
    }
    private static short ExpectedType(Type type) => type == typeof(int) ? (short)3 : type == typeof(uint) ? (short)19 :
        type == typeof(byte) ? (short)17 : type == typeof(bool) ? (short)11 : type == typeof(string) ? (short)8 : (short)26;

    private static void ValidateReturn(ITypeInfo info, TYPEDESC description, Type type, ref FanValidationDiagnostic context)
    {
        context = context with { Stage = "ReturnType", ExpectedVartype = ExpectedType(type), ObservedVartype = description.vt };
        if (description.vt != context.ExpectedVartype) throw new InvalidDataException("FanGetterReturnTypeMismatch");
        if (!type.IsInterface) return;
        // Only VT_PTR is dereferenced. Only its VT_USERDEFINED element contains HREFTYPE.
        RequirePointer(description.lpValue);
        var element = Marshal.PtrToStructure<TYPEDESC>(description.lpValue);
        context = context with { Stage = "UserDefinedReference", ExpectedVartype = 29, ObservedVartype = element.vt };
        if (element.vt != 29) throw new InvalidDataException("FanReferencedTypeMismatch");
        var href = ExtractHRefType(element);
        context = context with { HRefTypeBits = $"0x{unchecked((uint)href):X8}" };
        info.GetRefTypeInfo(href, out var target);
        try
        {
            target.GetTypeAttr(out var ptr);
            try
            {
                RequirePointer(ptr);
                if (Marshal.PtrToStructure<TYPEATTR>(ptr).guid != type.GUID)
                    throw new InvalidDataException("FanReferencedInterfaceMismatch");
            }
            finally { if (ptr != IntPtr.Zero) target.ReleaseTypeAttr(ptr); }
        }
        finally { Release(target); }
    }
    private static void RequirePointer(IntPtr pointer)
    {
        if (pointer == IntPtr.Zero) throw new InvalidDataException("FanNullTypeDescriptor");
    }
    private static void Release(object value)
    {
        if (Marshal.IsComObject(value)) Marshal.ReleaseComObject(value);
    }
    [DllImport("oleaut32.dll", CharSet = CharSet.Unicode, PreserveSig = true)]
    private static extern int LoadTypeLibEx(string path, int kind, [MarshalAs(UnmanagedType.Interface)] out ITypeLib library);
}
