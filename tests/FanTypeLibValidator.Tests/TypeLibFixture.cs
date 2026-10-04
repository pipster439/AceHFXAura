using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text.Json.Nodes;
using AceHFX.FanWorker;

[assembly: DoNotParallelize]
namespace FanTypeLibValidator.Tests;

// Managed metadata fixture only. No activation or vendor member invocation.
public class MetadataProxy : DispatchProxy
{
    internal Func<string, object?[]?, object?> Handler = null!;
    protected override object? Invoke(MethodInfo? targetMethod, object?[]? args) => Handler(targetMethod!.Name, args);
}

internal sealed class TypeLibFixture : IDisposable
{
    private readonly List<IntPtr> allocations = [];
    internal JsonObject Model { get; } = JsonNode.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "fancontrol-typelib.json")))!.AsObject();
    internal string? Fault;
    internal ulong UpperBits = 0x1234567800000000;
    internal ITypeLib Create()
    {
        var lib = DispatchProxy.Create<ITypeLib, MetadataProxy>();
        ((MetadataProxy)(object)lib).Handler = (name, args) =>
        {
            switch (name)
            {
                case "GetLibAttr": args![0] = Allocate(new TYPELIBATTR { guid = Fault == "Library" ? Guid.Empty : Guid.Parse(Model["libraryGuid"]!.GetValue<string>()) }); return null;
                case "ReleaseTLibAttr": return null;
                case "GetTypeInfoOfGuid":
                    var guid = (Guid)args![0]!;
                    if (Fault == "MissingInterface") throw new COMException("fixture", unchecked((int)0x8002802B));
                    args[1] = Info(Model["types"]!.AsArray().Single(t => Guid.Parse(t!["guid"]!.GetValue<string>()) == guid)!.AsObject()); return null;
                default: throw new NotSupportedException(name);
            }
        };
        return lib;
    }
    private ITypeInfo Info(JsonObject type, bool referenced = false)
    {
        var functions = type["functions"]!.AsArray().Where(f => f!["dispid"]!.GetValue<int>() < 1000).ToArray();
        var references = new Dictionary<int, Guid>();
        var proxy = DispatchProxy.Create<ITypeInfo, MetadataProxy>();
        ((MetadataProxy)(object)proxy).Handler = (name, args) =>
        {
            switch (name)
            {
                case "GetTypeAttr":
                    args![0] = Allocate(new TYPEATTR { guid = (Fault == "InterfaceIdentity" || referenced && Fault == "ReferenceIid") ? Guid.Empty : Guid.Parse(type["guid"]!.GetValue<string>()), cFuncs = (short)functions.Length }); return null;
                case "ReleaseTypeAttr": case "ReleaseFuncDesc": return null;
                case "GetFuncDesc":
                    var f = functions[(int)args![0]!]!;
                    var isControls = f["name"]!.GetValue<string>() == "Controls";
                    var isItem = f["name"]!.GetValue<string>() == "Item";
                    var ps = f["parameters"]!.AsArray();
                    var parameterPointer = ps.Count == 0 ? IntPtr.Zero : AllocateBytes(Marshal.SizeOf<ELEMDESC>() * ps.Count);
                    for (var i = 0; i < ps.Count; i++)
                    {
                        var e = new ELEMDESC { tdesc = new TYPEDESC { vt = (short)(isItem && Fault == "ParameterType" ? 19 : ps[i]!["vt"]!.GetValue<int>()) } };
                        e.desc.paramdesc.wParamFlags = (PARAMFLAG)(isItem && Fault == "ParameterFlags" ? 2 : ps[i]!["flags"]!.GetValue<int>());
                        Marshal.StructureToPtr(e, IntPtr.Add(parameterPointer, i * Marshal.SizeOf<ELEMDESC>()), false);
                    }
                    var returned = new TYPEDESC { vt = (short)f["returnVt"]!.GetValue<int>() };
                    if (f["returnType"]?["element"]?["guid"] is JsonNode targetGuid)
                    {
                        var key = unchecked((int)(0x80000000u | (uint)(references.Count + 7)));
                        references[key] = Guid.Parse(targetGuid.GetValue<string>());
                        var union = IntPtr.Size == 8 ? new IntPtr(unchecked((long)(UpperBits | unchecked((uint)key)))) : new IntPtr(key);
                        var nested = new TYPEDESC { vt = (short)(isControls && Fault == "NestedType" ? 3 : 29), lpValue = union };
                        returned.lpValue = isControls && Fault == "NullNested" ? IntPtr.Zero : Allocate(nested);
                    }
                    if (isControls && Fault == "ReturnType") { returned.vt = 19; returned.lpValue = new IntPtr(1); }
                    args[1] = Allocate(new FUNCDESC { memid = isControls && Fault == "Dispid" ? -1 : f["dispid"]!.GetValue<int>(),
                        invkind = (INVOKEKIND)(isControls && Fault == "Setter" ? 4 : f["invokeKind"]!.GetValue<int>()),
                        cParams = (short)(isControls && Fault == "ParameterCount" ? 1 : ps.Count), lprgelemdescParam = isItem && Fault == "NullParameters" ? IntPtr.Zero : parameterPointer,
                        elemdescFunc = new ELEMDESC { tdesc = returned } }); return null;
                case "GetRefTypeInfo":
                    var target = references[(int)args![0]!];
                    args[1] = Info(Model["types"]!.AsArray().Single(t => Guid.Parse(t!["guid"]!.GetValue<string>()) == target)!.AsObject(), true); return null;
                default: throw new NotSupportedException(name);
            }
        };
        return proxy;
    }
    private IntPtr Allocate<T>(T value) where T : struct
    {
        var pointer = AllocateBytes(Marshal.SizeOf<T>()); Marshal.StructureToPtr(value, pointer, false); return pointer;
    }
    private IntPtr AllocateBytes(int bytes) { var pointer = Marshal.AllocHGlobal(bytes); allocations.Add(pointer); return pointer; }
    public void Dispose() { foreach (var pointer in allocations) Marshal.FreeHGlobal(pointer); }
}
