using System.Diagnostics;
using System.Runtime.InteropServices;
using AceHFX.AsusPlatform;
using AceHFX.AsusPlatform.Cooling;
using AceHFX.AsusPlatform.Ipc;
using AceHFX.FanWorker;

// One fixed read transaction. No command-line override, alternate CLSID or vendor-call forwarding.
if (args.Length != 0) { Environment.ExitCode = 2; return; }
CoolingRuntimeSnapshot snapshot;
try
{
    WorkerIdentity.Validate();
    using var inputDeadline = new CancellationTokenSource(FanReadProtocol.WorkerTimeout);
    var request = await FrameProtocol.ReadAsync<FanWorkerReadRequest>(Console.OpenStandardInput(), inputDeadline.Token);
    if (request.SchemaVersion != 1 || request.RequestId == Guid.Empty || request.Command != "ReadFanSnapshot")
        throw new InvalidDataException("ReadRequestRejected");
    // All COM lifetime, including activation, stays on this single dedicated STA thread.
    var completion = new TaskCompletionSource<CoolingRuntimeSnapshot>(TaskCreationOptions.RunContinuationsAsynchronously);
    var thread = new Thread(() =>
    {
        try { completion.SetResult(FanReader.Read()); }
        catch (Exception e) { completion.SetResult(CoolingRuntimeSnapshot.Unobserved(PlatformError.VendorApiFailure, e.GetType().Name)); }
    });
    thread.SetApartmentState(ApartmentState.STA);
    thread.Start();
    snapshot = await completion.Task; // Broker's external job/timeout bounds a hung native call.
}
catch (UnauthorizedAccessException)
{
    snapshot = CoolingRuntimeSnapshot.Unobserved(PlatformError.AccessDenied, "ProtectedSystemBrokerRequired", FanObservationState.PermissionDenied)
        with { WorkerIdentity = NativeSecurity.Current() };
    Environment.ExitCode = 3;
}
catch (Exception e)
{
    snapshot = CoolingRuntimeSnapshot.Unobserved(PlatformError.MalformedRequest, e.GetType().Name);
    Environment.ExitCode = 2;
}
using var outputDeadline = new CancellationTokenSource(TimeSpan.FromSeconds(1));
await FrameProtocol.WriteAsync(Console.OpenStandardOutput(), snapshot, outputDeadline.Token);

