namespace AceHFX.AsusPlatform.Cooling;

// Stable wire values; missing observations never become zero telemetry.
public enum FanObservationState { NotAttempted = 0, Succeeded = 1, Failed = 2, PermissionDenied = 3,
    TimedOut = 4, Unavailable = 5, Unsupported = 6, Unknown = 7 }
public enum FanEvidenceConfidence { Unknown = 0, RuntimeExact = 1, ConfigExact = 2, StrongCorrelation = 3 }
public enum FanSafetyClass { Unknown = 0, CriticalCpu = 1, CoolingCritical = 2, Chassis = 3,
    Auxiliary = 4, NonControllableSensor = 5 }
public sealed record FanValue<T>(FanObservationState State, T? Value, int? Hresult = null,
    double? LatencyMs = null, string? Reason = null);
public sealed record FanCurvePointDescriptor(int Index, FanValue<int?> Temperature, FanValue<int?> Speed);
public sealed record FanCurveDescriptor(FanValue<int?> PointCount, IReadOnlyList<FanCurvePointDescriptor> Points,
    string Units = "VendorRaw; scaling not validated");
public sealed record FanChannelDescriptor(string StableId, int CollectionIndex, FanValue<uint?> VendorId,
    FanValue<string> Name, FanValue<int?> CurrentRpm, FanValue<byte?> DutyRaw,
    FanValue<bool?> RpmMode, FanValue<bool?> FanStopState, FanValue<uint?> SourceIndex,
    FanValue<uint?> CurrentProfile, FanCurveDescriptor? CurrentCurve, FanValue<byte?> MinimumDutyRaw,
    FanSafetyClass SafetyClass = FanSafetyClass.Unknown,
    FanEvidenceConfidence Confidence = FanEvidenceConfidence.RuntimeExact)
{
    public FanValue<bool?> ManualMode { get; init; } = new(FanObservationState.Unsupported, null,
        Reason: "NoManualModeGetterInCurrentFanTypeLib");
    public FanValue<int?> NameId { get; init; } = new(FanObservationState.Unsupported, null,
        Reason: "NoNameIdGetter; config mapping requires separate evidence");
    public bool? ControlSupported { get; init; }
}
public sealed record FanSensorDescriptor(string StableId, string Name, FanValue<int?> CurrentValue,
    string Unit, string SourceType, bool? ControlSupported = null, string? ChannelStableId = null,
    FanEvidenceConfidence Confidence = FanEvidenceConfidence.Unknown);
public sealed record CoolingRuntimeCapabilities(FanObservationState WorkerReachable,
    FanObservationState ComActivated, FanObservationState Enumeration, int? ControlChannelCount,
    int? RpmSensorCount, int? TemperatureSourceCount, FanObservationState CurveReadSupported,
    FanObservationState WriteContractValidated);
public sealed record CoolingRuntimeSnapshot(int SchemaVersion, DateTimeOffset ObservedAt,
    SecurityContext? WorkerIdentity, FanObservationState Registered, FanObservationState ActivationStarted,
    FanObservationState Activation, FanObservationState InterfaceAcquired, FanObservationState InvocationStarted,
    FanObservationState Invocation, int? ActivationHresult, double? ActivationLatencyMs,
    FanValue<uint?>? ServiceVersion, FanValue<uint?>? VendorFanCount, FanValue<int?>? NativePointCount,
    FanValue<bool?>? TenPointCapability, CoolingRuntimeCapabilities Capabilities,
    IReadOnlyList<FanChannelDescriptor> Channels, IReadOnlyList<FanSensorDescriptor> Sensors,
    PlatformError Error, string? Diagnostic)
{
    public int? CoInitializeHresult { get; init; }
    public string Apartment { get; init; } = "STA";
    public static CoolingRuntimeSnapshot Unobserved(PlatformError error, string reason,
        FanObservationState worker = FanObservationState.NotAttempted) => new(1, DateTimeOffset.UtcNow,
        null, FanObservationState.NotAttempted, FanObservationState.NotAttempted, FanObservationState.NotAttempted,
        FanObservationState.NotAttempted, FanObservationState.NotAttempted, FanObservationState.NotAttempted,
        null, null, null, null, null, null,
        new(worker, FanObservationState.NotAttempted, FanObservationState.NotAttempted, null, null, null,
            FanObservationState.NotAttempted, FanObservationState.NotAttempted), [], [], error, reason);
}
public interface ICoolingReadOnlyProvider
{
    Task<CoolingRuntimeSnapshot> ReadAsync(CancellationToken cancellationToken = default);
}
public sealed record FanWorkerReadRequest(int SchemaVersion, Guid RequestId, string Command);
public static class FanReadProtocol
{
    public const int SchemaVersion = 1;
    public const int MaximumChannels = 32;
    public const int MaximumCurvePoints = 16;
    public static readonly TimeSpan WorkerTimeout = TimeSpan.FromSeconds(3);
    public static readonly TimeSpan MinimumPollInterval = TimeSpan.FromSeconds(1);
    public const string WorkerFileName = "AceHFXFanWorker.exe";
}

// Pure decision model only. It cannot invoke a vendor API or acquire a lease.
public sealed record FanSafetyEvidence(FanSafetyClass Class, bool UniqueMapping, bool LiveRpmVerified,
    bool ControlSupported, bool MinimumDutyVerified, bool RestoreContractVerified, bool ThermalAbortVerified);
public static class FanGateF1
{
    public static IReadOnlyList<string> Blockers(FanSafetyEvidence evidence)
    {
        List<string> reasons = [];
        if (evidence.Class is not (FanSafetyClass.Chassis or FanSafetyClass.Auxiliary)) reasons.Add("FirstTargetMustBeNonCritical");
        if (!evidence.UniqueMapping) reasons.Add("UniquePhysicalMappingMissing");
        if (!evidence.LiveRpmVerified) reasons.Add("LiveRpmMissing");
        if (!evidence.ControlSupported) reasons.Add("ControlSupportUnverified");
        if (!evidence.MinimumDutyVerified) reasons.Add("SafeMinimumDutyMissing");
        if (!evidence.RestoreContractVerified) reasons.Add("DeterministicRestoreMissing");
        if (!evidence.ThermalAbortVerified) reasons.Add("ThermalAbortMissing");
        return reasons;
    }
}
