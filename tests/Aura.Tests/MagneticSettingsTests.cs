using Aura_WinUI.Services;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Text.Json;

namespace Aura.Tests;

[TestClass]
public sealed class MagneticSettingsTests
{
    private static readonly ushort[] ExpectedLogicalIds =
    [
        0x0100, 0x0600, 0x0700, 0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0106,
        0x0107, 0x0108, 0x0109, 0x010a, 0x0706, 0x0608,
        0x0200, 0x0601, 0x0701, 0x0201, 0x0202, 0x0203, 0x0204, 0x0205,
        0x0206, 0x0207, 0x0208, 0x0209, 0x020a, 0x0607, 0x0708,
        0x0300, 0x0602, 0x0702, 0x0301, 0x0302, 0x0303, 0x0304,
        0x0305, 0x0306, 0x0307, 0x0308, 0x0309, 0x0707, 0x060b,
        0x0400, 0x0703, 0x0401, 0x0501, 0x0402, 0x0403, 0x0404,
        0x0405, 0x0406, 0x0407, 0x0408, 0x040a, 0x070a, 0x070b,
        0x0500, 0x0604, 0x0704, 0x0503, 0x0507, 0x0508, 0x050a,
        0x060a, 0x050b, 0x040b
    ];

    private static MagneticStatus Clean() => new() { Status = "ok", ApiVersion = 1, Health = "Clean", Available = true };

    [TestMethod]
    public void LayoutHasExactlyTheAudited68LogicalKeysAndSpecialKeys()
    {
        var actual = MagneticKeyLayout.Keys.Select(key => key.LogicalId).ToArray();
        Assert.HasCount(68, actual);
        Assert.HasCount(68, actual.Distinct());
        CollectionAssert.AreEquivalent(ExpectedLogicalIds, actual);
        Assert.AreEqual("Fn", MagneticKeyLayout.Find(0x0508)!.FullName);
        Assert.AreEqual("Copilot", MagneticKeyLayout.Find(0x050a)!.FullName);
        Assert.AreEqual("Cop", MagneticKeyLayout.Find(0x050a)!.Label);
        Assert.AreEqual(1.0, MagneticKeyLayout.Find(0x050a)!.Units);
        Assert.AreEqual("Right Shift", MagneticKeyLayout.Find(0x040a)!.FullName);
        Assert.IsNull(MagneticKeyLayout.Find(0xffff));
    }

    [TestMethod]
    public void PageDownAlignsUnderPageUpAndCopilotSitsBeforeArrowCluster()
    {
        static double X(ushort id) => MagneticKeyLayout.LeftOf(MagneticKeyLayout.Find(id)!, 38);
        Assert.AreEqual(X(0x0608), X(0x0708), 0.01);
        Assert.AreEqual(X(0x0608), X(0x060b), 0.01);
        Assert.AreEqual(X(0x0608), X(0x070b), 0.01);
        Assert.AreEqual(X(0x070a), X(0x050b), 0.01);
        Assert.AreEqual(X(0x0608), X(0x040b), 0.01);
        Assert.IsLessThan(X(0x050a), X(0x0508)); // value Fn < upper bound Copilot
        Assert.IsLessThan(X(0x060a), X(0x050a)); // value Copilot < upper bound Left Arrow
    }