internal static class FanReader
{
    internal static CoolingRuntimeSnapshot Read()
    {
        var result = FanComActivation.Initialize();
        if (result < 0) return CoolingRuntimeSnapshot.Unobserved(PlatformError.VendorApiFailure, "FanApartmentInitFailed") with { CoInitializeHresult = result };
        try { return ReadInitialized() with { CoInitializeHresult = result }; }
        finally { FanComActivation.Uninitialize(); }
    }
    private static CoolingRuntimeSnapshot ReadInitialized()
    {
        var identity = NativeSecurity.Current();
        var snapshot = CoolingRuntimeSnapshot.Unobserved(PlatformError.None, "FanReadPending") with { WorkerIdentity = identity,
            Capabilities = new(FanObservationState.Succeeded, FanObservationState.NotAttempted, FanObservationState.NotAttempted,
                null, null, null, FanObservationState.NotAttempted, FanObservationState.NotAttempted) };
        var service = new AceHFX.AsusPlatform.Runtime.AsusServiceDetector(new()).Detect("AsusFanControlService");
        if (service.Running.Value != true)
            return snapshot with { Error = PlatformError.ServiceUnavailable, Diagnostic = "FanVendorServiceNotRunning; activation not attempted" };
        try { FanLibraryContract.Verify(); }
        catch (Exception e) { return snapshot with { Error = PlatformError.UnsupportedCapability, Diagnostic = FanLibraryContract.DescribeFailure(e) }; }
        snapshot = snapshot with { Registered = FanObservationState.Succeeded, ActivationStarted = FanObservationState.Succeeded };
        object? instance = null;
        var activation = Stopwatch.StartNew();
        try
        {
            var activated = FanComActivation.Activate();
            instance = activated.Instance;
            activation.Stop();
            snapshot = snapshot with { Activation = FanObservationState.Succeeded, ActivationHresult = activated.Hresult,
                ActivationLatencyMs = activation.Elapsed.TotalMilliseconds,
                Capabilities = snapshot.Capabilities with { ComActivated = FanObservationState.Succeeded } };
            var manager = (IFanManagerRead)(instance ?? throw new InvalidDataException("NullFanManager"));
            snapshot = snapshot with { InterfaceAcquired = FanObservationState.Succeeded,
                InvocationStarted = FanObservationState.Succeeded, ServiceVersion = Observe<uint?>(() => manager.FanServiceVersion),
                VendorFanCount = Observe<uint?>(() => manager.FanCount), NativePointCount = Observe<int?>(() => manager.getNumberOfCurvePoint),
                TenPointCapability = Observe<bool?>(() => manager.IsSupport_10_point) };
            var collection = manager.Controls;
            List<FanChannelDescriptor> channels = [];
            try
            {
                var count = collection.Count;
                if (count is < 0 or > FanReadProtocol.MaximumChannels) throw new InvalidDataException("FanCountBoundExceeded");
                for (var i = 0; i < count; i++)
                {
                    var fan = collection[i];
                    try
                    {
                        var id = Observe<uint?>(() => fan.Id);
                        channels.Add(new(id.Value.HasValue ? $"asus:fan:{id.Value.Value}" : $"unresolved:collection:{i}", i, id,
                            Observe(() => { var name = fan.Name; if (name.Length > 128) throw new InvalidDataException("FanNameTooLong"); return name; }),
                            new(FanObservationState.Unsupported, null, Reason: "NoPerChannelRpmGetterInFanTypeLib"),
                            Observe<byte?>(() => fan.DutyCycle), Observe<bool?>(() => fan.IsRpmMode), Observe<bool?>(() => fan.EcFanStop),
                            Observe<uint?>(() => fan.FanSourceIndex), ReadProfile(fan), ReadCurve(fan), Observe<byte?>(() => fan.MinimalDuty),
                            Confidence: id.State == FanObservationState.Succeeded ? FanEvidenceConfidence.RuntimeExact : FanEvidenceConfidence.Unknown));
                    }
                    finally { Marshal.ReleaseComObject(fan); }
                }
            }
            finally { Marshal.ReleaseComObject(collection); }
            return snapshot with { Invocation = FanObservationState.Succeeded, Channels = channels,
                Capabilities = new(FanObservationState.Succeeded, FanObservationState.Succeeded, FanObservationState.Succeeded,
                    channels.Count, null, null,
                    channels.Count > 0 && channels.All(x => x.CurrentCurve?.PointCount.State == FanObservationState.Succeeded) ?
                        FanObservationState.Succeeded : FanObservationState.Unknown, FanObservationState.NotAttempted),
                Diagnostic = "PerChannelRpmAndTemperatureSourcesUnverified; CurrentCurveMayBeServiceCache" };
        }
        catch (COMException e)
        {
            var state = e.HResult == unchecked((int)0x80070005) ? FanObservationState.PermissionDenied : FanObservationState.Failed;
            return snapshot with { Activation = instance == null ? state : snapshot.Activation,
                ActivationHresult = instance == null ? e.HResult : snapshot.ActivationHresult,
                ActivationLatencyMs = snapshot.ActivationLatencyMs ?? activation.Elapsed.TotalMilliseconds,
                Invocation = instance == null ? FanObservationState.NotAttempted : state,
                Error = e.HResult == unchecked((int)0x80070005) ? PlatformError.AccessDenied : PlatformError.VendorApiFailure,
                Diagnostic = "FanComReadFailed", Capabilities = snapshot.Capabilities with { WorkerReachable = FanObservationState.Succeeded,
                    ComActivated = instance == null ? state : FanObservationState.Succeeded } };
        }
        catch (Exception e) { return snapshot with { Error = PlatformError.VendorApiFailure, Invocation = FanObservationState.Failed, Diagnostic = e.GetType().Name }; }
        finally { if (instance != null) Marshal.ReleaseComObject(instance); }
    }
    private static FanValue<T> Observe<T>(Func<T> getter)
    {
        var clock = Stopwatch.StartNew();
        try { return new(FanObservationState.Succeeded, getter(), null, clock.Elapsed.TotalMilliseconds,
            "TypedAutomationCompleted; successful HRESULT not exposed by RCW"); }
        catch (Exception e) { return new(e.HResult == unchecked((int)0x80070005) ? FanObservationState.PermissionDenied : FanObservationState.Failed,
            default, e.HResult, clock.Elapsed.TotalMilliseconds, e.GetType().Name); }
    }
    private static FanValue<uint?> ReadProfile(IFanRead fan) => Observe<uint?>(() =>
    {
        var profiles = fan.Profiles;
        try { return profiles.Current; }
        finally { Marshal.ReleaseComObject(profiles); }
    });
    private static FanCurveDescriptor ReadCurve(IFanRead fan)
    {
        IFanCurveRead? curve = null;
        try
        {
            curve = fan.CurrentFanCurve;
            var count = Observe<int?>(() => curve.Count);
            if (count.State != FanObservationState.Succeeded) return new(count, []);
            if (count.Value is null or < 0 or > FanReadProtocol.MaximumCurvePoints)
                return new(new(FanObservationState.Failed, null, Reason: "CurveCountBoundExceeded"), []);
            List<FanCurvePointDescriptor> points = [];
            for (var i = 0; i < count.Value; i++)
            {
                var point = curve[i];
                try { points.Add(new(i, Observe<int?>(() => point.Temperature), Observe<int?>(() => point.Speed))); }
                finally { Marshal.ReleaseComObject(point); }
            }
            return new(count, points);
        }
        catch (Exception e) { return new(new(FanObservationState.Failed, null, e.HResult, Reason: e.GetType().Name), []); }
        finally { if (curve != null) Marshal.ReleaseComObject(curve); }
    }
}
