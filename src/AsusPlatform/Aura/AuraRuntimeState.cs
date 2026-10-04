namespace AceHFX.AsusPlatform.Aura;

public enum AuraOwnershipState { Unavailable = 0, Idle = 1, OwnedByVendorEngine = 2,
    OwnedByWindowsDynamicLighting = 3, AvailableForClaim = 4, ClaimPending = 5,
    OwnedByAceHFXAura = 6, ReleasePending = 7, RecoveryPending = 8, Unknown = 9 }
public enum MappingConfidence { Exact = 0, Strong = 1, Probable = 2, Unknown = 3 }
public sealed record AuraValue<T>(ExecutionState Execution, T? Value, int? HResult = null);
public sealed record AuraLightDescriptor(int Index, AuraValue<string>? Name, AuraValue<byte?>? Red,
    AuraValue<byte?>? Green, AuraValue<byte?>? Blue, AuraValue<uint?>? Color, AuraValue<uint?>? LocationId,
    ExecutionState Execution);
public sealed record AuraZoneDescriptor(string Name, uint? LocationId, IReadOnlyList<int> LightIndices,
    MappingConfidence Confidence, string Reason);
public sealed record AuraDeviceDescriptor(string RuntimeId, int RuntimeIndex, uint Category, int? ComIdentityIndex,
    string? StableDeviceId, AuraValue<uint?>? Type, AuraValue<string>? Name, AuraValue<uint?>? Width,
    AuraValue<uint?>? Height, AuraValue<int?>? LightCount, IReadOnlyList<AuraLightDescriptor> Lights,
    IReadOnlyList<AuraZoneDescriptor> Zones, IReadOnlyList<uint> AlsoSeenInCategories,
    IReadOnlyList<string> Anomalies, ExecutionState Execution);
public sealed record AuraEnumeration(uint Category, ExecutionState Execution, int? HResult,
    AuraValue<int?> Count, IReadOnlyList<AuraDeviceDescriptor> Devices);
public sealed record AuraRuntimeCapabilities(Evidence Installed, Evidence Registered, Evidence ServiceRunning,
    Evidence WorkerReachable, Evidence ComActivated, Evidence EnumerationSucceeded, Evidence DevicesFound,
    Evidence ReadableTopology, Evidence OwnershipApiPresent, Evidence OwnershipValidated,
    Evidence RealtimeBackendDetected, Evidence RealtimeBackendValidated);
public sealed record LampArrayDescriptor(string DeviceId, string Name, bool Enabled, bool? FalchionAceHfx,
    string IdentityReason);
public sealed record DynamicLightingObservation(ExecutionState Execution, IReadOnlyDictionary<string,string> UserSettings,
    Evidence Enabled, IReadOnlyList<LampArrayDescriptor> LampArrays, Evidence WindowsAmbientControl,
    Evidence AuraParticipation, string Reason);
public sealed record LightingTopologyZone(string DeviceKey, string LightKey, uint? LocationId, string? Name, string? Type);
public sealed record LightingTopologyObservation(ExecutionState Execution, string Source, string? Sha256,
    IReadOnlyList<LightingTopologyZone> Zones, IReadOnlyDictionary<string,string> LastSyncList,
    IReadOnlyDictionary<string,string> LastUnsyncList, string Reason);
public sealed record AuraRuntimeState(Guid BackendInstanceId, DateTimeOffset ObservedAt, int? WorkerPid,
    PlatformError Error, string Diagnostic, ExecutionState Activation, int? ActivationHResult,
    ExecutionState Enumeration, int? DeviceCount, int? UniqueComIdentityCount,
    IReadOnlyList<AuraEnumeration> Categories, AuraRuntimeCapabilities Capabilities,
    DynamicLightingObservation DynamicLighting, LightingTopologyObservation LightingTopology,
    AuraOwnershipState Ownership, IReadOnlyDictionary<string,double> StageMilliseconds,
    AuraValue<bool?> Sdk2Support, AuraValue<bool?> Sdk3Support);
public interface IAuraBackend { Task<AuraRuntimeState> DiscoverAsync(CancellationToken token = default); }
