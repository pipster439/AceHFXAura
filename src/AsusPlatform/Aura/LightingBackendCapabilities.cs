namespace AceHFX.AsusPlatform.Aura;

// A proposal-facing read-only contract. No frame, ownership, lease or vendor-call method.
public interface IAuraLightingBackend
{
    Task<LightingBackendCapability> GetCapabilitiesAsync(CancellationToken cancellationToken);
    Task<IReadOnlyList<LightingDeviceDescriptor>> GetTopologyAsync(CancellationToken cancellationToken);
}

public enum LightingEvidenceConfidence { Unknown = 0, StrongCorrelation = 1, ConfigExact = 2, RuntimeExact = 3 }
public enum LightingRuntimeQualification { NotAttempted = 0, MetadataValidated = 1, EnumerationUnreliable = 2, Unavailable = 3 }
public sealed record LightingDeviceDescriptor(string SourceQualifiedId, string? VendorIdentifier,
    string? DisplayName, string? DeviceType, int? LogicalLightCount, LightingEvidenceConfidence Confidence,
    string Evidence, string PhysicalIdentityEvidence);

public sealed record LightingBackendCapability(string Name, bool? Installed, bool? Registered,
    LightingRuntimeQualification Runtime, bool OutputProtocolValidated, bool DeviceAddressingValidated,
    bool RestoreValidated, string Evidence);

public sealed record LightingBackendDecision(string? PreferredMetadataBackend, string? SelectedOutputBackend,
    bool WriteGateExecutable, string Reason);

public static class LightingBackendSelection
{
    // Host evidence is supplied by the caller; an installed binary/version alone does not qualify output.
    public static LightingBackendDecision Evaluate(IReadOnlyList<LightingBackendCapability> candidates)
    {
        var metadata = candidates.FirstOrDefault(c => c.Name == "LightingServiceMediator" &&
            c.Runtime == LightingRuntimeQualification.MetadataValidated);
        var output = candidates.Where(c => c.Runtime == LightingRuntimeQualification.MetadataValidated &&
            c.OutputProtocolValidated && c.DeviceAddressingValidated && c.RestoreValidated).ToArray();
        return output.Length == 1
            ? new(metadata?.Name, output[0].Name, false, "OutputCandidateRequiresSeparateHumanApproval")
            : new(metadata?.Name, null, false, output.Length == 0 ? "OutputOrRestoreEvidenceMissing" : "AmbiguousOutputCandidates");
    }

    public static LightingBackendCapability UnreliableAuraSdk(bool installed, bool registered, string observedEvidence) =>
        new("AuraSdk", installed, registered, LightingRuntimeQualification.EnumerationUnreliable,
            false, false, false, observedEvidence);
}
