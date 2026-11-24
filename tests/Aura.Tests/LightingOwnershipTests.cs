using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class LightingOwnershipTests
{
    [TestMethod]
    public void DefaultsRemainBackendDependentAndLegacyReferenceUnchanged()
    {
        var doc = ProfileJson.NewDefault("desktop");
        var p = doc.Profiles[0];
        Assert.AreEqual("AuraDirectRgb", p.EffectiveLightingOwner);
        Assert.IsFalse(ProfileJson.Serialize(doc).Contains("control_owner"));
        p.ActivationBackend = "hardware_slot"; p.HardwareSlot = 5;
        Assert.AreEqual("FirmwareBank", p.EffectiveLightingOwner);
        Assert.AreEqual("FirmwareBank", ProfileJson.Deserialize(ProfileJson.Serialize(doc)).Profiles[0].EffectiveLightingOwner);
        Assert.AreEqual("LegacyUnmanaged", p.Lighting.Ownership);
        Assert.AreEqual("desktop", p.Lighting.LegacyEffectReference);
    }

    [TestMethod]
    public void RoundTripExportImportCloneRetainsOwnerRevisionAndOpaqueFields()
    {
        foreach (var backend in new[] { "host_managed", "hardware_slot" })
        foreach (var owner in new[] { "AuraDirectRgb", "FirmwareBank" }) {
            var doc = ProfileJson.NewDefault("desktop"); doc.Revision = 17;
            var p = doc.Profiles[0]; p.ActivationBackend = backend; p.HardwareSlot = backend == "hardware_slot" ? 5 : null;
            p.Lighting.ControlOwner = owner;
            p.Lighting.Extensions = new() { ["future_lighting"] = JsonSerializer.SerializeToElement(new { keep = 42 }) };
            var imported = ProfileJson.Deserialize(ProfileJson.Serialize(doc));
            Assert.AreEqual(17L, imported.Revision);
            var clone = imported.Profiles[0].Clone();
            Assert.AreEqual(owner, clone.EffectiveLightingOwner);
            Assert.AreEqual(42, clone.Lighting.Extensions!["future_lighting"].GetProperty("keep").GetInt32());
            Assert.AreEqual("LegacyUnmanaged", clone.Lighting.Ownership);
            Assert.AreEqual("desktop", clone.Lighting.LegacyEffectReference);
        }
    }

    [TestMethod]
    public void UnknownOwnerRejectedAndStatusDoesNotProveFirmwareRestoration()
    {
        var doc = ProfileJson.NewDefault(); doc.Profiles[0].Lighting.ControlOwner = "future";
        Assert.Throws<InvalidDataException>(() => ProfileJson.Serialize(doc));
        var status = JsonSerializer.Deserialize<HardwareSlotStatus>(
            "{\"lighting_owner\":\"FirmwareBank\",\"desired_lighting_owner\":\"AuraDirectRgb\",\"lighting_diagnostic\":\"FirmwareLightingRestoreUnverified\"}", ProfileJson.Options)!;
        Assert.AreEqual("FirmwareBank", status.LightingOwner);
        Assert.AreEqual("AuraDirectRgb", status.DesiredLightingOwner);
        Assert.AreEqual("FirmwareLightingRestoreUnverified", status.LightingDiagnostic);
    }
}
