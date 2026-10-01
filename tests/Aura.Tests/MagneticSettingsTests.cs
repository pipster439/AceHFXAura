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
        Assert.IsEmpty(fake.Calls);
        Assert.IsTrue(await model.ApplySpeedTapPairAsync(true));
        Assert.IsTrue(await model.ApplySpeedTapMasterAsync());
        Assert.IsTrue(await model.ResetSpeedTapToProfileAsync());
        CollectionAssert.AreEqual(new[] { "pair-on:1538:769", "master:True", "profile-reset" }, fake.Calls);
    }

    [TestMethod]
    public async Task HardwareAnalogNormalApplyIsBlockedWhileAuraCannotEstablishFirmwareStatic()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        Assert.IsTrue(model.CanWrite);
        Assert.IsFalse(model.CanApplyStaticAnalog);
        StringAssert.Contains(MagneticSettingsModel.HardwareAnalogDescription, "固件根据磁轴行程直接渲染");
        StringAssert.Contains(MagneticSettingsModel.HardwareAnalogDescription, "固件恒亮模式");
        StringAssert.Contains(MagneticSettingsModel.HardwareAnalogBlockedReason, "无法安全切换");
        model.EditStaticAnalog(true);
        Assert.IsFalse(await model.ApplyStaticAnalogAsync());
        Assert.IsEmpty(fake.Calls);
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

    [TestMethod]
    public void DksActionTargetsExcludesFnKey()
    {
        Assert.IsFalse(MagneticKeyLayout.IsValidDksActionTarget(0x0508)); // Fn
        Assert.IsTrue(MagneticKeyLayout.IsValidDksActionTarget(0x050a)); // Copilot
        Assert.IsTrue(MagneticKeyLayout.IsValidDksActionTarget(0x0602)); // A
        Assert.IsFalse(MagneticKeyLayout.IsValidDksActionTarget(0xffff)); // Invalid ID

        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        Assert.IsTrue(model.Select(0x0402)); // V
        Assert.IsFalse(model.EditDksSlot(0, 0x0508, "Tap", "Inactive", "Inactive", "Inactive"));
        Assert.IsTrue(model.EditDksSlot(0, 0x050a, "Tap", "Inactive", "Inactive", "Inactive"));
    }

    [TestMethod]
    public async Task DirtyDraftStateTransitionsReflectUnappliedChanges()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.Select(0x0402); // V

        // Actuation draft
        Assert.IsNull(model.Draft?.ActuationMm);
        model.EditActuation(2.5);
        Assert.AreEqual(2.5, model.Draft?.ActuationMm);
        Assert.IsTrue(await model.ApplyActuationAsync());
        Assert.IsNull(model.Draft?.ActuationMm);
        Assert.AreEqual("actuation:1026:2.5", fake.Calls.Last());

        // Rapid trigger draft
        Assert.IsNull(model.Draft?.RapidTriggerEnabled);
        model.EditRapidTrigger(true, 0.4, 0.3);
        Assert.IsTrue(model.Draft?.RapidTriggerEnabled);
        Assert.AreEqual(0.4, model.Draft?.PressMm);
        Assert.AreEqual(0.3, model.Draft?.ReleaseMm);

        // Deadzone draft
        Assert.IsNull(model.Draft?.TopMm);
        model.EditDeadzone(0.3, 0.2);
        Assert.AreEqual(0.3, model.Draft?.TopMm);
        Assert.AreEqual(0.2, model.Draft?.BottomMm);
        Assert.IsTrue(await model.ApplyDeadzoneAsync());
        Assert.IsNull(model.Draft?.TopMm);
        Assert.IsNull(model.Draft?.BottomMm);
        Assert.AreEqual("deadzone:1026:0.3:0.2", fake.Calls.Last());

        // DKS dirty
        Assert.IsFalse(model.Draft?.DksDirty);
        model.EditDksThresholds(1.5, 3.5);
        Assert.IsTrue(model.Draft?.DksDirty);
    }

    [TestMethod]
    public async Task ConflictDetectionBetweenRtAndDksRequiresExplicitResolution()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.Select(0x0402);

        // Initially no conflict with clean profile
        Assert.IsTrue(model.RtConflictPossible); // initially DKS is unknown
        model.EditRapidTrigger(true, 0.5, 0.5);

        // Applying RT when DKS is unconfirmed requires explicit resolveDks=true
        Assert.IsFalse(await model.ApplyRapidTriggerAsync(resolveDks: false));
        Assert.IsTrue(await model.ApplyRapidTriggerAsync(resolveDks: true));
        Assert.AreEqual("rt-on:1026:0.5:0.5:True", fake.Calls.Last());
    }

    [TestMethod]
    public async Task GlobalModeSelectionAndTransitionsPerformZeroWrites()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        fake.Calls.Clear();

        // Enter global mode: no key selected, IsGlobalMode = true
        model.SelectGlobal();
        Assert.IsTrue(model.IsGlobalMode);
        Assert.IsNull(model.SelectedLogicalId);
        Assert.IsNull(model.SelectedKey);
        Assert.IsEmpty(fake.Calls);

        // Select physical key: IsGlobalMode = false, key selected
        Assert.IsTrue(model.Select(0x0402)); // V
        Assert.IsFalse(model.IsGlobalMode);
        Assert.AreEqual((ushort)0x0402, model.SelectedLogicalId);
        Assert.AreEqual("V", model.SelectedKey?.FullName);
        Assert.IsEmpty(fake.Calls);

        // Return to global mode: zero writes
        model.SelectGlobal();
        Assert.IsTrue(model.IsGlobalMode);
        Assert.IsNull(model.SelectedLogicalId);
        Assert.IsEmpty(fake.Calls);
    }

    [TestMethod]
    public async Task MultiSelectionIsUniqueReadOnlyAndPreservesUnknownProvenance()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        Assert.AreEqual(MagneticSelectionContext.Global, model.SelectionContext);
        Assert.IsTrue(model.Select(0x0701)); // W
        Assert.AreEqual(MagneticSelectionContext.Single, model.SelectionContext);
        Assert.IsTrue(model.EnterMulti());
        Assert.AreEqual(MagneticSelectionContext.Multi, model.SelectionContext);
        Assert.AreEqual(1, model.SelectedCount);
        Assert.IsTrue(model.ToggleMulti(0x0602)); // A
        Assert.IsTrue(model.ToggleMulti(0x0702)); // S
        Assert.IsTrue(model.ToggleMulti(0x0301)); // D
        Assert.AreEqual("W · A · S · D", model.MultiSelectionSummary);
        Assert.AreEqual(4, model.SelectedCount);
        Assert.IsFalse(model.CanWriteSelected);
        Assert.IsFalse(model.CanWriteGlobal);
        Assert.IsFalse(model.ToggleMulti(0xffff));
        Assert.AreEqual(4, model.SelectedCount);

        fake.Status.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0701, Raw = 10, Source = "SessionApplied" });
        fake.Status.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0602, Raw = 20, Source = "SessionApplied" });
        fake.Status.RapidTrigger.Add(new MagneticRapidTriggerValue { LogicalId = 0x0701, Enabled = true, Source = "SessionApplied" });
        fake.Status.Deadzone.Add(new MagneticDeadzoneValue { LogicalId = 0x0701, TopRaw = 1, BottomRaw = 2, Source = "SessionApplied" });
        await model.RefreshAsync();
        Assert.AreEqual("2 个已知 · 2 个未知", model.MultiActuationText);
        Assert.AreEqual("1 个已知 · 3 个未知", model.MultiRapidTriggerText);
        Assert.AreEqual("1 个已知 · 3 个未知", model.MultiDeadzoneText);
        fake.Status.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0702, Raw = 10, Source = "SessionApplied" });
        fake.Status.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0301, Raw = 10, Source = "HostProfile" });
        Assert.AreEqual("混合值", model.MultiActuationText);
        fake.Status.Actuation[1].Raw = 10;
        Assert.AreEqual("来源混合：1.0 mm", model.MultiActuationText);

        model.EditActuation(2.0);
        model.EditRapidTrigger(true, 0.5, 0.3);
        model.EditDeadzone(0.1, 0.2);
        Assert.IsFalse(await model.ApplyActuationAsync());
        Assert.IsFalse(await model.ApplyRapidTriggerAsync(true));
        Assert.IsFalse(await model.ApplyDeadzoneAsync());
        Assert.IsEmpty(fake.Calls);
        Assert.IsTrue(model.ToggleMulti(0x0301));
        Assert.AreEqual(3, model.SelectedCount);
        Assert.IsTrue(model.ToggleMulti(0x0301));
        Assert.AreEqual(4, model.SelectedCount);
        Assert.AreEqual(4, model.SelectedLogicalIds.Distinct().Count());
        Assert.IsTrue(model.ClearMulti());
        Assert.AreEqual(0, model.SelectedCount);
        Assert.IsTrue(model.SelectGlobal());
        Assert.AreEqual(MagneticSelectionContext.Global, model.SelectionContext);
        Assert.IsEmpty(fake.Calls);
    }

    [TestMethod]
    public async Task DksTriStateAndConflictCopyUseSessionEvidenceOnly()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.Select(0x0402);
        Assert.AreEqual(MagneticDksState.Unknown, model.DksState);
        Assert.IsTrue(model.RtConflictPossible);
        StringAssert.Contains(model.RtDksConflictCopy, "无法确认此键当前的 DKS 状态");
        Assert.IsTrue(model.ConfigureDksEditor());
        Assert.IsTrue(model.DksEditorOpen);
        Assert.IsEmpty(fake.Calls);

        fake.Status.Dks.Add(new MagneticDksValue { LogicalId = 0x0402, Source = "HostProfile", StandardRuntimeConfiguration = false });
        await model.RefreshAsync();
        Assert.AreEqual(MagneticDksState.Unknown, model.DksState);
        fake.Status.Dks.Add(new MagneticDksValue { LogicalId = 0x0402, Source = "SessionApplied", StandardRuntimeConfiguration = true });
        await model.RefreshAsync();
        Assert.AreEqual(MagneticDksState.Standard, model.DksState);
        Assert.IsFalse(model.RtConflictPossible);
        fake.Status.Dks.Last().StandardRuntimeConfiguration = false;
        Assert.AreEqual(MagneticDksState.SessionConfigured, model.DksState);
        StringAssert.Contains(model.RtDksConflictCopy, "本次会话中已配置 DKS");
        Assert.IsFalse(model.RtDksConflictCopy.Contains("无法确认"));
        Assert.IsTrue(await model.RestoreDksStandardAsync());
        CollectionAssert.AreEqual(new[] { "dks-standard:1026" }, fake.Calls);
    }

    [TestMethod]
    public async Task BatchDraftsAreExplicitAggregatesTypedAndSelectionFreezesWhileBusy()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.EnterMulti();
        foreach (ushort id in new ushort[] { 0x0301, 0x0702, 0x0602, 0x0701 }) model.ToggleMulti(id);
        Assert.AreEqual(MagneticAggregateKind.Unknown, model.MultiActuation.Kind);
        Assert.IsNull(model.BatchDraft.ActuationMm);
        Assert.IsFalse(model.CanBatchActuation);
        fake.Status.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0701, Raw = 12, Source = "SessionApplied" });
        Assert.AreEqual(MagneticAggregateKind.ContainsUnknown, model.MultiActuation.Kind);
        foreach (ushort id in new ushort[] { 0x0602, 0x0702, 0x0301 })
            fake.Status.Actuation.Add(new MagneticActuationValue { LogicalId = id, Raw = 12, Source = "SessionApplied" });
        Assert.AreEqual(MagneticAggregateKind.UniformKnown, model.MultiActuation.Kind);
        fake.Status.Actuation[1].Raw = 14;
        Assert.AreEqual(MagneticAggregateKind.MixedKnown, model.MultiActuation.Kind);
        Assert.IsNull(model.BatchDraft.ActuationMm); // Mixed did not seed a draft.

        model.EditBatchActuation(1.2);
        Assert.IsTrue(model.CanBatchActuation);
        var pending = new TaskCompletionSource<MagneticStatus>();
        fake.Next = pending.Task;
        var apply = model.ApplyBatchActuationAsync();
        Assert.IsTrue(model.Busy);
        Assert.IsFalse(model.ToggleMulti(0x0402));
        Assert.IsFalse(model.SelectGlobal());
        CollectionAssert.AreEqual(new[] { "batch-actuation:1793,1538,1794,769:1.2" }, fake.Calls);
        pending.SetResult(new MagneticStatus { Status = "ok", ApiVersion = 1, Health = "Clean", Available = true,
            BatchResult = new MagneticBatchResult { RequestedCount = 4, AppliedCount = 4,
                CompletedFully = true } });
        Assert.IsTrue(await apply);
        Assert.IsFalse(model.Busy);
        Assert.IsNull(model.BatchDraft.ActuationMm);
        Assert.AreEqual(4, model.LastBatchResult!.AppliedCount);
        Assert.IsTrue(model.ToggleMulti(0x0402));
        Assert.IsNull(model.BatchDraft.ActuationMm); // New selection cannot inherit an old batch draft.
    }

    [TestMethod]
    public async Task BatchRtDisableRequiresBaselineAndPartialResultsStayDistinct()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.EnterMulti(); model.ToggleMulti(0x0701); model.ToggleMulti(0x0602);
        model.EditBatchRapidTrigger(MagneticBatchRtAction.Disable);
        Assert.IsFalse(model.CanBatchRapidTrigger);
        Assert.IsFalse(await model.ApplyBatchRapidTriggerAsync());
        Assert.IsEmpty(fake.Calls);
        fake.Status.HostProfile.GlobalRtPress = new MagneticKnownRaw { Known = true, Raw = 4, Source = "HostProfile" };
        fake.Status.HostProfile.GlobalRtRelease = new MagneticKnownRaw { Known = true, Raw = 2, Source = "HostProfile" };
        Assert.IsTrue(model.CanBatchRapidTrigger);
        fake.Next = Task.FromResult(new MagneticStatus { Status = "error", ApiVersion = 1,
            Health = "IndeterminateStagedState", BatchResult = new MagneticBatchResult {
                RequestedCount = 2, AppliedCount = 1, FailedCount = 1, CompletedFully = false,
                Results = [new() { LogicalId = 0x0701, Status = "Applied" },
                    new() { LogicalId = 0x0602, Status = "Failed", Detail = "fake failure" }] } });
        Assert.IsFalse(await model.ApplyBatchRapidTriggerAsync());
        Assert.AreEqual(1, model.LastBatchResult!.AppliedCount);
        Assert.AreEqual("Failed", model.LastBatchResult.Results[1].Status);
        Assert.AreEqual(MagneticBatchRtAction.Disable, model.BatchDraft.RtAction);
        CollectionAssert.AreEqual(new[] { "batch-rt:1793,1538:False:::False" }, fake.Calls);
    }

    [TestMethod]
    public async Task GlobalSettingsDraftAndApplyLifecycle()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.SelectGlobal();

        // Global Actuation Draft & Apply
        Assert.IsNull(model.GlobalDraft?.ActuationMm);
        model.EditGlobalActuation(1.8);
        Assert.AreEqual(1.8, model.GlobalDraft?.ActuationMm);
        Assert.IsEmpty(fake.Calls);

        Assert.IsTrue(await model.ApplyGlobalActuationAsync());
        Assert.IsNull(model.GlobalDraft?.ActuationMm);
        Assert.AreEqual("global-actuation:1.8", fake.Calls.Last());

        // Global Deadzone Draft & Apply (must never call ResetAllDeadzoneAsync)
        Assert.IsNull(model.GlobalDraft?.TopMm);
        Assert.IsNull(model.GlobalDraft?.BottomMm);
        model.EditGlobalDeadzone(0.3, 0.4);
        Assert.AreEqual(0.3, model.GlobalDraft?.TopMm);
        Assert.AreEqual(0.4, model.GlobalDraft?.BottomMm);
        Assert.AreEqual(1, fake.Calls.Count);

        Assert.IsTrue(await model.ApplyGlobalDeadzoneAsync());
        Assert.IsNull(model.GlobalDraft?.TopMm);
        Assert.IsNull(model.GlobalDraft?.BottomMm);
        Assert.AreEqual("global-deadzone:0.3:0.4", fake.Calls.Last());
        Assert.DoesNotContain("deadzone-reset-all", fake.Calls);

        // Global Rapid Trigger Draft & Apply
        Assert.IsNull(model.GlobalDraft?.PressMm);
        model.EditGlobalDeadzone(0.1, 0.2);
        model.EditGlobalRapidTrigger(0.5, 0.6, true);
        Assert.AreEqual(0.5, model.GlobalDraft?.PressMm);
        Assert.AreEqual(0.6, model.GlobalDraft?.ReleaseMm);
        Assert.IsTrue(model.GlobalDraft?.SeparateMode);
        Assert.AreEqual(0.1, model.GlobalDraft?.TopMm);
        Assert.AreEqual(0.2, model.GlobalDraft?.BottomMm);

        Assert.IsFalse(await model.ApplyGlobalRapidTriggerAsync());
        Assert.AreEqual(0.5, model.GlobalDraft?.PressMm);
        // Preserves currently applied global deadzone 0.3 / 0.4, NOT the unapplied draft 0.1 / 0.2
        Assert.IsFalse(fake.Calls.Any(call => call.StartsWith("global-rt:")));
    }

    [TestMethod]
    public async Task GlobalRapidTriggerSeparateModeToggleBehavior()
    {
        var fake = new Fake();
        fake.Status.GlobalDeadzone = new MagneticGlobalDeadzoneState { Known = true, TopRaw = 1, BottomRaw = 1, Source = "SessionApplied" };
        fake.Status.HostProfile.GlobalDeadzoneTop = new MagneticKnownRaw { Known = true, Raw = 1, Source = "HostProfile" };
        fake.Status.HostProfile.GlobalDeadzoneBottom = new MagneticKnownRaw { Known = true, Raw = 1, Source = "HostProfile" };
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.SelectGlobal();

        // Linked sensitivity (separateMode = false): release follows press
        model.EditGlobalRapidTrigger(0.4, 0.4, false);
        Assert.IsFalse(model.GlobalDraft?.SeparateMode);
        Assert.AreEqual(0.4, model.GlobalDraft?.PressMm);
        Assert.AreEqual(0.4, model.GlobalDraft?.ReleaseMm);
        Assert.IsFalse(await model.ApplyGlobalRapidTriggerAsync());
        Assert.IsFalse(fake.Calls.Any(call => call.StartsWith("global-rt:")));

        // Separate sensitivity (separateMode = true)
        model.EditGlobalRapidTrigger(0.2, 0.8, true);
        Assert.IsTrue(model.GlobalDraft?.SeparateMode);
        Assert.AreEqual(0.2, model.GlobalDraft?.PressMm);
        Assert.AreEqual(0.8, model.GlobalDraft?.ReleaseMm);
        Assert.IsFalse(await model.ApplyGlobalRapidTriggerAsync());
        Assert.IsFalse(fake.Calls.Any(call => call.StartsWith("global-rt:")));
    }

    [TestMethod]
    public async Task GlobalRapidTriggerApplyPreservesKnownDeadzoneAndIgnoresDeadzoneDraft()
    {
        var fake = new Fake();
        fake.Status.GlobalDeadzone = new MagneticGlobalDeadzoneState
        {
            Known = true,
            TopRaw = 1,
            BottomRaw = 2,
            Source = "SessionApplied"
        };
        fake.Status.HostProfile.GlobalDeadzoneTop = new MagneticKnownRaw { Known = true, Raw = 1, Source = "HostProfile" };
        fake.Status.HostProfile.GlobalDeadzoneBottom = new MagneticKnownRaw { Known = true, Raw = 2, Source = "HostProfile" };
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.SelectGlobal();

        // Edit unapplied Global Deadzone draft to 0.3 / 0.4
        model.EditGlobalDeadzone(0.3, 0.4);
        Assert.AreEqual(0.3, model.GlobalDraft?.TopMm);
        Assert.AreEqual(0.4, model.GlobalDraft?.BottomMm);

        // Edit and Apply Global RT
        model.EditGlobalRapidTrigger(0.5, 0.6, true);
        Assert.IsFalse(await model.ApplyGlobalRapidTriggerAsync());

        // Expected: RT packet uses currently applied/known global deadzone (0.1 / 0.2), NOT 0.3 / 0.4!
        Assert.IsFalse(fake.Calls.Any(call => call.StartsWith("global-rt:")));

        // Deadzone draft remains unapplied
        Assert.AreEqual(0.3, model.GlobalDraft?.TopMm);
        Assert.AreEqual(0.4, model.GlobalDraft?.BottomMm);
    }

    [TestMethod]
    public async Task GlobalRapidTriggerApplyFailsClosedWhenDeadzoneIsUnknown()
    {
        var fake = new Fake();
        fake.Status.GlobalDeadzone = new MagneticGlobalDeadzoneState { Known = false };
        fake.Status.HostProfile.GlobalDeadzoneTop = new MagneticKnownRaw { Known = false };
        fake.Status.HostProfile.GlobalDeadzoneBottom = new MagneticKnownRaw { Known = false };

        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        model.SelectGlobal();

        // Edit Global RT draft
        model.EditGlobalRapidTrigger(0.4, 0.4, false);
        fake.Calls.Clear();

        // Apply Global RT -> fails closed, zero client mutation
        Assert.IsFalse(await model.ApplyGlobalRapidTriggerAsync());
        Assert.IsEmpty(fake.Calls);
        Assert.AreEqual(0.4, model.GlobalDraft?.PressMm);
        Assert.AreEqual(0.4, model.GlobalDraft?.ReleaseMm);
        StringAssert.Contains(model.LastMessage, "停用");
    }

    [TestMethod]
    public async Task GlobalIpcUsesSemanticContractsWithoutVendorFields()
    {
        var handler = new CaptureHandler();
        var client = new MagneticControlClient(new HttpClient(handler));

        // Actuation
        Assert.IsTrue((await client.SetGlobalActuationAsync(1.5)).Succeeded);
        Assert.AreEqual("/api/magnetic/global/actuation", handler.Path);
        using var actDoc = JsonDocument.Parse(handler.Body!);
        Assert.AreEqual(1.5, actDoc.RootElement.GetProperty("mm").GetDouble(), 0.001);
        Assert.IsFalse(handler.Body!.Contains("raw", StringComparison.OrdinalIgnoreCase));
        Assert.IsFalse(handler.Body.Contains("opcode", StringComparison.OrdinalIgnoreCase));

        // Deadzone
        Assert.IsTrue((await client.SetGlobalDeadzoneAsync(0.2, 0.3)).Succeeded);
        Assert.AreEqual("/api/magnetic/global/deadzone", handler.Path);
        using var dzDoc = JsonDocument.Parse(handler.Body!);
        Assert.AreEqual(0.2, dzDoc.RootElement.GetProperty("top_mm").GetDouble(), 0.001);
        Assert.AreEqual(0.3, dzDoc.RootElement.GetProperty("bottom_mm").GetDouble(), 0.001);
        Assert.IsFalse(handler.Body!.Contains("raw", StringComparison.OrdinalIgnoreCase));

        // Compatibility client cannot issue a legacy HTTP mutation.
        var previousPath=handler.Path;
        Assert.IsFalse((await client.SetGlobalRapidTriggerAsync(0.4,0.5,0.1,0.2,true)).Succeeded);
        Assert.AreEqual(previousPath,handler.Path);

    }

    [TestMethod]
    public async Task KeycapOverlayLabelsGlobalBaselineAndPrefersExactPerKeyValues()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();
        const ushort w = 0x0701;
        var unknown = model.KeyOverlay(w);
        Assert.AreEqual("—", unknown.ActuationText);
        Assert.AreEqual("—", unknown.RtPressText);
        Assert.AreEqual("—", unknown.RtReleaseText);
        Assert.IsTrue(unknown.AccessibilityText.Contains("触发点未知"));

        fake.Status.HostProfile.GlobalActuation = new MagneticKnownRaw { Known = true, Raw = 10, Source = "HostProfile" };
        fake.Status.GlobalActuation = new MagneticGlobalActuationState { Known = true, Raw = 10, Source = "HostProfile" };
        fake.Status.HostProfile.GlobalRtPress = new MagneticKnownRaw { Known = true, Raw = 4, Source = "HostProfile" };
        fake.Status.HostProfile.GlobalRtRelease = new MagneticKnownRaw { Known = true, Raw = 2, Source = "HostProfile" };
        fake.Status.RapidTrigger.Add(new MagneticRapidTriggerValue { LogicalId = w, Enabled = true,
            PressRaw = 4, ReleaseRaw = 2, Source = "HostProfile" });
        model.Select(w);
        model.EditActuation(1.8);
        model.EditRapidTrigger(true, 0.8, 0.6);
        var baseline = model.KeyOverlay(w);
        Assert.AreEqual("1.0", baseline.ActuationText);
        Assert.AreEqual(MagneticOverlaySource.HostGlobalBaseline, baseline.ActuationSource);
        Assert.IsTrue(baseline.AccessibilityText.Contains("全局基线 1.0 mm"));
        Assert.IsTrue(baseline.AccessibilityText.Contains("设备逐键覆盖状态未确认"));
        Assert.AreEqual("—", model.KeyOverlay(w).RtPressText);

        fake.Status.Actuation.Add(new MagneticActuationValue { LogicalId = w, Raw = 10, Source = "HostProfile" });
        fake.Status.RapidTrigger.Add(new MagneticRapidTriggerValue { LogicalId = w, Enabled = true,
            PressRaw = 2, ReleaseRaw = 3, Source = "SessionApplied" });
        var known = model.KeyOverlay(w);
        Assert.AreEqual("1.0", known.ActuationText);
        Assert.AreEqual("↓0.2", known.RtPressText);
        Assert.AreEqual("↑0.3", known.RtReleaseText);
        Assert.IsTrue(known.ActuationKnown && known.RtPressKnown && known.RtReleaseKnown);
        Assert.AreEqual(MagneticOverlaySource.HostPerKey, known.ActuationSource);
        Assert.IsTrue(known.AccessibilityText.Contains("触发点 1.0 mm"));
        Assert.IsTrue(known.AccessibilityText.Contains("RT 按下 0.2 mm"));
        Assert.IsTrue(known.AccessibilityText.Contains("RT 抬起 0.3 mm"));
        Assert.AreEqual("1.0", model.KeyOverlay(0x0602).ActuationText);
        Assert.AreEqual(MagneticOverlaySource.HostGlobalBaseline, model.KeyOverlay(0x0602).ActuationSource);

        fake.Status.Actuation.Add(new MagneticActuationValue { LogicalId = w, Raw = 12, Source = "SessionApplied" });
        Assert.AreEqual("1.2", model.KeyOverlay(w).ActuationText); // SessionApplied wins over HostProfile.
        Assert.AreEqual(MagneticOverlaySource.SessionPerKey, model.KeyOverlay(w).ActuationSource);
        Assert.IsTrue(model.KeyOverlay(w).AccessibilityText.Contains("本次会话逐键设置"));
        fake.Status.RapidTrigger.Last().Enabled = false;
        var disabled = model.KeyOverlay(w);
        Assert.AreEqual("—", disabled.RtPressText);
        Assert.AreEqual("—", disabled.RtReleaseText);
        Assert.IsTrue(disabled.AccessibilityText.Contains("RT 已禁用"));
        Assert.IsEmpty(fake.Calls); // Projection and drafts do not mutate hardware.
    }

    [TestMethod]
    public async Task BatchIpcUsesOnlyTypedLogicalIdsAndExplicitRtAction()
    {
        var handler = new CaptureHandler();
        var client = new MagneticControlClient(new HttpClient(handler));
        ushort[] ids = [0x0701, 0x0602];
        await client.SetBatchActuationAsync(ids, 1.2);
        Assert.AreEqual("/api/magnetic/batch/actuation", handler.Path);
        using (var doc = JsonDocument.Parse(handler.Body!)) {
            Assert.AreEqual(2, doc.RootElement.GetProperty("logical_ids").GetArrayLength());
            Assert.AreEqual(1.2, doc.RootElement.GetProperty("millimeters").GetDouble(), 0.001);
        }
        await client.SetBatchRapidTriggerAsync(ids, true, 0.8, 0.6, true);
        Assert.AreEqual("/api/magnetic/batch/rapid-trigger", handler.Path);
        using (var doc = JsonDocument.Parse(handler.Body!)) {
            Assert.AreEqual("enable", doc.RootElement.GetProperty("action").GetString());
            Assert.IsTrue(doc.RootElement.GetProperty("resolve_dks").GetBoolean());
        }
        await client.SetBatchRapidTriggerAsync(ids, false);
        using (var doc = JsonDocument.Parse(handler.Body!)) {
            Assert.AreEqual("disable", doc.RootElement.GetProperty("action").GetString());
            Assert.IsFalse(doc.RootElement.TryGetProperty("press_mm", out _));
        }
        await client.SetBatchDeadzoneAsync(ids, 0.0, 0.3);
        Assert.AreEqual("/api/magnetic/batch/deadzone", handler.Path);
        using (var doc = JsonDocument.Parse(handler.Body!)) {
            Assert.AreEqual(0.0, doc.RootElement.GetProperty("top_mm").GetDouble(), 0.001);
            Assert.AreEqual(0.3, doc.RootElement.GetProperty("bottom_mm").GetDouble(), 0.001);
        }
        Assert.IsFalse(handler.Body!.Contains("opcode", StringComparison.OrdinalIgnoreCase));
        Assert.IsFalse(handler.Body.Contains("wire_id", StringComparison.OrdinalIgnoreCase));
    }

    [TestMethod]
    public async Task ServiceRouteRequiresExplicitConfirmationAndEmitsZeroVendorOpcode()
    {
        var handler = new CaptureHandler();
        var client = new MagneticControlClient(new HttpClient(handler));

        var result = await client.AcknowledgeExternalResynchronizationAsync();
        Assert.IsTrue(result.Succeeded);
        Assert.AreEqual("/api/magnetic/safety/acknowledge-external-resynchronization", handler.Path);
        Assert.IsNotNull(handler.Body);
        using var doc = JsonDocument.Parse(handler.Body);
        Assert.IsTrue(doc.RootElement.GetProperty("confirm_external_resynchronization").GetBoolean());
        Assert.IsFalse(handler.Body.Contains("opcode", StringComparison.OrdinalIgnoreCase));
        Assert.IsFalse(handler.Body.Contains("hid", StringComparison.OrdinalIgnoreCase));
    }

    [TestMethod]
    public async Task QuarantinedModelRecoveryFlowAndSessionAppliedClearing()
    {
        var fake = new Fake();
        fake.Status.PersistentSafetyQuarantine = true;
        fake.Status.Health = "PersistentSafetyQuarantine";
        fake.Status.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0402, Raw = 20, Source = "SessionApplied" });
        fake.Status.RapidTrigger.Add(new MagneticRapidTriggerValue { LogicalId = 0x0402, Enabled = true, PressRaw = 4, ReleaseRaw = 4, Source = "SessionApplied" });
        fake.Status.Deadzone.Add(new MagneticDeadzoneValue { LogicalId = 0x0402, TopRaw = 1, BottomRaw = 2, Source = "SessionApplied" });

        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();

        Assert.IsTrue(model.Quarantined);
        Assert.IsFalse(model.CanWrite);
        Assert.IsFalse(model.CanWriteSelected);

        // App restart / refresh alone does not acknowledge:
        await model.RefreshAsync();
        Assert.IsTrue(model.Quarantined);
        Assert.IsFalse(fake.Calls.Contains("ack-external-resync"));

        // Normal operations fail closed while quarantined:
        model.Select(0x0402);
        model.EditActuation(1.0);
        Assert.IsFalse(await model.ApplyActuationAsync());

        // Confirmed recovery invokes AcknowledgeExternalResynchronization:
        Assert.IsTrue(await model.AcknowledgeExternalResynchronizationAsync());
        Assert.IsTrue(fake.Calls.Contains("ack-external-resync"));
        Assert.IsFalse(model.Quarantined);
        Assert.IsTrue(model.CanWrite);
        Assert.AreEqual("Clean", model.Status!.Health);
        Assert.IsFalse(model.Status.PersistentSafetyQuarantine);

        // SessionApplied state is cleared:
        Assert.AreEqual(0, model.Status.Actuation.Count);
        Assert.AreEqual(0, model.Status.RapidTrigger.Count);
        Assert.AreEqual(0, model.Status.Deadzone.Count);
    }

    [TestMethod]
    public async Task UiDoesNotExposeRtMasterMutationAndSeparatesHardwareMasterDisplay()
    {
        var fake = new Fake();
        var model = new MagneticSettingsModel(fake);
        await model.RefreshAsync();

        // 1. Model & Client do not expose any RT master mutation API:
        Assert.IsNull(typeof(MagneticSettingsModel).GetMethod("SetRapidTriggerMaster"));
        Assert.IsNull(typeof(MagneticSettingsModel).GetMethod("EditRapidTriggerMaster"));
        Assert.IsNull(typeof(MagneticSettingsModel).GetMethod("ApplyRapidTriggerMasterAsync"));
        Assert.IsNull(typeof(IMagneticControlClient).GetMethod("SetRapidTriggerMasterAsync"));

        // 2. RapidTriggerMaster displays read-only hardware switch state:
        fake.Status.RapidTriggerMaster = new MagneticKnownBool { Known = false };
        Assert.AreEqual("硬件总开关：未知", model.RapidTriggerMasterText);

        fake.Status.RapidTriggerMaster = new MagneticKnownBool { Known = true, Value = true };
        Assert.AreEqual("硬件总开关：开启", model.RapidTriggerMasterText);

        fake.Status.RapidTriggerMaster = new MagneticKnownBool { Known = true, Value = false };
        Assert.AreEqual("硬件总开关：关闭", model.RapidTriggerMasterText);

        Assert.AreEqual("快速触发总开关由键盘物理开关控制。", MagneticSettingsModel.RapidTriggerMasterExplanation);

        // 3. Single-key RT toggle modifies per-key RT (51 54), NOT master:
        model.Select(0x0402);
        model.EditRapidTrigger(true, 0.4, 0.4);
        Assert.IsTrue(await model.ApplyRapidTriggerAsync(resolveDks: true));
        Assert.AreEqual("rt-on:1026:0.4:0.4:True", fake.Calls.Last());

        // 4. Global RT modifies separate_mode / press / release (51 53), NOT master enable:
        fake.Status.GlobalDeadzone = new MagneticGlobalDeadzoneState { Known = true, TopRaw = 0, BottomRaw = 1, Source = "SessionApplied" };
        fake.Status.HostProfile.GlobalDeadzoneTop = new MagneticKnownRaw { Known = true, Raw = 0, Source = "HostProfile" };
        fake.Status.HostProfile.GlobalDeadzoneBottom = new MagneticKnownRaw { Known = true, Raw = 1, Source = "HostProfile" };
        model.SelectGlobal();
        model.EditGlobalRapidTrigger(0.5, 0.5, false);
        Assert.IsFalse(await model.ApplyGlobalRapidTriggerAsync());
        Assert.IsFalse(fake.Calls.Any(call => call.StartsWith("global-rt:")));
    }

    [TestMethod]
    public async Task ProfileAllKeySubmissionNeverSeedsManualGlobalDraft()
    {
        var magnetic = new Fake();
        var profiles = new BaselineFake();
        var desktop = Guid.NewGuid(); var custom = Guid.NewGuid();
        profiles.Response.SelectedProfileId = custom;
        profiles.Response.ActiveProfileId = custom;
        profiles.Response.Dirty = false;
        profiles.Response.GlobalDefaults = new ProfileMagnetic {
            GlobalActuationMm = 1.0, GlobalDeadzone = new(0.0, 0.1),
            GlobalRapidTrigger = new(true, 0.4, 0.4, false, 0.0, 0.1) };
        profiles.Response.EffectiveGlobalDefaults = profiles.Response.GlobalDefaults;
        magnetic.Status.GlobalActuation = new() { Known = true, Raw = 40, Source = "SessionApplied" };
        magnetic.Status.GlobalDeadzone = new() { Known = true, TopRaw = 2,
            BottomRaw = 3, Source = "SessionApplied" };
        magnetic.Status.GlobalRapidTrigger = new() { Known = true, PressRaw = 6,
            ReleaseRaw = 6, Source = "SessionApplied" };
        var model = new MagneticSettingsModel(magnetic, profileClient: profiles);
        await model.RefreshAsync(); model.SelectGlobal();

        Assert.AreEqual((byte)10, model.ManualGlobalActuation.Raw);
        Assert.AreEqual("DeviceBaseline", model.ManualGlobalActuation.Source);
        Assert.AreEqual((byte)1, model.ManualGlobalDeadzone.BottomRaw);
        Assert.AreEqual((byte)4, model.ManualGlobalRapidTrigger.PressRaw);
        Assert.IsTrue(model.ProfileOverridesActuation);
        Assert.IsTrue(model.ProfileOverridesDeadzone);
        Assert.IsNull(model.GlobalDraft.ActuationMm);
        Assert.IsFalse(await model.ApplyGlobalActuationAsync());
        Assert.IsFalse(await model.ApplyGlobalDeadzoneAsync());
        Assert.IsEmpty(magnetic.Calls);

        profiles.Response.SelectedProfileId = desktop;
        profiles.Response.ActiveProfileId = desktop;
        magnetic.Status.GlobalActuation.Raw = 10;
        magnetic.Status.GlobalDeadzone.TopRaw = 0;
        magnetic.Status.GlobalDeadzone.BottomRaw = 1;
        await model.RefreshAsync();
        Assert.AreEqual((byte)10, model.ManualGlobalActuation.Raw);
        Assert.IsFalse(model.ProfileOverridesActuation);
        Assert.IsFalse(model.ProfileOverridesDeadzone);

        profiles.Response.SelectedProfileId = custom;
        profiles.Response.ActiveProfileId = custom;
        magnetic.Status.GlobalActuation.Raw = 40;
        magnetic.GlobalActuationWritten = mm => {
            profiles.Response.GlobalDefaults!.GlobalActuationMm = mm;
            profiles.Response.ActiveProfileId = null;
            profiles.Response.Dirty = true;
            magnetic.Status.GlobalActuation.Raw = (byte)Math.Round(mm * 10);
        };
        await model.RefreshAsync();
        model.EditGlobalActuation(1.5);
        Assert.IsTrue(await model.ApplyGlobalActuationAsync());
        Assert.AreEqual("global-actuation:1.5", magnetic.Calls.Last());
        Assert.AreEqual((byte)15, model.ManualGlobalActuation.Raw);
        Assert.IsTrue(profiles.Response.Dirty);
        Assert.IsNull(profiles.Response.ActiveProfileId);
        Assert.AreEqual(custom, profiles.Response.SelectedProfileId);
        Assert.AreEqual(1.5, profiles.Response.GlobalDefaults!.GlobalActuationMm);

        profiles.Response.ActiveProfileId = custom; profiles.Response.Dirty = false;
        magnetic.Status.GlobalActuation.Raw = 40;
        await model.RefreshAsync();
        Assert.AreEqual((byte)15, model.ManualGlobalActuation.Raw);
        Assert.IsTrue(model.ProfileOverridesActuation);
        profiles.Response.ActiveProfileId = desktop;
        profiles.Response.SelectedProfileId = desktop;
        magnetic.Status.GlobalActuation.Raw = 15;
        await model.RefreshAsync();
        Assert.AreEqual((byte)15, model.ManualGlobalActuation.Raw);
        Assert.IsFalse(model.ProfileOverridesActuation);
    }

    [TestMethod]
    public async Task GlobalRapidTriggerUsesManualDeadzoneAndBaselineFailureNeverFallsBackToProfileSubmission()
    {
        var magnetic = new Fake(); var profiles = new BaselineFake();
        profiles.Response.GlobalDefaults = new ProfileMagnetic {
            GlobalActuationMm = 1.0, GlobalDeadzone = new(0.0, 0.1) };
        profiles.Response.EffectiveGlobalDefaults = profiles.Response.GlobalDefaults;
        magnetic.Status.GlobalActuation = new() { Known = true, Raw = 40, Source = "SessionApplied" };
        magnetic.Status.GlobalDeadzone = new() { Known = true, TopRaw = 2,
            BottomRaw = 3, Source = "SessionApplied" };
        var model = new MagneticSettingsModel(magnetic, profileClient: profiles);
        await model.RefreshAsync(); model.SelectGlobal();
        model.EditGlobalRapidTrigger(0.5, 0.5, false);
        Assert.IsFalse(await model.ApplyGlobalRapidTriggerAsync());
        Assert.IsFalse(magnetic.Calls.Any(call => call.StartsWith("global-rt:")));

        profiles.Offline = true;
        await model.RefreshAsync();
        Assert.IsFalse(model.ManualGlobalActuation.Known);
        Assert.IsFalse(model.ManualGlobalDeadzone.Known);
        Assert.IsNull(model.GlobalDraft.ActuationMm);
        Assert.IsFalse(await model.ApplyGlobalActuationAsync());
        Assert.IsFalse(model.ProfileOverridesActuation);
    }

    [TestMethod]
    public async Task NullDocumentBaselineUsesDaemonResolvedHostIntentNotSessionSubmission()
    {
        var magnetic = new Fake(); var profiles = new BaselineFake();
        profiles.Response.GlobalDefaults = new ProfileMagnetic();
        profiles.Response.EffectiveGlobalDefaults = new ProfileMagnetic {
            GlobalActuationMm = 1.0, GlobalDeadzone = new(0.0, 0.1) };
        magnetic.Status.GlobalActuation = new() { Known = true, Raw = 40, Source = "SessionApplied" };
        magnetic.Status.GlobalDeadzone = new() { Known = true, TopRaw = 2,
            BottomRaw = 3, Source = "SessionApplied" };
        magnetic.Status.HostProfile.GlobalRtPress = new() { Known = true, Raw = 4,
            Source = "HostProfile" };
        magnetic.Status.HostProfile.GlobalRtRelease = new() { Known = true, Raw = 2,
            Source = "HostProfile" };
        var model = new MagneticSettingsModel(magnetic, profileClient: profiles);
        await model.RefreshAsync();

        Assert.AreEqual((byte)10, model.ManualGlobalActuation.Raw);
        Assert.AreEqual("HostProfile", model.ManualGlobalActuation.Source);
        Assert.AreEqual((byte)1, model.ManualGlobalDeadzone.BottomRaw);
        Assert.AreEqual("HostProfile", model.ManualGlobalDeadzone.Source);
        Assert.AreEqual((byte)4, model.ManualGlobalRapidTrigger.PressRaw);
        Assert.AreEqual((byte)2, model.ManualGlobalRapidTrigger.ReleaseRaw);
        Assert.IsNull(model.GlobalDraft.ActuationMm);
        Assert.IsFalse(await model.ApplyGlobalActuationAsync());
        Assert.IsEmpty(magnetic.Calls);
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
        public Action<double>? GlobalActuationWritten { get; set; }
        public Task<MagneticStatus>? Next { get; set; }
        public Task<MagneticStatus> GetStatusAsync() => Task.FromResult(Status);
        private Task<MagneticStatus> Record(string call) { Calls.Add(call); return Next ?? Task.FromResult(Status); }
        public Task<MagneticStatus> SetActuationAsync(ushort id, double mm) => Record($"actuation:{id}:{mm:F1}");
        public Task<MagneticStatus> SetRapidTriggerAsync(ushort id, double press, double release, bool resolve) => Record($"rt-on:{id}:{press:F1}:{release:F1}:{resolve}");
        public Task<MagneticStatus> DisableRapidTriggerAsync(ushort id) => Record($"rt-off:{id}");
        public Task<MagneticStatus> SetDeadzoneAsync(ushort id, double top, double bottom) => Record($"deadzone:{id}:{top:F1}:{bottom:F1}");
        public Task<MagneticStatus> ResetAllDeadzoneAsync() => Record("deadzone-reset-all");
        public Task<MagneticStatus> SetGlobalActuationAsync(double mm) {
            GlobalActuationWritten?.Invoke(mm);
            return Record($"global-actuation:{mm:F1}");
        }
        public Task<MagneticStatus> SetGlobalDeadzoneAsync(double top, double bottom)
        {
            Status.GlobalDeadzone = new MagneticGlobalDeadzoneState
            {
                Known = true,
                TopRaw = (byte)(top * 10),
                BottomRaw = (byte)(bottom * 10),
                Source = "SessionApplied"
            };
            Status.HostProfile.GlobalDeadzoneTop = new MagneticKnownRaw {
                Known = true, Raw = (byte)Math.Round(top * 10), Source = "HostProfile" };
            Status.HostProfile.GlobalDeadzoneBottom = new MagneticKnownRaw {
                Known = true, Raw = (byte)Math.Round(bottom * 10), Source = "HostProfile" };
            return Record($"global-deadzone:{top:F1}:{bottom:F1}");
        }
        public Task<MagneticStatus> SetGlobalRapidTriggerAsync(double press, double release, double top, double bottom, bool separate) =>
            Record($"global-rt:{press:F1}:{release:F1}:{top:F1}:{bottom:F1}:{separate}");
        public Task<MagneticStatus> SetDksAsync(ushort id, double start, double end, IReadOnlyList<MagneticDksSlot> slots, bool resolve) =>
            Record($"dks:{id}:{start:F1}:{end:F1}:{slots.Count}:{resolve}");
        public Task<MagneticStatus> RestoreDksStandardAsync(ushort id) => Record($"dks-standard:{id}");
        public Task<MagneticStatus> SetSpeedTapPairAsync(ushort first, ushort second) => Record($"pair-on:{first}:{second}");
        public Task<MagneticStatus> DisableSpeedTapPairAsync(ushort first, ushort second) => Record($"pair-off:{first}:{second}");
        public Task<MagneticStatus> SetSpeedTapMasterAsync(bool enabled) => Record($"master:{enabled}");
        public Task<MagneticStatus> ResetSpeedTapToProfileAsync() => Record("profile-reset");
        public Task<MagneticStatus> SetStaticAnalogEffectAsync(bool enabled) => Record($"analog:{enabled}");
        public Task<MagneticStatus> SetBatchActuationAsync(IReadOnlyList<ushort> ids, double mm) =>
            Record($"batch-actuation:{string.Join(',', ids)}:{mm:F1}");
        public Task<MagneticStatus> SetBatchRapidTriggerAsync(IReadOnlyList<ushort> ids, bool enable,
            double? press = null, double? release = null, bool resolveDks = false) =>
            Record($"batch-rt:{string.Join(',', ids)}:{enable}:{press:F1}:{release:F1}:{resolveDks}");
        public Task<MagneticStatus> SetBatchDeadzoneAsync(IReadOnlyList<ushort> ids, double top, double bottom) =>
            Record($"batch-deadzone:{string.Join(',', ids)}:{top:F1}:{bottom:F1}");
        public Task<MagneticStatus> AcknowledgeExternalResynchronizationAsync()
        {
            Status.PersistentSafetyQuarantine = false;
            Status.Health = "Clean";
            Status.Actuation.Clear();
            Status.RapidTrigger.RemoveAll(v => v.Source == "SessionApplied");
            Status.Deadzone.Clear();
            Status.Dks.Clear();
            Status.GlobalActuation = new MagneticGlobalActuationState { Known = false, Source = "Unknown" };
            Status.GlobalRapidTrigger = new MagneticGlobalRapidTriggerState { Known = false, Source = "Unknown" };
            Status.GlobalDeadzone = new MagneticGlobalDeadzoneState { Known = false, Source = "Unknown" };
            return Record("ack-external-resync");
        }
    }

    private sealed class BaselineFake : IProfileControlClient
    {
        public Task<ProfileApiResponse> GetDiagnosticsAsync(CancellationToken token = default) => throw new NotSupportedException();
        public ProfileApiResponse Response { get; } = new() { Status = "ok", ApiVersion = 1 };
        public bool Offline { get; set; }
        public Task<ProfileApiResponse> GetRuntimeAsync(CancellationToken token = default) =>
            Offline ? Task.FromException<ProfileApiResponse>(new HttpRequestException("offline")) :
            Task.FromResult(Response);
        public Task<ProfileApiResponse> ListAsync(CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> GetAsync(Guid id, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> CreateAsync(string name, long revision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> DuplicateAsync(Guid id, string name, long revision,
            CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> RenameAsync(Guid id, string name, long revision,
            CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> UpdateAsync(DeviceProfile profile, long revision,
            CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> DeleteAsync(Guid id, long revision,
            CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> UpdateDefaultsAsync(ProfileMagnetic defaults, long revision,
            CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> ActivateAsync(Guid id, ProfileActivationReason reason,
            long revision, ProfileMagnetic? temporaryOverride = null,
            CancellationToken token = default) => throw new NotSupportedException();
    }
}
