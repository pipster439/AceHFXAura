using AceHFX.AsusPlatform;
using AceHFX.AsusPlatform.Cooling;
using AceHFX.AsusPlatform.Ipc;

// Synthetic software fixture. No ASUS activation or native hardware calls.
if (args is not [var mode]) return 2;
var request = await FrameProtocol.ReadAsync<FanWorkerReadRequest>(Console.OpenStandardInput(), CancellationToken.None);
if (request.Command != "ReadFanSnapshot" || request.SchemaVersion != 1 || request.RequestId == Guid.Empty) return 2;
if (mode == "crash") return 27;
if (mode == "hang") { await Task.Delay(Timeout.Infinite); return 0; }
if (mode == "malformed") { await Console.OpenStandardOutput().WriteAsync(new byte[] { 0, 0, 0, 0 }); return 0; }
var snapshot = CoolingRuntimeSnapshot.Unobserved(PlatformError.None, "SyntheticFixtureNotHostEvidence") with
{
    WorkerIdentity = new("S-1-5-18", Environment.ProcessId, 0, true),
    Activation = mode == "denied" ? FanObservationState.PermissionDenied : FanObservationState.Succeeded,
    Error = mode == "denied" ? PlatformError.AccessDenied : PlatformError.None,
    Capabilities = new(FanObservationState.Succeeded,
        mode == "denied" ? FanObservationState.PermissionDenied : FanObservationState.Succeeded,
        mode == "denied" ? FanObservationState.NotAttempted : FanObservationState.Succeeded,
        mode == "denied" ? null : 0, null, null, FanObservationState.NotAttempted, FanObservationState.NotAttempted)
};
await FrameProtocol.WriteAsync(Console.OpenStandardOutput(), snapshot, CancellationToken.None);
return 0;