    [TestMethod]
    public async Task SelectionAndSliderEditsNeverSubmitUntilExplicitCommit()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        Assert.IsFalse(model.Select(0xffff));
        Assert.IsFalse(await model.ApplyActuationAsync());
        Assert.IsTrue(model.Select(0x0402));
        Assert.AreEqual("V", model.SelectedKey!.FullName);
        model.EditActuation(4.0);
        model.EditActuation(1.0);
        Assert.IsEmpty(fake.Calls);
        Assert.IsTrue(await model.ApplyActuationAsync());
        CollectionAssert.AreEqual(new[] { "actuation:1026:1.0" }, fake.Calls);
    }

    [TestMethod]
    public async Task RapidTriggerEnableRoutesValuesAndDisableRequiresAuthoritativeInheritance()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync(); model.Select(0x0602);
        model.EditRapidTrigger(true, 0.8, 0.6);
        Assert.IsEmpty(fake.Calls);
        Assert.IsFalse(await model.ApplyRapidTriggerAsync()); // unresolved DKS state needs confirmation
        Assert.IsTrue(await model.ApplyRapidTriggerAsync(resolveDks: true));
        model.EditRapidTrigger(false, null, null);
        Assert.IsFalse(model.CanDisableRapidTrigger);
        Assert.IsFalse(await model.ApplyRapidTriggerAsync());
        CollectionAssert.AreEqual(new[] { "rt-on:1538:0.8:0.6:True" }, fake.Calls);

        var withInheritance = new MagneticSettingsModel(fake, _ => (0.4, 0.2));
        await withInheritance.RefreshAsync(); withInheritance.Select(0x0602);
        withInheritance.EditRapidTrigger(false, null, null);
        Assert.IsTrue(await withInheritance.ApplyRapidTriggerAsync());
        Assert.AreEqual("rt-off:1538", fake.Calls.Last());
    }

    [TestMethod]
    public async Task DeadzoneRoutesTopThenBottomAndUnknownValuesAreNeverGuessed()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync(); model.Select(0x0402);
        model.EditDeadzone(0.2, null);
        Assert.IsFalse(await model.ApplyDeadzoneAsync());
        model.EditDeadzone(null, 0.3);
        Assert.IsTrue(await model.ApplyDeadzoneAsync());
        CollectionAssert.AreEqual(new[] { "deadzone:1026:0.2:0.3" }, fake.Calls);
    }

    [TestMethod]
    public async Task InFlightSubmissionIsBoundedAndQuarantineDisablesAllWrites()
    {
        var fake = new Fake();
        var pending = new TaskCompletionSource<MagneticStatus>(TaskCreationOptions.RunContinuationsAsynchronously);
        fake.Next = pending.Task;
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync(); model.Select(0x0402); model.EditActuation(1.0);
        var first = model.ApplyActuationAsync();
        Assert.IsTrue(model.Busy);
        model.EditActuation(2.0);
        Assert.IsFalse(await model.ApplyActuationAsync());
        Assert.HasCount(1, fake.Calls);
        pending.SetResult(new MagneticStatus { Status = "error", ApiVersion = 1,
            Health = "IndeterminateStagedState", Available = true });
        Assert.IsFalse(await first);
        Assert.AreEqual(1.0, model.Draft?.ActuationMm); // failed submission keeps the user's draft
        Assert.IsFalse(model.CanWrite);
        model.EditDeadzone(0.2, 0.3);
        Assert.IsFalse(await model.ApplyDeadzoneAsync());
        Assert.HasCount(1, fake.Calls);
    }

    [TestMethod]
    public async Task SuccessfulApplyClearsDirtyDraftAndUsesReturnedAggregatedState()
    {
        var fake = new Fake();
        fake.Next = Task.FromResult(new MagneticStatus {
            Status = "ok", ApiVersion = 1, Health = "Clean", Available = true,
            Actuation = [new MagneticActuationValue { LogicalId = 0x0402, Raw = 40, Source = "SessionApplied" }]
        });
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync(); model.Select(0x0402); model.EditActuation(4.0);
        Assert.IsTrue(await model.ApplyActuationAsync());
        Assert.IsFalse(model.Busy);
        Assert.IsNull(model.Draft?.ActuationMm);
        Assert.AreEqual((byte)40, model.Status!.Actuation.Single().Raw);
        Assert.AreEqual("SessionApplied", model.Status.Actuation.Single().Source);
    }

    [TestMethod]
    public async Task HostProfileZeroAndSessionAppliedProvenanceStayDistinct()
    {
        var fake = new Fake();
        fake.Status.HostProfile.GlobalDeadzoneTop = new MagneticKnownRaw { Known = true, Raw = 0, Source = "HostProfile" };
        fake.Status.HostProfile.GlobalDeadzoneBottom = new MagneticKnownRaw { Known = true, Raw = 1, Source = "HostProfile" };
        fake.Status.HostProfile.GlobalRtPress = new MagneticKnownRaw { Known = true, Raw = 4, Source = "HostProfile" };
        fake.Status.HostProfile.GlobalRtRelease = new MagneticKnownRaw { Known = true, Raw = 2, Source = "HostProfile" };
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync(); model.Select(0x0402);
        Assert.IsTrue(model.CanResetAllDeadzone);
        model.EditRapidTrigger(false, null, null);
        Assert.IsTrue(model.CanDisableRapidTrigger);
        Assert.IsTrue(await model.ApplyRapidTriggerAsync());
        Assert.AreEqual("rt-off:1026", fake.Calls.Last());
    }

    [TestMethod]
    public async Task DksDraftRequiresFourValidSlotsAndExplicitConflictResolution()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync(); model.Select(0x0402);
        model.EditDksThresholds(1.0, 3.6);
        Assert.IsFalse(model.EditDksSlot(0, 0xffff, "Tap", "Hold", "Inactive", "Release"));
        Assert.IsTrue(model.EditDksSlot(0, 0x0602, "Tap", "Hold", "Inactive", "Release"));
        Assert.IsEmpty(fake.Calls);
        Assert.IsFalse(await model.ApplyDksAsync());
        Assert.IsTrue(await model.ApplyDksAsync(resolveRt: true));
        Assert.AreEqual("dks:1026:1.0:3.6:4:True", fake.Calls.Last());
        model.EditDksThresholds(4.0, 1.0);
        Assert.IsFalse(await model.ApplyDksAsync(resolveRt: true));
        Assert.HasCount(1, fake.Calls);
        Assert.IsTrue(await model.RestoreDksStandardAsync());
        Assert.AreEqual("dks-standard:1026", fake.Calls.Last());
    }

    [TestMethod]
    public async Task SelectingKnownSessionDksClonesAllFourSlotsWithoutWriting()
    {
        var fake = new Fake();
        fake.Status.Dks.Add(new MagneticDksValue {
            LogicalId = 0x0402, StartRaw = 10, EndRaw = 36, Source = "SessionApplied",
            Slots = [
                new MagneticDksSlot { Target = new MagneticDksTarget { Kind = "LogicalKey", LogicalId = 0x0602 }, DownStart = "Tap" },
                new MagneticDksSlot(), new MagneticDksSlot(), new MagneticDksSlot()
            ]
        });
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        Assert.IsTrue(model.Select(0x0402));
        Assert.AreEqual(1.0, model.Draft?.DksStartMm);
        Assert.AreEqual(3.6, model.Draft?.DksEndMm);
        Assert.AreEqual((ushort)0x0602, model.Draft?.DksSlots[0].Target.LogicalId);
        Assert.AreEqual("Tap", model.Draft?.DksSlots[0].DownStart);
        Assert.IsFalse(model.Draft!.DksDirty);
        Assert.IsEmpty(fake.Calls);
    }

    [TestMethod]
    public async Task SpeedTapMasterPairAndBaselineRemainSeparate()
    {
        var fake = new Fake();
        fake.Status.SpeedTap.SavedProfilePairsKnown = true;
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.EditSpeedTapPair(0x0602, 0x0301);
        model.EditSpeedTapMaster(true);
        model.EditStaticAnalog(true);
        Assert.IsEmpty(fake.Calls);
        Assert.IsTrue(await model.ApplySpeedTapPairAsync(true));
        Assert.IsTrue(await model.ApplySpeedTapMasterAsync());
        Assert.IsTrue(await model.ResetSpeedTapToProfileAsync());
        Assert.IsTrue(await model.ApplyStaticAnalogAsync());
        CollectionAssert.AreEqual(new[] { "pair-on:1538:769", "master:True", "profile-reset", "analog:True" }, fake.Calls);
    }

    [TestMethod]
    public async Task SpeedTapEnableFailsClosedWhenBaselineIsUnknown()
    {
        var fake = new Fake();
        fake.Status.SpeedTap.SavedProfilePairsKnown = false;
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.EditSpeedTapPair(0x0602, 0x0301);
        Assert.IsFalse(model.CanEnableSpeedTapPair);
        Assert.IsTrue(model.CanDisableSpeedTapPair);
        Assert.IsFalse(await model.ApplySpeedTapPairAsync(true));
        Assert.IsEmpty(fake.Calls);
        StringAssert.Contains(model.LastMessage, "活动配置保存的 SpeedTap 键对基线未知");
        Assert.IsTrue(await model.ApplySpeedTapPairAsync(false));
        CollectionAssert.AreEqual(new[] { "pair-off:1538:769" }, fake.Calls);
    }

    [TestMethod]
    public async Task DksFnActionTargetIsRejectedByModelWhileFnSourceRemainsAllowed()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        Assert.IsFalse(MagneticKeyLayout.IsValidDksActionTarget(0x0508));
        Assert.IsTrue(MagneticKeyLayout.IsValidDksActionTarget(0x0602));
        Assert.IsTrue(model.Select(0x0508)); // Fn is valid as selection/source
        Assert.AreEqual("Fn", model.SelectedKey!.FullName);
        model.EditDksThresholds(1.0, 3.6);
        Assert.IsFalse(model.EditDksSlot(0, 0x0508, "Tap", "Hold", "Inactive", "Release"));
        Assert.IsTrue(model.EditDksSlot(0, 0x0602, "Tap", "Hold", "Inactive", "Release"));
        // Manually inject Fn target into draft to verify ApplyDksAsync validates
        model.Draft!.DksSlots[0].Target = new MagneticDksTarget { Kind = "LogicalKey", LogicalId = 0x0508 };
        model.Draft.DksDirty = true;
        Assert.IsFalse(await model.ApplyDksAsync(resolveRt: true));
        Assert.IsEmpty(fake.Calls);
        // Fn remains allowed for other features
        model.EditActuation(1.5);
        Assert.IsTrue(await model.ApplyActuationAsync());
        CollectionAssert.AreEqual(new[] { "actuation:1288:1.5" }, fake.Calls);
    }

    [TestMethod]
    public async Task DksConflictPossibleFollowsSourcePrecedence()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.Select(0x0402);

        // Precedence 1: SessionApplied enabled -> conflict = true
        fake.Status.RapidTrigger.Clear();
        fake.Status.RapidTrigger.Add(new MagneticRapidTriggerValue { LogicalId = 0x0402, Enabled = true, Source = "SessionApplied" });
        fake.Status.HostProfile.PerKeyRtListKnown = false;
        Assert.IsTrue(model.DksConflictPossible);

        // Precedence 1: SessionApplied disabled -> conflict = false (outranks HostProfile Unknown!)
        fake.Status.RapidTrigger[0].Enabled = false;
        fake.Status.HostProfile.PerKeyRtListKnown = false;
        Assert.IsFalse(model.DksConflictPossible);

        // Precedence 2: No SessionApplied, HostProfile RT list known -> follows HostProfile
        fake.Status.RapidTrigger.Clear();
        fake.Status.HostProfile.PerKeyRtListKnown = true;
        Assert.IsFalse(model.DksConflictPossible); // key not in hostprofile list => disabled

        fake.Status.RapidTrigger.Add(new MagneticRapidTriggerValue { LogicalId = 0x0402, Enabled = true, Source = "HostProfile" });
        Assert.IsTrue(model.DksConflictPossible); // key in hostprofile list => enabled

        // Precedence 3: Both unavailable -> conflict = true / confirmation required
        fake.Status.RapidTrigger.Clear();
        fake.Status.HostProfile.PerKeyRtListKnown = false;
        Assert.IsTrue(model.DksConflictPossible);
    }

    [TestMethod]
    public async Task PersistentQuarantineRejectsAllEdits()
    {
        var fake = new Fake();
        fake.Status.Health = "PersistentSafetyQuarantine";
        fake.Status.PersistentSafetyQuarantine = true;
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync(); model.Select(0x0402);
        model.EditActuation(1.0);
        Assert.IsTrue(model.Quarantined);
        Assert.IsFalse(await model.ApplyActuationAsync());
        Assert.IsFalse(await model.ResetSpeedTapToProfileAsync());
        Assert.IsEmpty(fake.Calls);
    }

    [TestMethod]
    public async Task IpcUsesTypedLogicalIdsAndDksEnumsWithoutVendorFields()
    {
        var handler = new CaptureHandler();
        var client = new MagneticControlClient(new HttpClient(handler));
        var slots = Enumerable.Range(0, 4).Select(_ => new MagneticDksSlot()).ToArray();
        slots[0].Target = new MagneticDksTarget { Kind = "LogicalKey", LogicalId = 0x0602 };
        slots[0].DownStart = "Tap";
        Assert.IsTrue((await client.SetDksAsync(0x0402, 1.0, 3.6, slots, true)).Succeeded);
        Assert.AreEqual("/api/magnetic/dks", handler.Path);
        using var dks = JsonDocument.Parse(handler.Body!);
        Assert.AreEqual(0x0402, dks.RootElement.GetProperty("logical_id").GetInt32());
        Assert.AreEqual(4, dks.RootElement.GetProperty("slots").GetArrayLength());
        Assert.AreEqual("Tap", dks.RootElement.GetProperty("slots")[0].GetProperty("down_start").GetString());
        Assert.AreEqual(0x0602, dks.RootElement.GetProperty("slots")[0].GetProperty("target").GetProperty("logical_id").GetInt32());
        Assert.IsFalse(handler.Body!.Contains("wire_id", StringComparison.OrdinalIgnoreCase));
        Assert.IsFalse(handler.Body.Contains("opcode", StringComparison.OrdinalIgnoreCase));
        Assert.IsTrue((await client.DisableRapidTriggerAsync(0x0402)).Succeeded);
        Assert.AreEqual("/api/magnetic/rapid-trigger/disable", handler.Path);
        using var disabled = JsonDocument.Parse(handler.Body!);
        Assert.AreEqual(1, disabled.RootElement.EnumerateObject().Count());
        Assert.AreEqual(0x0402, disabled.RootElement.GetProperty("logical_id").GetInt32());
    }

    private sealed class CaptureHandler : HttpMessageHandler
    {
        public string? Path { get; private set; }
        public string? Body { get; private set; }
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,
            CancellationToken cancellationToken)
        {
            Path = request.RequestUri?.AbsolutePath;
            Body = request.Content == null ? null : await request.Content.ReadAsStringAsync(cancellationToken);
            return new HttpResponseMessage(HttpStatusCode.OK) {
                Content = new StringContent("{\"status\":\"ok\",\"api_version\":1,\"health\":\"Clean\",\"available\":true}", Encoding.UTF8, "application/json")
            };
        }
    }

    private sealed class Fake : IMagneticControlClient
    {
        public MagneticStatus Status { get; } = Clean();
        public List<string> Calls { get; } = [];
        public Task<MagneticStatus>? Next { get; set; }
        public Task<MagneticStatus> GetStatusAsync() => Task.FromResult(Status);
        private Task<MagneticStatus> Record(string call) { Calls.Add(call); return Next ?? Task.FromResult(Status); }
        public Task<MagneticStatus> SetActuationAsync(ushort id, double mm) => Record($"actuation:{id}:{mm:F1}");
        public Task<MagneticStatus> SetRapidTriggerAsync(ushort id, double press, double release, bool resolve) => Record($"rt-on:{id}:{press:F1}:{release:F1}:{resolve}");
        public Task<MagneticStatus> DisableRapidTriggerAsync(ushort id) => Record($"rt-off:{id}");
        public Task<MagneticStatus> SetDeadzoneAsync(ushort id, double top, double bottom) => Record($"deadzone:{id}:{top:F1}:{bottom:F1}");
        public Task<MagneticStatus> ResetAllDeadzoneAsync() => Record("deadzone-reset-all");
        public Task<MagneticStatus> SetDksAsync(ushort id, double start, double end, IReadOnlyList<MagneticDksSlot> slots, bool resolve) =>
            Record($"dks:{id}:{start:F1}:{end:F1}:{slots.Count}:{resolve}");
        public Task<MagneticStatus> RestoreDksStandardAsync(ushort id) => Record($"dks-standard:{id}");
        public Task<MagneticStatus> SetSpeedTapPairAsync(ushort first, ushort second) => Record($"pair-on:{first}:{second}");
        public Task<MagneticStatus> DisableSpeedTapPairAsync(ushort first, ushort second) => Record($"pair-off:{first}:{second}");
        public Task<MagneticStatus> SetSpeedTapMasterAsync(bool enabled) => Record($"master:{enabled}");
        public Task<MagneticStatus> ResetSpeedTapToProfileAsync() => Record("profile-reset");
        public Task<MagneticStatus> SetStaticAnalogEffectAsync(bool enabled) => Record($"analog:{enabled}");
    }
}
