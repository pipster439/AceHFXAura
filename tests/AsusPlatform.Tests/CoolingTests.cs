using AceHFX.AsusPlatform;
using AceHFX.AsusPlatform.Aura;
using AceHFX.AsusPlatform.Cooling;
using AceHFX.AsusPlatform.Ipc;
using AceHFX.AsusPlatform.Runtime;
using AceHFX.Service;

namespace AsusPlatform.Tests;

[TestClass]
public sealed class CoolingTests
{
    private static CoolingReadOnlyProvider Backend(string mode, Action? started = null) => new(() =>
    {
        started?.Invoke();
        return new WorkerProcess(Path.Combine(AppContext.BaseDirectory, "fan-fixture", "FanWorkerFixture.exe"), mode);
    }, () => true, TimeSpan.FromSeconds(2));
    private static FanSafetyEvidence Safety(FanSafetyClass kind = FanSafetyClass.Chassis) => new(kind, true, true, true, true, true, true);
    [TestMethod] public async Task ReadFixtureActivationSuccessPreservesZeroEnumeration()
    {
        var snapshot = await Backend("normal").ReadAsync();
        Assert.AreEqual(PlatformError.None, snapshot.Error);
        Assert.AreEqual(FanObservationState.Succeeded, snapshot.Activation);
        Assert.AreEqual(0, snapshot.Capabilities.ControlChannelCount);
        Assert.IsNull(snapshot.Capabilities.RpmSensorCount);
        Assert.IsTrue(CoolingSnapshotValidation.IsValid(snapshot));
    }
    [TestMethod] public async Task AccessDeniedDoesNotInventZeroFans()
    {
        var snapshot = await Backend("denied").ReadAsync();
        Assert.AreEqual(PlatformError.AccessDenied, snapshot.Error);
        Assert.AreEqual(FanObservationState.PermissionDenied, snapshot.Activation);
        Assert.IsNull(snapshot.Capabilities.ControlChannelCount);
    }
    [TestMethod] public async Task WorkerCrashIsolated()
    {
        var snapshot = await Backend("crash").ReadAsync();
        Assert.AreEqual(PlatformError.WorkerCrashed, snapshot.Error);
        Assert.IsNull(snapshot.Capabilities.ControlChannelCount);
    }
    [TestMethod] public async Task WorkerTimeoutIsolatedAndNotZero()
    {
        var snapshot = await Backend("hang").ReadAsync();
        Assert.AreEqual(PlatformError.VendorTimeout, snapshot.Error);
        Assert.AreEqual(FanObservationState.TimedOut, snapshot.Capabilities.WorkerReachable);
        Assert.AreEqual(FanObservationState.Unknown, snapshot.Activation);
        Assert.IsNull(snapshot.Capabilities.ControlChannelCount);
    }
    [TestMethod] public async Task MalformedWorkerFrameRejected()
    { Assert.AreEqual(PlatformError.MalformedRequest, (await Backend("malformed").ReadAsync()).Error); }
    [TestMethod] public async Task ReadRequestsShareSingleWorkerAndCache()
    {
        var started = 0; var provider = Backend("normal", () => started++);
        var snapshots = await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => provider.ReadAsync()));
        Assert.AreEqual(1, started); Assert.IsTrue(snapshots.All(x => x.Error == PlatformError.None));
    }
    [TestMethod] public async Task ReconnectRecyclesWorkerAfterCacheExpiry()
    {
        var started = 0; var provider = Backend("normal", () => started++);
        var first = await provider.ReadAsync(); await Task.Delay(1100); var second = await provider.ReadAsync();
        Assert.AreEqual(2, started); Assert.AreNotEqual(first.WorkerIdentity?.ProcessId, second.WorkerIdentity?.ProcessId);
    }
    [TestMethod] public async Task FailedReadBackoffPreventsSpawnStorm()
    {
        var started = 0; var provider = Backend("crash", () => started++);
        await provider.ReadAsync(); await provider.ReadAsync(); Assert.AreEqual(1, started);
    }
    [TestMethod] public async Task UnauthorizedHostNeverLaunchesWorker()
    {
        var provider = new CoolingReadOnlyProvider(() => throw new AssertFailedException("Worker launched"), () => false, TimeSpan.FromSeconds(1));
        Assert.AreEqual(PlatformError.AccessDenied, (await provider.ReadAsync()).Error);
    }
    [TestMethod] public async Task CancellationKillsOnlyFixtureAndAllowsReconnect()
    {
        var provider = Backend("hang"); using var cancellation = new CancellationTokenSource(250);
        await Assert.ThrowsAsync<OperationCanceledException>(() => provider.ReadAsync(cancellation.Token));
        Assert.AreEqual(PlatformError.None, (await Backend("normal").ReadAsync()).Error);
    }
    [TestMethod] public void ZeroRpmAndUnknownAreDistinct()
    {
        var zero = new FanValue<int?>(FanObservationState.Succeeded, 0);
        var unknown = new FanValue<int?>(FanObservationState.Unknown, null);
        Assert.AreEqual(0, zero.Value); Assert.IsNull(unknown.Value); Assert.AreNotEqual(zero.State, unknown.State);
    }
    [TestMethod] public void ControlAndTachAreSeparateModels()
    {
        var sensor = new FanSensorDescriptor("tach:cpuopt", "CPU Optional Fan", new(FanObservationState.NotAttempted, null),
            "RPM", "ConfigOnlyTach; coupling unverified", false, null, FanEvidenceConfidence.ConfigExact);
        Assert.IsFalse(sensor.ControlSupported); Assert.IsNull(sensor.ChannelStableId);
    }
    [TestMethod] public void UnknownModeNumbersAndPointCountsRemainRaw()
    {
        Assert.IsFalse(Enum.IsDefined((FanObservationState)999));
        var curve = new FanCurveDescriptor(new(FanObservationState.Succeeded, 10), []);
        Assert.AreEqual(10, curve.PointCount.Value); Assert.AreEqual("VendorRaw; scaling not validated", curve.Units);
    }
    [TestMethod] public void WireStatesHaveStableValues()
    {
        Assert.AreEqual(9, (int)Enum.Parse<Command>("GetCoolingReadOnlySnapshot"));
        Assert.AreEqual(0, (int)Enum.Parse<FanObservationState>("NotAttempted")); Assert.AreEqual(7, (int)Enum.Parse<FanObservationState>("Unknown"));
    }
    [TestMethod] public async Task SnapshotSerializationRoundTrip()
    {
        var snapshot = await Backend("normal").ReadAsync(); using var stream = new MemoryStream();
        await FrameProtocol.WriteAsync(stream, snapshot, CancellationToken.None); stream.Position = 0;
        var copy = await FrameProtocol.ReadAsync<CoolingRuntimeSnapshot>(stream, CancellationToken.None);
        Assert.AreEqual(snapshot.WorkerIdentity, copy.WorkerIdentity); Assert.IsTrue(CoolingSnapshotValidation.IsValid(copy));
    }
    [TestMethod] public async Task MalformedSnapshotCannotInventSensors()
    {
        var snapshot = await Backend("normal").ReadAsync();
        Assert.IsFalse(CoolingSnapshotValidation.IsValid(snapshot with
        { Capabilities = snapshot.Capabilities with { RpmSensorCount = 8 } }));
        Assert.IsFalse(CoolingSnapshotValidation.IsValid(snapshot with
        { Capabilities = snapshot.Capabilities with { ControlChannelCount = 33 } }));
    }
    [TestMethod] public async Task InvalidCapabilityStateRejected()
    {
        var snapshot = await Backend("normal").ReadAsync();
        Assert.IsFalse(CoolingSnapshotValidation.IsValid(snapshot with
        { Capabilities = snapshot.Capabilities with { WorkerReachable = (FanObservationState)999 } }));
    }
    [TestMethod] public async Task TimeoutLeavesVendorProgressUnknown()
    {
        var snapshot = await Backend("hang").ReadAsync();
        Assert.AreEqual(FanObservationState.TimedOut, snapshot.Capabilities.WorkerReachable);
        Assert.AreEqual(FanObservationState.Unknown, snapshot.Activation);
        Assert.IsNull(snapshot.Capabilities.ControlChannelCount);
    }
    [TestMethod] public void UnknownAndCriticalChannelsNeverDefaultF1Targets()
    {
        foreach (var kind in new[] { FanSafetyClass.Unknown, FanSafetyClass.CriticalCpu, FanSafetyClass.CoolingCritical, FanSafetyClass.NonControllableSensor })
            CollectionAssert.Contains(FanGateF1.Blockers(Safety(kind)).ToArray(), "FirstTargetMustBeNonCritical");
    }
    [TestMethod] public void MissingRestoreBlocksF1() => CollectionAssert.Contains(FanGateF1.Blockers(Safety() with { RestoreContractVerified = false }).ToArray(), "DeterministicRestoreMissing");
    [TestMethod] public void MissingMinimumBlocksF1() => CollectionAssert.Contains(FanGateF1.Blockers(Safety() with { MinimumDutyVerified = false }).ToArray(), "SafeMinimumDutyMissing");
    [TestMethod] public void MissingPhysicalMappingBlocksF1() => CollectionAssert.Contains(FanGateF1.Blockers(Safety() with { UniqueMapping = false }).ToArray(), "UniquePhysicalMappingMissing");
    [TestMethod] public void MissingLiveTelemetryBlocksF1() => CollectionAssert.Contains(FanGateF1.Blockers(Safety() with { LiveRpmVerified = false, ThermalAbortVerified = false }).ToArray(), "LiveRpmMissing");
    [TestMethod] public async Task BrokerValidatesHelloBeforeCoolingRead()
    {
        var cooling = new CountingCooling(); var dispatcher = Dispatcher(cooling);
        var response = await dispatcher.DispatchAsync(new(1, Guid.NewGuid(), Command.GetCoolingReadOnlySnapshot), CancellationToken.None);
        Assert.IsFalse(response.Success); Assert.AreEqual(0, cooling.Calls);
    }
    [TestMethod] public async Task BrokerAllowsOnlyExplicitReadCommand()
    {
        var cooling = new CountingCooling(); var dispatcher = Dispatcher(cooling);
        await dispatcher.DispatchAsync(new(1, Guid.NewGuid(), Command.ClientHello), CancellationToken.None);
        var response = await dispatcher.DispatchAsync(new(1, Guid.NewGuid(), Command.GetCoolingReadOnlySnapshot), CancellationToken.None);
        Assert.IsTrue(response.Success); Assert.AreEqual(1, cooling.Calls); Assert.IsNotNull(response.Cooling);
        var unknown = await dispatcher.DispatchAsync(new(1, Guid.NewGuid(), (Command)999), CancellationToken.None);
        Assert.IsFalse(unknown.Success); Assert.AreEqual(1, cooling.Calls);
    }
    [TestMethod] public async Task DuplicateCoolingRequestRejectedBeforeWorker()
    {
        var cooling = new CountingCooling(); var dispatcher = Dispatcher(cooling);
        await dispatcher.DispatchAsync(new(1, Guid.NewGuid(), Command.ClientHello), CancellationToken.None);
        var request = new PlatformRequest(1, Guid.NewGuid(), Command.GetCoolingReadOnlySnapshot);
        await dispatcher.DispatchAsync(request, CancellationToken.None);
        var duplicate = await dispatcher.DispatchAsync(request, CancellationToken.None);
        Assert.AreEqual(PlatformError.DuplicateRequestId, duplicate.Error); Assert.AreEqual(1, cooling.Calls);
    }
    private static RequestDispatcher Dispatcher(ICoolingReadOnlyProvider cooling) => new(new AsusCapabilityDetector(new Inventory()),
        new(1, "Fixture", new("S-1-5-18", 100, 0, true)), cooling);
    private sealed class CountingCooling : ICoolingReadOnlyProvider
    {
        public int Calls;
        public Task<CoolingRuntimeSnapshot> ReadAsync(CancellationToken cancellationToken = default)
        { Calls++; return Task.FromResult(CoolingRuntimeSnapshot.Unobserved(PlatformError.ComponentMissing, "Fixture")); }
    }
}
