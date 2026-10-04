using AceHFX.AsusPlatform;
using AceHFX.AsusPlatform.Aura;
using AceHFX.AsusPlatform.Cooling;
using AceHFX.AsusPlatform.Ipc;

namespace AceHFX.Service;

// One worker in flight across all clients; failure backoff is shared, not per UI page.
public sealed class CoolingReadOnlyProvider : ICoolingReadOnlyProvider
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly Func<IAuraWorker> _start;
    private readonly Func<bool> _authorized;
    private readonly TimeSpan _timeout;
    private readonly StructuredLog? _log;
    private CoolingRuntimeSnapshot? _cached;
    private DateTimeOffset _nextRead;
    private DateTimeOffset _nextLog;
    private string? _lastLogState;
    public CoolingReadOnlyProvider(StructuredLog log) : this(
        () => new WorkerProcess(Path.Combine(AppContext.BaseDirectory, FanReadProtocol.WorkerFileName)),
        () => ServiceHost.NativeIdentity() is { IsSystem: true, SessionId: 0 }, FanReadProtocol.WorkerTimeout) { _log = log; }
    internal CoolingReadOnlyProvider(Func<IAuraWorker> start, Func<bool> authorized, TimeSpan timeout)
    { _start = start; _authorized = authorized; _timeout = timeout; }
    public async Task<CoolingRuntimeSnapshot> ReadAsync(CancellationToken cancellationToken = default)
    {
        if (!_authorized()) return CoolingRuntimeSnapshot.Unobserved(PlatformError.AccessDenied,
            "SystemBrokerRequired", FanObservationState.PermissionDenied);
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (_cached != null && DateTimeOffset.UtcNow < _nextRead) return _cached;
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            deadline.CancelAfter(_timeout);
            CoolingRuntimeSnapshot snapshot;
            try
            {
                await using var worker = _start();
                await FrameProtocol.WriteAsync(worker.Input, new FanWorkerReadRequest(1, Guid.NewGuid(), "ReadFanSnapshot"), deadline.Token);
                snapshot = await FrameProtocol.ReadAsync<CoolingRuntimeSnapshot>(worker.Output, deadline.Token);
                await worker.WaitForExitAsync(deadline.Token);
                if (worker.ExitCode != 0 && snapshot.Error == PlatformError.None)
                    snapshot = LostWorker(PlatformError.WorkerCrashed, "FanWorkerAbnormalExit", FanObservationState.Failed);
                else if (snapshot.WorkerIdentity?.ProcessId != worker.Pid || !CoolingSnapshotValidation.IsValid(snapshot))
                    snapshot = LostWorker(PlatformError.MalformedRequest, "FanWorkerInvalidResponse", FanObservationState.Failed);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            { snapshot = LostWorker(PlatformError.VendorTimeout, "FanWorkerDeadlineExceeded", FanObservationState.TimedOut); }
            catch (ProtocolException e)
            { snapshot = LostWorker(e.Error == PlatformError.TruncatedFrame ? PlatformError.WorkerCrashed : e.Error,
                "FanWorkerFrameRejected", FanObservationState.Failed); }
            catch (Exception e) when (e is IOException or System.ComponentModel.Win32Exception or InvalidOperationException)
            { snapshot = LostWorker(PlatformError.WorkerCrashed, "FanWorkerLaunchOrReadFailed", FanObservationState.Failed); }
            _cached = snapshot;
            _nextRead = DateTimeOffset.UtcNow + (snapshot.Error == PlatformError.None ? FanReadProtocol.MinimumPollInterval : TimeSpan.FromSeconds(30));
            var logState = $"{snapshot.Error}:{snapshot.Activation}:{snapshot.Capabilities}";
            if (logState != _lastLogState || DateTimeOffset.UtcNow >= _nextLog)
            {
                _log?.Write("CoolingRead", new { snapshot.ObservedAt, snapshot.Error, snapshot.Capabilities,
                    pid = snapshot.WorkerIdentity?.ProcessId }, snapshot.Error != PlatformError.None);
                _lastLogState = logState;
                _nextLog = DateTimeOffset.UtcNow.AddMinutes(1);
            }
            return snapshot;
        }
        finally { _gate.Release(); }
    }
    private static CoolingRuntimeSnapshot LostWorker(PlatformError error, string reason, FanObservationState state) =>
        CoolingRuntimeSnapshot.Unobserved(error, reason, state) with
        {
            Registered = FanObservationState.Unknown, ActivationStarted = FanObservationState.Unknown,
            Activation = FanObservationState.Unknown, InterfaceAcquired = FanObservationState.Unknown,
            InvocationStarted = FanObservationState.Unknown, Invocation = FanObservationState.Unknown,
            Capabilities = new(state, FanObservationState.Unknown, FanObservationState.Unknown, null, null, null,
                FanObservationState.Unknown, FanObservationState.NotAttempted)
        };
}
