using AceHFX.AsusPlatform.Aura;
namespace AsusPlatform.Tests;

[TestClass]
public sealed class LightingBackendTests
{
    private static LightingBackendCapability Metadata(string name) => new(name, true, true,
        LightingRuntimeQualification.MetadataValidated, false, false, false, "FixtureMetadata");

    [TestMethod]
    public void InstalledAndRegisteredDoNotOverrideEnumerationFailure()
    {
        var sdk = LightingBackendSelection.UnreliableAuraSdk(true, true, "M2.7 host evidence");
        Assert.IsTrue(sdk.Installed == true && sdk.Registered == true);
        Assert.AreEqual(LightingRuntimeQualification.EnumerationUnreliable, sdk.Runtime);
        Assert.IsNull(LightingBackendSelection.Evaluate([sdk]).SelectedOutputBackend);
    }

    [TestMethod]
    public void MetadataPreferredWithoutImplyingWriteReadiness()
    {
        var decision = LightingBackendSelection.Evaluate([Metadata("LightingServiceMediator")]);
        Assert.AreEqual("LightingServiceMediator", decision.PreferredMetadataBackend);
        Assert.IsNull(decision.SelectedOutputBackend);
        Assert.IsFalse(decision.WriteGateExecutable);
    }

    [TestMethod]
    public void OutputRequiresProtocolAddressingAndRestoreTogether()
    {
        var baseValue = Metadata("WDL");
        foreach (var candidate in new[] { baseValue, baseValue with { OutputProtocolValidated = true },
            baseValue with { OutputProtocolValidated = true, DeviceAddressingValidated = true } })
            Assert.IsNull(LightingBackendSelection.Evaluate([candidate]).SelectedOutputBackend);
        var ready = baseValue with { OutputProtocolValidated = true, DeviceAddressingValidated = true, RestoreValidated = true };
        Assert.AreEqual("WDL", LightingBackendSelection.Evaluate([ready]).SelectedOutputBackend);
        Assert.IsFalse(LightingBackendSelection.Evaluate([ready]).WriteGateExecutable);
        Assert.IsNull(LightingBackendSelection.Evaluate([ready, ready with { Name = "Other" }]).SelectedOutputBackend);
    }

    [TestMethod]
    public void ConfidenceAndPhysicalIdentityAreIndependent()
    {
        var d = new LightingDeviceDescriptor("mediator:GPU0", "GPU0", "Vga", "VGA", 23,
            LightingEvidenceConfidence.RuntimeExact, "Actual returned metadata", "Unknown physical serial");
        Assert.AreEqual(LightingEvidenceConfidence.RuntimeExact, d.Confidence);
        Assert.AreEqual("Unknown physical serial", d.PhysicalIdentityEvidence);
        Assert.AreNotEqual(d.SourceQualifiedId, (d with { SourceQualifiedId = "config:GPU0" }).SourceQualifiedId);
    }
}
