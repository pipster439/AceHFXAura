namespace AceHFX.AsusPlatform;

// Explicit values are wire contracts. Available means the specified evidence was detected,
// never permission to control hardware. Accessible/Validated are independent evidence.
public enum CapabilityState { Unknown = 0, Available = 1, Unavailable = 2, PermissionDenied = 3,
    InstalledButStopped = 4, VersionUnsupported = 5 }
public enum ExecutionState { NotAttempted = 0, Succeeded = 1, Failed = 2, PermissionDenied = 3, Unavailable = 4, TimedOut = 5 }
public enum PlatformError { None = 0, ServiceUnavailable = 1, ComponentMissing = 2, AccessDenied = 3,
    ProtocolMismatch = 4, UnauthorizedClient = 5, MalformedRequest = 6, RequestTooLarge = 7,
    Timeout = 8, UnsupportedCapability = 9, VendorApiFailure = 10, InternalError = 11,
    DuplicateRequestId = 12, TruncatedFrame = 13, VendorTimeout = 14, WorkerCrashed = 15,
    AuraWriteApprovalRequired = 16 }

public sealed record Evidence(CapabilityState State, ExecutionState Execution, string Reason, bool? Value = null)
{
    public static Evidence NotAttempted(string reason) => new(CapabilityState.Unknown, ExecutionState.NotAttempted, reason);
    public static Evidence Detected(bool value, string reason) => new(value ? CapabilityState.Available : CapabilityState.Unavailable,
        ExecutionState.Succeeded, reason, value);
    public static Evidence Denied(string reason) => new(CapabilityState.PermissionDenied, ExecutionState.PermissionDenied, reason);
    public static Evidence Unknown(string reason) => new(CapabilityState.Unknown, ExecutionState.Failed, reason);
}
public sealed record ComponentVersion(string? Value, ExecutionState Execution, string Compatibility, string Reason);
public sealed record ComponentStatus(string Name, Evidence Installed, Evidence Registered, Evidence Running,
    Evidence Accessible, Evidence Validated, ComponentVersion Version);
public sealed record AuraBackendDescriptor(Evidence StackDetected, Evidence Usable);
public sealed record FanBackendDescriptor(Evidence StackDetected, Evidence Usable, bool RequiresPrivilegedBroker);
public sealed record AuraCapabilities(ComponentStatus AuraSdk, ComponentStatus LightingService,
    ComponentStatus ServiceMediator, Evidence SharedMemorySupportDetected, Evidence DynamicLightingDetected,
    AuraBackendDescriptor Backend);
public sealed record CoolingCapabilities(ComponentStatus FanControlService, ComponentStatus FanControlCom,
    ComponentStatus AsIo3, ComponentStatus AsusComService, FanBackendDescriptor Backend);
public sealed record SecurityContext(string Sid, int ProcessId, int SessionId, bool IsSystem);
public sealed record AsusPlatformCapabilities(DateTimeOffset ObservedAt, AuraCapabilities Aura, CoolingCapabilities Cooling,
    IReadOnlyList<ComponentStatus> Services, IReadOnlyList<ComponentStatus> Components, SecurityContext SecurityContext);
public sealed record AsusPlatformStatus(ComponentStatus Broker, bool IpcReachable, PlatformError Error,
    string? Diagnostic, AsusPlatformCapabilities? Capabilities);

public interface IAsusCapabilityProvider { AsusPlatformCapabilities Discover(); }
public interface IAsusPlatform
{
    Task<AsusPlatformStatus> GetStatusAsync(CancellationToken cancellationToken = default);
    Task<Cooling.CoolingRuntimeSnapshot> GetCoolingAsync(CancellationToken cancellationToken = default) =>
        Task.FromResult(Cooling.CoolingRuntimeSnapshot.Unobserved(PlatformError.UnsupportedCapability, "CoolingReadNotSupported"));
}
