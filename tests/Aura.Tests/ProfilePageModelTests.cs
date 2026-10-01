using System.Net;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class ProfilePageModelTests
{
    private sealed class Fake : IProfileControlClient
    {
        public readonly Guid A = Guid.Parse("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
        public readonly Guid B = Guid.Parse("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
        public List<DeviceProfile> Profiles = [];
        public Guid Selected;
        public Guid? Active;
        public bool Dirty;
        public long Revision = 2;
        public bool Offline;
        public bool Incompatible;
        public bool InvalidResponse;
        public bool Corrupt;
        public HardwareRtGateStatus? HardwareGate;
        public string? NextOutcome;
        public string? FailureError;
        public TaskCompletionSource<bool>? ActivationGate;
        public readonly List<long> RevisionUsed = [];
        public readonly List<Guid> Activated = [];
        public readonly List<ProfileActivationReason> ActivationReasons = [];

        public Fake()
        {
            Profiles = [new DeviceProfile { Id = A, Name = "Desktop" },
                new DeviceProfile { Id = B, Name = "CS2" }];
            Selected = A; Active = A;
        }
        private void Available()
        {
            if (Offline) throw new ProfileApiException("daemon offline",
                category: ProfileApiErrorCategory.Transport);
            if (Incompatible) throw new ProfileApiException("Profile route missing",
                HttpStatusCode.NotFound, ProfileApiErrorCategory.IncompatibleDaemon);
            if (InvalidResponse) throw new ProfileApiException("Daemon Profile API returned an invalid response (HTTP 502 / text/plain).",
                HttpStatusCode.BadGateway, ProfileApiErrorCategory.InvalidResponse);
            if (Corrupt) throw new ProfileApiException("corrupt Profile document", HttpStatusCode.ServiceUnavailable);
        }
        private void Check(long expected)
        {
            Available(); RevisionUsed.Add(expected);
            if (expected != Revision) throw new ProfileApiException("revision conflict", HttpStatusCode.Conflict);
        }
        private ProfileApiResponse State(DeviceProfile? changed = null, string? outcome = null)
        {
            Available();
            return new ProfileApiResponse { Status = "ok", ApiVersion = 1,
                DocumentRevision = Revision, MutationRevision = (ulong)Revision,
                SelectedProfileId = Selected, ActiveProfileId = Active, Dirty = Dirty,
                Profiles = Profiles.Select(p => p.Clone()).ToList(),
                Profile = changed?.Clone(), Outcome = outcome, HardwareRtGate = HardwareGate,
                Error = outcome == "failed" ? FailureError ?? "mock operation failed" : null,
                Operations = outcome == "failed" ? [new ProfileApiOperation {
                    Kind = "KeyActuation", Identity = "KeyActuation:1026", Succeeded = false,
                    Error = "mock failure" }] : null };
        }
        public Task<ProfileApiResponse> ListAsync(CancellationToken token = default) => Task.FromResult(State());
        public Task<ProfileApiResponse> GetAsync(Guid id, CancellationToken token = default) =>
            Task.FromResult(State(Profiles.Single(p => p.Id == id)));
        public Task<ProfileApiResponse> GetRuntimeAsync(CancellationToken token = default) => Task.FromResult(State());
        public Task<ProfileApiResponse> GetHardwareRtGateAsync(CancellationToken token = default) => Task.FromResult(State());
        public Task<ProfileApiResponse> GetDiagnosticsAsync(CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> CreateAsync(string name, long expectedRevision, CancellationToken token = default)
        {
            Check(expectedRevision); var p = new DeviceProfile { Name = name };
            Profiles.Add(p); Revision++; return Task.FromResult(State(p));
        }
        public Task<ProfileApiResponse> DuplicateAsync(Guid id, string name, long expectedRevision, CancellationToken token = default)
        {
            Check(expectedRevision); var p = Profiles.Single(x => x.Id == id).Clone();
            p.Id = Guid.NewGuid(); p.Name = name; Profiles.Add(p); Revision++;
            return Task.FromResult(State(p));
        }
        public Task<ProfileApiResponse> RenameAsync(Guid id, string name, long expectedRevision, CancellationToken token = default)
        {
            Check(expectedRevision); var p = Profiles.Single(x => x.Id == id); p.Name = name;
            Revision++; return Task.FromResult(State(p));
        }
        public Task<ProfileApiResponse> UpdateAsync(DeviceProfile profile, long expectedRevision,
            CancellationToken token = default)
        {
            Check(expectedRevision);
            var index = Profiles.FindIndex(x => x.Id == profile.Id);
            Profiles[index] = profile.Clone(); Revision++;
            if (Active == profile.Id) { Active = null; Dirty = true; }
            return Task.FromResult(State(profile));
        }
        public Task<ProfileApiResponse> DeleteAsync(Guid id, long expectedRevision, CancellationToken token = default)
        {
            Check(expectedRevision); Profiles.RemoveAll(x => x.Id == id); Revision++;
            return Task.FromResult(State());
        }
        public Task<ProfileApiResponse> UpdateDefaultsAsync(ProfileMagnetic defaults, long expectedRevision,
            CancellationToken token = default) => throw new NotSupportedException();
        public async Task<ProfileApiResponse> ActivateAsync(Guid id, ProfileActivationReason reason, long expectedRevision,
            ProfileMagnetic? temporaryOverride = null, CancellationToken token = default)
        {
            Check(expectedRevision); Activated.Add(id); ActivationReasons.Add(reason);
            if (ActivationGate is not null) await ActivationGate.Task.WaitAsync(token);
            if (Selected != id) { Selected = id; Revision++; }
            var outcome = NextOutcome ?? "succeeded"; NextOutcome = null;
            Active = outcome == "succeeded" ? id : null; Dirty = outcome != "succeeded";
            return State(outcome: outcome);
        }
    }

    [TestMethod]
    public async Task ProfileUiSaveDoesNotNotifyHoldAndApplyUsesOneManualRequest()
    {
        var api = new Fake(); var model = new ProfilePageModel(api);
        await model.LoadAsync(); model.Draft!.Magnetic.GlobalActuationMm = 1.2;
        Assert.IsTrue(await model.SaveAsync());
        Assert.IsEmpty(api.Activated); // no separate/manual-hold request for Save
        Assert.IsFalse(model.Snapshot!.ActiveProfileId.HasValue);
        api.NextOutcome = "deferred";
        Assert.IsFalse(await model.ApplyAsync());
        CollectionAssert.AreEqual(new[] { api.A }, api.Activated);
        CollectionAssert.AreEqual(new[] { ProfileActivationReason.Manual }, api.ActivationReasons);
        Assert.AreEqual(ProfilePageState.Deferred, model.State);
        Assert.IsTrue(model.Snapshot.Dirty);
    }

    [TestMethod]
    public async Task InitialStateDirtyDeferredAndDaemonUnavailableAreDistinct()
    {
        var api = new Fake(); var model = new ProfilePageModel(api);
        await model.LoadAsync();
        Assert.AreEqual(ProfilePageState.Ready, model.State);
        Assert.AreEqual("Desktop", model.CurrentName);
        api.Dirty = true; api.Active = null;
        await model.LoadAsync(); Assert.AreEqual(ProfilePageState.NeedsApply, model.State);
        api.NextOutcome = "deferred";
        Assert.IsFalse(await model.ApplyAsync());
        Assert.AreEqual(ProfilePageState.Deferred, model.State);
        StringAssert.Contains(model.StateDescription, "重试应用");
        api.Offline = true; await model.LoadAsync();
        Assert.AreEqual(ProfilePageState.DaemonUnavailable, model.State);
        api.Offline = false; await model.LoadAsync();
        Assert.AreEqual(ProfilePageState.NeedsApply, model.State);
        api.Corrupt = true; await model.LoadAsync();
        Assert.AreEqual(ProfilePageState.DocumentUnavailable, model.State);
    }

    [TestMethod]
    public async Task ApplyCommandTracksDraftSelectionDirtyBusyAndFailure()
    {
        var api = new Fake(); var model = new ProfilePageModel(api);
        await model.LoadAsync();
        Assert.IsFalse(model.CanApply); // selected == active, clean, saved
        model.Edit(api.B);
        Assert.IsTrue(model.CanApply); // another Profile is being edited
        model.Edit(api.A);
        model.Draft!.Magnetic.GlobalActuationMm = 1.5;
        Assert.IsTrue(model.CanApply);
        model.Draft.Magnetic.GlobalActuationMm = 4.5;
        Assert.IsFalse(model.CanApply); // validation error
        model.Draft.Magnetic.GlobalActuationMm = null;
        api.Active = null; api.Dirty = true;
        await model.LoadAsync();
        Assert.IsTrue(model.CanApply); // manual magnetic mutation invalidated active
        api.ActivationGate = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var applying = model.ApplyAsync();
        Assert.IsTrue(model.IsBusy);
        Assert.IsFalse(model.CanApply);
        api.ActivationGate.SetResult(true);
        Assert.IsTrue(await applying);
        api.ActivationGate = null;
        Assert.IsFalse(model.CanApply);
        api.Active = null; api.Dirty = true; await model.LoadAsync();
        api.NextOutcome = "failed";
        Assert.IsFalse(await model.ApplyAsync());
        Assert.IsTrue(model.CanApply);
        api.Offline = true; await model.LoadAsync();
        Assert.IsFalse(model.CanApply);
    }

    [TestMethod]
    public async Task IncompatibleDaemonHasDedicatedChineseStateAndCanRecover()
    {
        var api = new Fake { Incompatible = true }; var model = new ProfilePageModel(api);
        await model.LoadAsync();
        Assert.AreEqual(ProfilePageState.IncompatibleDaemon, model.State);
        StringAssert.Contains(model.StateDescription, "后台服务不支持配置文件功能");
        Assert.DoesNotContain("JSON", model.Notice);
        api.Incompatible = false;
        await model.LoadAsync();
        Assert.AreEqual(ProfilePageState.Ready, model.State);
        api.InvalidResponse = true;
        await model.LoadAsync();
        Assert.AreEqual(ProfilePageState.ApiProtocolError, model.State);
        Assert.DoesNotContain("JsonException", model.Notice);
    }

    [TestMethod]
    public async Task SaveThenApplyUsesReturnedRevisionAndSelectedChangeConsumesAnother()
    {
        var api = new Fake(); var model = new ProfilePageModel(api);
        await model.LoadAsync(); model.Edit(api.B);
        model.Draft!.Magnetic.GlobalActuationMm = 1.2;
        Assert.IsTrue(model.HasUnsavedChanges);
        Assert.IsTrue(await model.ApplyAsync());
        CollectionAssert.AreEqual(new long[] { 2, 3 }, api.RevisionUsed);
        Assert.AreEqual(4, model.Snapshot!.DocumentRevision);
        Assert.AreEqual(api.B, model.Snapshot.SelectedProfileId);
        Assert.AreEqual(api.B, model.Snapshot.ActiveProfileId);
        Assert.AreEqual(ProfilePageState.Ready, model.State);
        Assert.IsFalse(model.HasUnsavedChanges);
        Assert.IsTrue(await model.CreateAsync("Rhythm"));
        Assert.AreEqual(4, api.RevisionUsed.Last());
    }

    [TestMethod]
    public async Task UnknownActuationBaselineShowsClearChineseFailure()
    {
        var api = new Fake { Dirty = true, Active = null, NextOutcome = "failed",
            FailureError = "Device global baseline unknown for actuation" };
        var model = new ProfilePageModel(api);
        await model.LoadAsync();
        Assert.IsFalse(await model.ApplyAsync());
        StringAssert.Contains(model.Notice, "全局基础设置");
        Assert.DoesNotContain("resetType", model.Notice);
        Assert.IsTrue(model.CanApply);
        Assert.IsTrue(model.Snapshot!.Dirty);
        Assert.IsNull(model.Snapshot.ActiveProfileId);
    }

    [TestMethod]
    public async Task SavedDocumentSurvivesFailedApplyAndShowsDiagnostics()
    {
        var api = new Fake(); var model = new ProfilePageModel(api);
        await model.LoadAsync(); model.Draft!.Magnetic.GlobalActuationMm = 1.5;
        api.NextOutcome = "failed";
        Assert.IsFalse(await model.ApplyAsync());
        Assert.AreEqual(3, api.RevisionUsed.Last());
        Assert.AreEqual(3, api.Revision);
        Assert.AreEqual(1.5, api.Profiles[0].Magnetic.GlobalActuationMm);
        Assert.AreEqual(ProfilePageState.ApplyFailed, model.State);
        StringAssert.Contains(model.Notice, "已保存");
        StringAssert.Contains(model.ApplyDetails!, "KeyActuation");
        Assert.IsNull(model.Snapshot!.ActiveProfileId);
    }

    [TestMethod]
    public async Task UnknownBaselineAndUnsupportedWideRtHaveActionableChineseMessages()
    {
        var api = new Fake(); var model = new ProfilePageModel(api);
        await model.LoadAsync();
        api.NextOutcome = "failed"; api.FailureError = "Device global baseline unknown for actuation";
        Assert.IsFalse(await model.ApplyAsync());
        StringAssert.Contains(model.Notice, "磁轴设置页");
        api.NextOutcome = "failed";
        api.FailureError = "Legacy global RT settings cannot be safely applied; migrate or remove legacy settings";
        Assert.IsFalse(await model.ApplyAsync());
        StringAssert.Contains(model.Notice, "逐键设置");
    }

    [TestMethod]
    public async Task ConflictReloadsButPreservesDraftUntilExplicitDecision()
    {
        var api = new Fake(); var model = new ProfilePageModel(api);
        await model.LoadAsync(); model.Draft!.Magnetic.GlobalActuationMm = 1.9;
        api.Profiles[0].Name = "Elsewhere"; api.Revision++;
        Assert.IsFalse(await model.SaveAsync());
        Assert.IsTrue(model.HasConflict);
        Assert.AreEqual(1.9, model.Draft.Magnetic.GlobalActuationMm);
        Assert.AreEqual("Elsewhere", model.Profiles[0].Name);
        StringAssert.Contains(model.Notice, "重新加载");
        Assert.IsFalse(await model.SaveAsync());
        model.KeepDraftAfterConflict();
        Assert.IsTrue(await model.SaveAsync());
        Assert.AreEqual(1.9, api.Profiles[0].Magnetic.GlobalActuationMm);
    }

    [TestMethod]
    public async Task CrudAndDeleteGuardsDoNotWriteSelectedActiveOrLast()
    {
        var api = new Fake(); var model = new ProfilePageModel(api);
        await model.LoadAsync();
        Assert.IsFalse(await model.DeleteAsync(api.A));
        Assert.IsTrue(await model.CreateAsync("Rhythm"));
        var rhythm = model.EditingId!.Value;
        Assert.IsTrue(await model.RenameAsync(rhythm, "Rhythm 2"));
        Assert.AreEqual("Rhythm 2", model.EditingName);
        Assert.IsTrue(await model.DuplicateAsync(rhythm, "Rhythm - 副本"));
        Assert.HasCount(4, model.Profiles);
        Assert.IsTrue(await model.DeleteAsync(model.EditingId!.Value));
        Assert.HasCount(3, model.Profiles);
        api.Active = api.B; await model.LoadAsync();
        Assert.IsFalse(await model.DeleteAsync(api.B));
        api.Profiles = [api.Profiles[0]]; api.Selected = api.A; api.Active = api.A;
        await model.LoadAsync();
        Assert.IsFalse(model.CanDeleteEditing);
        Assert.IsFalse(await model.DeleteAsync(api.A));
    }

    [TestMethod]
    public async Task ManyProfilesLongNamesAndDaemonRecoveryKeepOneClientPath()
    {
        var api = new Fake { Offline = true }; var model = new ProfilePageModel(api);
        await model.LoadAsync(); Assert.AreEqual(ProfilePageState.DaemonUnavailable, model.State);
        api.Offline = false;
        for (var i = 0; i < 12; i++) api.Profiles.Add(new DeviceProfile {
            Name = $"游戏场景 {i} " + new string('长', 60) });
        await model.LoadAsync();
        Assert.HasCount(14, model.Profiles);
        Assert.IsTrue(model.Edit(api.Profiles.Last().Id));
        Assert.IsGreaterThan(60, model.EditingName.Length);
        Assert.AreEqual(ProfilePageState.Ready, model.State);
    }
    [TestMethod]
    public async Task RapidTriggerMasterHasSpecificLiveValidationAndClearsOnEnable()
    {
        var api = new Fake(); var model = new ProfilePageModel(api); await model.LoadAsync();
        model.Draft!.Magnetic.GlobalRapidTrigger = new(false, 1.0, 1.0, false, 0, 0.1);
        Assert.IsFalse(model.CanApply);
        StringAssert.Contains(model.DraftValidationText, "尚未启用快速触发");
        Assert.AreEqual("RtMasterDisabled", model.RapidTriggerIssues.Single().Code);
        Assert.IsFalse(await model.SaveAsync()); Assert.IsEmpty(api.RevisionUsed);
        model.Draft.Magnetic.GlobalRapidTrigger = model.Draft.Magnetic.GlobalRapidTrigger with { Enabled = true };
        Assert.AreEqual("", model.DraftValidationText); Assert.IsTrue(model.CanApply);
        Assert.IsFalse(await model.SaveAsync()); // valid legacy values still cannot create a new legacy representation
        StringAssert.Contains(model.Notice, "不能新增");
        Assert.AreEqual("", model.RapidTriggerValidationText);
        Assert.IsEmpty(api.RevisionUsed); Assert.IsEmpty(api.Activated);
    }

    [TestMethod]
    public async Task RapidTriggerNumericSeparateAndMultipleIssuesRemainSpecific()
    {
        var model = new ProfilePageModel(new Fake()); await model.LoadAsync();
        model.Draft!.Magnetic.GlobalRapidTrigger = new(true, 0.0, 1.0, true, 0, 0.1);
        CollectionAssert.AreEqual(new[] { "RtSensitivityRange" }, model.RapidTriggerIssues.Select(x => x.Code).ToArray());
        StringAssert.Contains(model.DraftValidationText, "0.1–2.5");
        Assert.IsFalse(model.DraftValidationText.Contains("尚未启用"));
        model.Draft.Magnetic.GlobalRapidTrigger = new(false, 0.0, 1.0, false, 0, 0.1);
        Assert.HasCount(3, model.RapidTriggerIssues);
        StringAssert.Contains(model.DraftValidationText, "同步模式");
        StringAssert.Contains(model.DraftValidationText, "尚未启用");
        model.Draft.Magnetic.GlobalRapidTrigger = new(true, 0.5, 1.5, true, 0, 0.1);
        Assert.AreEqual("", model.DraftValidationText);
        model.Draft.Magnetic.GlobalRapidTrigger = new(true, 0.5, 0.5, false, 0.6, 0.1);
        Assert.AreEqual("RtDeadzoneRange", model.RapidTriggerIssues.Single().Code);
    }

    [TestMethod]
    public async Task RapidTriggerDksConflictIsInlineAndDisabledPerKeyIsLegal()
    {
        var model = new ProfilePageModel(new Fake()); await model.LoadAsync();
        var key = new ProfileKey { LogicalId = 0x0701, RapidTrigger = new(true, 0.5, 0.5),
            Dks = new(0.5, 2.0, Enumerable.Range(0, 4).Select(_ => new ProfileDksSlot()).ToList()) };
        model.Draft!.Magnetic.Keys.Add(key);
        Assert.AreEqual("RtDksConflict", model.RapidTriggerIssues.Single().Code);
        StringAssert.Contains(model.DraftValidationText, "DKS"); Assert.IsFalse(model.CanApply);
        key.RapidTrigger = key.RapidTrigger with { Enabled = false };
        Assert.AreEqual("", model.DraftValidationText); Assert.IsTrue(model.CanApply);
    }

    [TestMethod]
    public async Task RapidTriggerSwitchProfileDropsOldValidationWithoutStaleNotice()
    {
        var api = new Fake(); var model = new ProfilePageModel(api); await model.LoadAsync();
        model.Draft!.Magnetic.GlobalRapidTrigger = new(false, 1, 1, false, 0, 0.1);
        Assert.IsFalse(await model.SaveAsync()); model.DiscardDraft();
        Assert.IsTrue(model.Edit(api.B));
        Assert.AreEqual("", model.RapidTriggerValidationText);
        Assert.AreEqual("", model.DraftValidationText); Assert.AreEqual("", model.Notice);
    }

    [TestMethod]
    public async Task RapidTriggerSaveConflictRetainsFullDraftAndValidatesAfterReload()
    {
        var rt = new ProfileRapidTrigger(true, 0.5, 1.5, true, 0.1, 0.2);
        var api = new Fake(); api.Profiles[0].Magnetic.GlobalRapidTrigger = rt;
        var model = new ProfilePageModel(api); await model.LoadAsync();
        model.Draft!.Name = "保留旧数据并改名"; api.Revision++;
        Assert.IsFalse(await model.SaveAsync()); Assert.IsTrue(model.HasConflict);
        Assert.AreEqual(rt, model.Draft.Magnetic.GlobalRapidTrigger);
        Assert.AreEqual("", model.DraftValidationText); Assert.IsEmpty(api.Activated);
        model.KeepDraftAfterConflict(); Assert.IsTrue(await model.SaveAsync());
        Assert.AreEqual(rt, model.Draft.Magnetic.GlobalRapidTrigger);
    }

    [TestMethod]
    public async Task LegacyRtSaveCannotCreateOrModifyButCanRemoveExistingData()
    {
        var api = new Fake(); var legacy = new ProfileRapidTrigger(true, 0.5, 1.5, true, 0.2, 0.3);
        api.Profiles[0].Magnetic.GlobalRapidTrigger = legacy;
        var model = new ProfilePageModel(api); await model.LoadAsync();
        model.Draft!.Magnetic.GlobalRapidTrigger = legacy with { PressMm = 0.7 };
        Assert.IsFalse(await model.SaveAsync()); Assert.IsEmpty(api.RevisionUsed);
        Assert.AreEqual(legacy, api.Profiles[0].Magnetic.GlobalRapidTrigger);
        model.Draft.Magnetic.GlobalRapidTrigger = null;
        Assert.IsTrue(await model.SaveAsync()); Assert.IsNull(api.Profiles[0].Magnetic.GlobalRapidTrigger);
        Assert.IsEmpty(api.Activated);
    }

    [TestMethod]
    public void RapidTriggerNativeCopyAndWrappingHaveLayoutGuardrails()
    {
        var root = new DirectoryInfo(AppContext.BaseDirectory);
        while (root is not null && !File.Exists(Path.Combine(root.FullName, "winui", "Pages", "ProfilesPage.xaml"))) root = root.Parent;
        Assert.IsNotNull(root);
        var text = File.ReadAllText(Path.Combine(root.FullName, "winui", "Pages", "ProfilesPage.xaml"));
        Assert.IsFalse(text.Contains("此配置文件快速触发"));
        StringAssert.Contains(text, "管理所选按键");
        StringAssert.Contains(text, "启用快速触发设置（旧版参数）");
        StringAssert.Contains(text, "x:Name=\"RtKeySetSummary\"");
        Assert.IsFalse(text.Contains("全局快速触发"));
        Assert.IsFalse(text.Contains("Global Rapid Trigger"));
        StringAssert.Contains(text, "x:Name=\"RapidTriggerValidationBar\"");
        StringAssert.Contains(text, "x:Name=\"DraftValidationBar\"");
        Assert.IsFalse(text.Contains("Foreground=\"Red\""));
        // Structural guard only: actual narrow/theme pixels are a separate visual check.
    }

    [TestMethod]
    public async Task RapidTriggerBatchEditPreservesEachKeysExtensionData()
    {
        var model = new ProfilePageModel(new Fake()); await model.LoadAsync();
        model.SetRapidTriggerDraftForKeys([0x0701, 0x0602], new(true, 0.5, 1.5, true));
        foreach (var key in model.Draft!.Magnetic.Keys.Where(k => k.RapidTrigger is not null))
            key.RapidTrigger!.Extensions = new() {
                ["future_rt"] = System.Text.Json.JsonSerializer.SerializeToElement(key.LogicalId),
                ["continuous"] = System.Text.Json.JsonSerializer.SerializeToElement(false)
            };
        model.SetRapidTriggerDraftForKeys([0x0701, 0x0602], new(false, 0.7, 1.2, true));
        foreach (var key in model.Draft.Magnetic.Keys.Where(k => k.RapidTrigger is not null)) {
            Assert.IsFalse(key.RapidTrigger!.Enabled);
            Assert.AreEqual(key.LogicalId, key.RapidTrigger.Extensions!["future_rt"].GetUInt16());
        }
    }

    [TestMethod]
    public async Task RapidTriggerSelectionCountsAreDraftStateNotGlobalMaster()
    {
        var api = new Fake(); var model = new ProfilePageModel(api); await model.LoadAsync();
        Assert.AreEqual(0, model.ManagedRapidTriggerKeyCount);
        Assert.AreEqual(0, model.EnabledRapidTriggerKeyCount);
        var ids = MagneticKeyLayout.Keys.Select(k => k.LogicalId).ToArray();
        model.SetRapidTriggerDraftForKeys(ids.Take(1), new(false, 0.5, 1.5));
        Assert.AreEqual(1, model.ManagedRapidTriggerKeyCount);
        Assert.AreEqual(0, model.EnabledRapidTriggerKeyCount);
        Assert.AreEqual("", model.RapidTriggerValidationText); // disabled key is legal; no overall master required
        model.SetRapidTriggerDraftForKeys(ids.Take(1), new(true, 0.5, 1.5));
        Assert.AreEqual(1, model.EnabledRapidTriggerKeyCount);
        model.SetRapidTriggerDraftForKeys(ids.Take(3), new(true, 0.5, 1.5));
        Assert.AreEqual(3, model.EnabledRapidTriggerKeyCount);
        model.SetRapidTriggerDraftForKeys(ids, new(true, 0.5, 1.5));
        Assert.AreEqual(ids.Length, model.EnabledRapidTriggerKeyCount);
        Assert.IsNull(model.Draft!.Magnetic.GlobalRapidTrigger);
        StringAssert.Contains(model.RapidTriggerSelectionSummary, $"{ids.Length} 个");
        Assert.IsTrue(await model.SaveAsync());
        Assert.IsEmpty(api.Activated);
        Assert.AreEqual(ids.Length, model.EnabledRapidTriggerKeyCount);
        model.SetRapidTriggerDraftForKeys(ids, null);
        Assert.AreEqual(0, model.ManagedRapidTriggerKeyCount);
        Assert.IsTrue(model.HasUnsavedChanges);
        Assert.IsEmpty(api.Activated);
    }

    [TestMethod]
    public async Task RapidTriggerBatchDraftPreservesOtherFieldsAndRejectsUnknownKeys()
    {
        var api = new Fake(); var model = new ProfilePageModel(api); await model.LoadAsync();
        var id = MagneticKeyLayout.Keys.First().LogicalId;
        model.Draft!.Magnetic.Keys.Add(new ProfileKey { LogicalId = id, ActuationMm = 0.5,
            Deadzone = new(0.1, 0.2), Dks = new(1, 3.6, [], false) });
        model.SetRapidTriggerDraftForKeys([id], new(true, 0.5, 1.5));
        Assert.AreEqual(0.5, model.Draft.Magnetic.Keys.Single().ActuationMm);
        Assert.AreEqual(new ProfileDeadzone(0.1, 0.2), model.Draft.Magnetic.Keys.Single().Deadzone);
        Assert.AreEqual("RtDksConflict", model.RapidTriggerIssues.Single().Code);
        var before = System.Text.Json.JsonSerializer.Serialize(model.Draft, ProfileJson.Options);
        Assert.ThrowsExactly<ArgumentException>(() => model.SetRapidTriggerDraftForKeys([id, ushort.MaxValue], new(true, 1, 1)));
        Assert.AreEqual(before, System.Text.Json.JsonSerializer.Serialize(model.Draft, ProfileJson.Options));
        Assert.IsEmpty(api.Activated);
    }

    [TestMethod]
    public void RapidTriggerLegacySchemaRemainsReadableWithoutReinterpretation()
    {
        var document = ProfileJson.NewDefault();
        document.GlobalDefaults.GlobalRapidTrigger = new(true, 1, 1, false, 0, 0.1);
        var profile = document.Profiles.Single();
        profile.Magnetic.GlobalRapidTrigger = new(true, 0.5, 1.5, true, 0.1, 0.2);
        profile.Magnetic.Keys.Add(new ProfileKey { LogicalId = MagneticKeyLayout.Keys.First().LogicalId,
            RapidTrigger = new(false, 0.5, 1.5) });
        var json = System.Text.Json.JsonSerializer.Serialize(document, ProfileJson.Options);
        var restored = ProfileJson.Deserialize(json);
        Assert.AreEqual(document.GlobalDefaults.GlobalRapidTrigger, restored.GlobalDefaults.GlobalRapidTrigger);
        Assert.AreEqual(profile.Magnetic.GlobalRapidTrigger, restored.Profiles.Single().Magnetic.GlobalRapidTrigger);
        Assert.AreEqual(profile.Magnetic.Keys.Single().RapidTrigger, restored.Profiles.Single().Magnetic.Keys.Single().RapidTrigger);
        Assert.AreEqual(1, restored.SchemaVersion);
    }

    [TestMethod]
    public async Task HardwareGateLiveRefreshDoesNotChangeDraftRevisionOrApplyState()
    {
        var api = new Fake(); api.Profiles[0].Magnetic.Keys.Add(new ProfileKey {
            LogicalId = MagneticKeyLayout.Keys.First().LogicalId, RapidTrigger = new(true, 0.5, 1.5) });
        var model = new ProfilePageModel(api); await model.LoadAsync();
        Assert.AreEqual("unknown", model.HardwareRtGateState);
        var revision = model.Snapshot!.DocumentRevision;
        foreach (var state in new[] { "off", "on", "unknown" }) {
            api.HardwareGate = new() { State = state }; api.Revision++;
            await model.RefreshHardwareRtGateAsync();
            Assert.AreEqual(state, model.HardwareRtGateState);
            Assert.AreEqual(1, model.EnabledRapidTriggerKeyCount);
            Assert.IsFalse(model.HasUnsavedChanges);
            Assert.IsFalse(model.CanApply);
            Assert.AreEqual(revision, model.Snapshot.DocumentRevision);
            Assert.IsTrue(model.Draft!.Magnetic.Keys.Single().RapidTrigger!.Enabled);
        }
        Assert.IsEmpty(api.Activated);
        model.Draft!.Magnetic.Keys.Single().RapidTrigger = new(false, 0.5, 1.5);
        api.HardwareGate = new() { State = "on" }; await model.RefreshHardwareRtGateAsync();
        Assert.IsTrue(model.HasUnsavedChanges); Assert.IsTrue(model.CanApply);
        Assert.AreEqual(1, model.ManagedRapidTriggerKeyCount); Assert.AreEqual(0, model.EnabledRapidTriggerKeyCount);
        api.Offline = true; await model.RefreshHardwareRtGateAsync();
        Assert.AreEqual("unknown", model.HardwareRtGateState);
        Assert.IsTrue(model.HasUnsavedChanges); Assert.AreEqual(revision, model.Snapshot.DocumentRevision);
    }

    [TestMethod]
    public async Task HardwareStatusDoesNotClaimDraftIsAppliedAndPreservesProfileSwitch()
    {
        var api = new Fake { HardwareGate = new() { State = "off" } };
        var model = new ProfilePageModel(api); await model.LoadAsync();
        StringAssert.Contains(model.HardwareRtGateText, "不会生效");
        api.HardwareGate = new() { State = "on" }; await model.RefreshHardwareRtGateAsync();
        StringAssert.Contains(model.HardwareRtGateText, "草稿不会自动应用");
        Assert.IsTrue(model.Edit(api.B));
        Assert.AreEqual("on", model.HardwareRtGateState);
        Assert.AreEqual(api.B, model.EditingId); Assert.IsTrue(model.CanApply);
        api.HardwareGate = new() { State = "unexpected" }; await model.RefreshHardwareRtGateAsync();
        Assert.AreEqual("unknown", model.HardwareRtGateState);
        StringAssert.Contains(model.HardwareRtGateText, "无法读取");
    }

}
