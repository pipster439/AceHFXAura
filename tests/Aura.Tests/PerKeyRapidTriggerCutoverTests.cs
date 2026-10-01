using System.Text.Json;
using Aura_WinUI.Services;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Aura.Tests;

[TestClass]
public sealed class PerKeyRapidTriggerCutoverTests
{
    [TestMethod]
    public void PartialSessionSubmissionDoesNotInventMissingReleaseSensitivity()
    {
        var partial = JsonSerializer.Deserialize<MagneticRapidTriggerValue>("""
        {"logical_id":1793,"enabled":true,"press_raw":5,"release_raw":0,
        "press_known":true,"release_known":false,"continuous":false,"source":"SessionApplied"}
        """)!;
        Assert.IsTrue(partial.PressKnown);
        Assert.IsFalse(partial.ReleaseKnown);
        var legacyPair = JsonSerializer.Deserialize<MagneticRapidTriggerValue>("""
        {"logical_id":1793,"enabled":true,"press_raw":5,"release_raw":15,"source":"SessionApplied"}
        """)!;
        Assert.IsTrue(legacyPair.PressKnown && legacyPair.ReleaseKnown);
    }

    [TestMethod]
    public void UnmanagedAndExplicitDisabledAreDistinctAndExtensionsSurvive()
    {
        const string json = """
        {"schema_version":1,"name":"RT","magnetic":{"keys":[
          {"logical_id":1793,"rapid_trigger":{"enabled":false,"press_mm":0.5,"release_mm":1.5,"separate_mode":true,"continuous":false,"future_rt":{"value":7}}},
          {"logical_id":1538,"rapid_trigger":null}]}}
        """;
        var profile = JsonSerializer.Deserialize<DeviceProfile>(json, ProfileJson.Options)!;
        var clone = profile.Clone();
        Assert.IsNotNull(clone.Magnetic.Keys[0].RapidTrigger);
        Assert.IsFalse(clone.Magnetic.Keys[0].RapidTrigger!.Enabled);
        Assert.IsNull(clone.Magnetic.Keys[1].RapidTrigger);
        Assert.AreEqual(7, clone.Magnetic.Keys[0].RapidTrigger!.Extensions!["future_rt"].GetProperty("value").GetInt32());
        Assert.AreEqual(JsonValueKind.False, clone.Magnetic.Keys[0].RapidTrigger!.Extensions!["continuous"].ValueKind);
        Assert.IsEmpty(ProfileValidator.RapidTriggerIssues(clone.Magnetic));
    }

    [TestMethod]
    public void LegacyDataIsPreservedWithoutExpandingKeysOrReinterpretingFields()
    {
        var profile = new DeviceProfile {
            Magnetic = new ProfileMagnetic { GlobalRapidTrigger = new(true, 0.5, 1.5, true, 0.2, 0.3) }
        };
        var clone = profile.Clone();
        Assert.AreEqual(profile.Magnetic.GlobalRapidTrigger, clone.Magnetic.GlobalRapidTrigger);
        Assert.AreEqual(0.2, clone.Magnetic.GlobalRapidTrigger!.TopMm);
        Assert.AreEqual(0.3, clone.Magnetic.GlobalRapidTrigger.BottomMm);
        Assert.IsEmpty(clone.Magnetic.Keys);
    }

    [TestMethod]
    public void ContinuousOnIsRetainedButFailsValidation()
    {
        var profile = JsonSerializer.Deserialize<DeviceProfile>("""
        {"name":"RT","magnetic":{"keys":[{"logical_id":1793,"rapid_trigger":
        {"enabled":true,"press_mm":0.5,"release_mm":1.5,"separate_mode":true,"continuous":true}}]}}
        """, ProfileJson.Options)!;
        var issues = ProfileValidator.RapidTriggerIssues(profile.Magnetic);
        Assert.HasCount(1, issues);
        Assert.AreEqual("RtContinuousUnsupported", issues[0].Code);
        Assert.AreEqual(JsonValueKind.True, profile.Clone().Magnetic.Keys[0].RapidTrigger!.Extensions!["continuous"].ValueKind);
    }

    [TestMethod]
    public void NewExplicitRtSerializationDoesNotRecreateLegacyField()
    {
        var profile = new DeviceProfile();
        profile.Magnetic.Keys.Add(new ProfileKey { LogicalId = 0x0701,
            RapidTrigger = new(false, 0.5, 1.5, true) });
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(profile, ProfileJson.Options));
        var magnetic = json.RootElement.GetProperty("magnetic");
        Assert.IsFalse(magnetic.TryGetProperty("global_rapid_trigger", out _));
        var clone = profile.Clone();
        Assert.IsFalse(clone.Magnetic.Keys.Single().RapidTrigger!.Enabled);
    }

    [TestMethod]
    public void LegacyEditorIsReadOnlyAndManualPageDoesNotOfferGlobalRtModel()
    {
        var root = new DirectoryInfo(AppContext.BaseDirectory);
        while (root is not null && !File.Exists(Path.Combine(root.FullName, "winui", "Pages", "ProfilesPage.xaml"))) root = root.Parent;
        Assert.IsNotNull(root);
        var manual = File.ReadAllText(Path.Combine(root.FullName, "winui", "Pages", "MagneticSwitchPage.xaml.cs"));
        Assert.DoesNotContain("全局快速触发", manual);
        StringAssert.Contains(manual, "旧版快速触发参数（只读）");
        StringAssert.Contains(manual, "PressSlider.IsEnabled = false;");
        var page = File.ReadAllText(Path.Combine(root.FullName, "winui", "Pages", "ProfilesPage.xaml.cs"));
        StringAssert.Contains(page, "GlobalRtEnabled.IsEnabled = false;");
        StringAssert.Contains(page, "case \"g_rt\": if (!toggle.IsOn) magnetic.GlobalRapidTrigger = null;");
    }
}
