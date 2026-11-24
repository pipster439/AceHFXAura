using System.Net;
using System.Text;
using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class AutomationUiClosureTests
{
    private sealed class Api : HttpMessageHandler
    {
        public List<DeviceProfile> Profiles = [new() { Name = "Desktop" }];
        public DeviceProfileAutomationConfig Saved = new() { Enabled = true };
        public DeviceProfileAutomationDecision Decision = new() { Enabled = true, HardwareActivationAllowed = true };
        public long Revision = 7;
        public bool Capability = true, LightingCapability = true, ConflictCreate, ConflictSave, Reject, LoseCreateResponse, CancelCreate, WrongProfile, LoseSaveResponse, CancelSave;
        public List<(string Path, string Body)> Calls = [];
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        {
            var path = request.RequestUri!.AbsolutePath;
            var body = request.Content is null ? "" : await request.Content.ReadAsStringAsync(token);
            Calls.Add((path, body));
            DeviceProfile? created = null;
            if (request.Method == HttpMethod.Post) {
                using var parsed = JsonDocument.Parse(body); var data = parsed.RootElement;
                if (ConflictCreate && path.EndsWith("/create") || ConflictSave && path.EndsWith("/automation")) {
                    Revision++; ConflictCreate = ConflictSave = false;
                }
                if (data.GetProperty("expected_revision").GetInt64() != Revision)
                    return Reply(new { status = "error", error = "revision conflict" }, HttpStatusCode.Conflict);
                if (Reject) return Reply(new { status = "error", error = "duplicate name" }, HttpStatusCode.UnprocessableEntity);
                if (path.EndsWith("/create")) {
                    created = new() { Name = data.GetProperty("name").GetString()!, ActivationBackend = "hardware_slot",
                        HardwareSlot = data.GetProperty("hardware_slot").GetInt32(),
                        Lighting = new() { ControlOwner = data.GetProperty("lighting_control_owner").GetString() } };
                    Profiles.Add(created); Revision++;
                    if (LoseCreateResponse) throw new HttpRequestException("response lost after commit");
                    if (CancelCreate) throw new OperationCanceledException(token);
                    if (WrongProfile) created = new() { Name = created.Name }; // old/invalid response must never bind
                } else {
                    Assert.AreEqual("/api/device-profiles/automation", path);
                    Saved = data.GetProperty("device_profile_automation").Deserialize<DeviceProfileAutomationConfig>(ProfileJson.Options)!;
                    Revision++;
                    if (LoseSaveResponse) throw new HttpRequestException("save response lost after commit");
                    if (CancelSave) throw new OperationCanceledException(token);
                }
            }
            return Reply(new ProfileApiResponse { Status = "ok", ApiVersion = 1, DocumentRevision = Revision,
                LightingControlOwnerSupported = LightingCapability, HardwareProfileCreateSupported = Capability,
                Profiles = Profiles, Profile = created, DeviceProfileAutomation = JsonSerializer.SerializeToElement(Saved, ProfileJson.Options),
                AutomationConfigurationAvailable = true, AutomationDecision = Decision });
        }
        private static HttpResponseMessage Reply(object payload, HttpStatusCode code = HttpStatusCode.OK) => new(code) {
            Content = new StringContent(JsonSerializer.Serialize(payload, ProfileJson.Options), Encoding.UTF8, "application/json") };
        public DeviceProfileAutomationModel Model() => new(new ProfileControlClient(new HttpClient(this)));
    }

    [TestMethod]
    public async Task TargetLabelsUseRealBackendSlotAndLightingIncludingLegacyDefaults()
    {
        var api = new Api();
        for (int slot = 1; slot <= 5; slot++) {
            api.Profiles.Add(new() { Name = $"bank {slot}", ActivationBackend = "hardware_slot", HardwareSlot = slot });
            api.Profiles.Add(new() { Name = $"rgb {slot}", ActivationBackend = "hardware_slot", HardwareSlot = slot,
                Lighting = new() { ControlOwner = "AuraDirectRgb" } });
        }
        var model = api.Model(); await model.RefreshAsync(); var choices = model.TargetChoices(null, false);
        Assert.AreEqual("Aura 管理 · Desktop · Aura 动态灯光", choices[0].Label);
        foreach (var p in api.Profiles.Skip(1)) {
            var expected = $"板载槽位 {p.HardwareSlot} · {p.Name} · {(p.Name.StartsWith("rgb") ? "Aura 动态灯光" : "板载灯光")}";
            Assert.AreEqual(expected, choices.Single(c => c.Id == p.Id).Label);
        }
        Assert.HasCount(11, choices.Select(c => c.Id).Distinct());
    }

    [TestMethod]
    [DataRow(1, "FirmwareBank")][DataRow(2, "AuraDirectRgb")][DataRow(3, "AuraDirectRgb")]
    [DataRow(4, "FirmwareBank")][DataRow(5, "AuraDirectRgb")]
    public async Task ExplicitCreateReturnsRealIdAndImmediatelyBindsRuleAndFallback(int slot, string owner)
    {
        var api = new Api(); var model = api.Model(); await model.RefreshAsync();
        var original = model.Draft!.Clone();
        var id = await model.CreateHardwareAsync($"slot {slot}", slot, owner);
        Assert.IsNotNull(id); Assert.AreEqual(8L, model.DocumentRevision);
        Assert.IsFalse(model.HasConflict); Assert.IsFalse(model.HasUnsavedChanges);
        Assert.IsEmpty(api.Saved.Bindings); Assert.IsNull(api.Saved.FallbackProfileId);
        var p = model.Profiles.Single(p => p.Id == id);
        Assert.AreEqual(slot, p.HardwareSlot); Assert.AreEqual(owner, p.Lighting.ControlOwner);
        Assert.IsEmpty(p.Magnetic.Keys); Assert.IsNull(p.Magnetic.GlobalActuationMm);
        Assert.AreEqual("LegacyUnmanaged", p.Lighting.Ownership);
        model.UpsertRule(null, "CS2.EXE", id, 110, true); model.SetFallback(id);
        Assert.IsTrue(await model.SaveAsync());
        Assert.AreEqual(id, api.Saved.Bindings.Single().ProfileId); Assert.AreEqual(id, api.Saved.FallbackProfileId);
        Assert.AreEqual(9L, model.DocumentRevision);
        Assert.HasCount(2, api.Calls.Where(c => c.Body.Length > 0));
        using var create = JsonDocument.Parse(api.Calls.Single(c => c.Path.EndsWith("/create")).Body);
        Assert.AreEqual(7L, create.RootElement.GetProperty("expected_revision").GetInt64());
        Assert.IsFalse(create.RootElement.TryGetProperty("firmware_supported", out _));
        Assert.IsFalse(create.RootElement.TryGetProperty("magnetic", out _));
    }

    [TestMethod]
    public async Task SameSlotProfilesKeepSeparateIdsAcrossRulesPreviewAndFallback()
    {
        var api = new Api(); api.Profiles = [new() { Name = "same", ActivationBackend = "hardware_slot", HardwareSlot = 3 },
            new() { Name = "same", ActivationBackend = "hardware_slot", HardwareSlot = 3, Lighting = new() { ControlOwner = "AuraDirectRgb" } }];
        var model = api.Model(); await model.RefreshAsync(); var a = api.Profiles[0].Id; var b = api.Profiles[1].Id;
        Assert.HasCount(2, model.SlotChoices(3));
        model.UpsertRule(null, "cs2.exe", b, 100, true); model.SetFallback(a); await model.SaveAsync();
        api.Decision.ResolvedProfileId = b; api.Decision.ActivationTarget = b; api.Decision.DecisionKind = "Match";
        api.Decision.ActivationOutcome = "succeeded"; await model.RefreshDecisionAsync();
        Assert.AreEqual(model.TargetChoices(b, false).Single(c => c.Id == b).Label, model.WouldSelect);
        StringAssert.Contains(model.Rules.Single().Description, model.WouldSelect);
        StringAssert.Contains(model.ActivationStatus, model.WouldSelect);
        Assert.AreEqual(a, model.Draft!.FallbackProfileId);
    }

    [TestMethod]
    public async Task MissingTargetNeverRemapsOrSavesAndDraftSurvives()
    {
        var api = new Api(); var model = api.Model(); await model.RefreshAsync();
        var missing = Guid.NewGuid(); model.SetFallback(missing);
        Assert.IsFalse(await model.SaveAsync()); Assert.AreEqual(missing, model.Draft!.FallbackProfileId);
        Assert.IsTrue(model.HasUnsavedChanges); StringAssert.Contains(model.Notice, "已不存在");
        Assert.IsEmpty(api.Calls.Where(c => c.Body.Length > 0));
        Assert.AreEqual("配置文件已不存在", model.TargetChoices(missing, true).Single(c => c.Id == missing).Label);
    }

    [TestMethod]
    public async Task DuplicateNameAndInvalidInputsNeverCreateOrOverwrite()
    {
        var api = new Api(); var model = api.Model(); await model.RefreshAsync();
        foreach (var (name, slot, owner) in new[] { ("desktop", 3, "AuraDirectRgb"), ("", 1, "FirmwareBank"),
            ("x", 0, "FirmwareBank"), ("x", 6, "FirmwareBank"), ("x", 3, "invented"), (new string('中',34), 3, "FirmwareBank") })
            Assert.IsNull(await model.CreateHardwareAsync(name, slot, owner));
        Assert.IsEmpty(api.Calls.Where(c => c.Body.Length > 0)); Assert.HasCount(1, api.Profiles);
        api.Reject = true; Assert.IsNull(await model.CreateHardwareAsync("race duplicate", 3, "FirmwareBank"));
        Assert.IsTrue(model.IsAvailable); Assert.IsTrue(model.CanCreateHardware);
        StringAssert.Contains(model.Notice, "被拒绝"); Assert.HasCount(1, api.Profiles);
    }

    [TestMethod]
    public async Task CreationConflictPreservesDraftAndRequiresExplicitReview()
    {
        var api = new Api { ConflictCreate = true }; var model = api.Model(); await model.RefreshAsync();
        model.UpsertRule(null, "old.exe", api.Profiles[0].Id, 120, true);
        Assert.IsNull(await model.CreateHardwareAsync("new", 3, "AuraDirectRgb"));
        Assert.IsTrue(model.HasConflict); Assert.IsFalse(model.CanSave); Assert.HasCount(1, model.Draft!.Bindings);
        Assert.HasCount(1, api.Calls.Where(c => c.Path.EndsWith("/create")));
        Assert.HasCount(1, api.Profiles); Assert.AreEqual(8L, model.DocumentRevision);
    }

    [TestMethod]
    [DataRow(false)][DataRow(true)]
    public async Task LostOrCancelledCreateResponseIsNotRetriedOrBound(bool cancelled)
    {
        var api = new Api { LoseCreateResponse = !cancelled, CancelCreate = cancelled }; var model = api.Model(); await model.RefreshAsync();
        model.SetFallback(api.Profiles[0].Id);
        Assert.IsNull(await model.CreateHardwareAsync("new", 3, "AuraDirectRgb"));
        Assert.IsFalse(model.IsAvailable); Assert.IsFalse(model.IsBusy); StringAssert.Contains(model.Notice, "未确认");
        Assert.AreEqual(api.Profiles[0].Id, model.Draft!.FallbackProfileId); Assert.IsNull(api.Saved.FallbackProfileId);
        Assert.HasCount(1, api.Calls.Where(c => c.Path.EndsWith("/create")));
        await model.RefreshAsync(); Assert.HasCount(2, model.Profiles); Assert.IsTrue(model.HasConflict);
        Assert.IsNull(await model.CreateHardwareAsync("new", 3, "AuraDirectRgb")); // conflict blocks repeat
        Assert.HasCount(1, api.Calls.Where(c => c.Path.EndsWith("/create")));
    }

    [TestMethod]
    public async Task CreateSucceededThenSaveConflictRetainsNewSelectionAndOriginalRule()
    {
        var api = new Api(); var model = api.Model(); await model.RefreshAsync();
        var id = await model.CreateHardwareAsync("new", 5, "AuraDirectRgb");
        model.UpsertRule(null, "new.exe", id, 10, true); model.SetFallback(id); api.ConflictSave = true;
        Assert.IsFalse(await model.SaveAsync()); Assert.IsTrue(model.HasConflict);
        Assert.AreEqual(id, model.Draft!.Bindings.Single().ProfileId); Assert.AreEqual(id, model.Draft.FallbackProfileId);
        Assert.IsEmpty(api.Saved.Bindings); Assert.IsNull(api.Saved.FallbackProfileId);
        model.KeepDraftAfterConflict(); Assert.IsTrue(await model.SaveAsync());
        Assert.AreEqual(id, api.Saved.Bindings.Single().ProfileId);
    }

    [TestMethod]
    public async Task CreateConflictWithRemoteRuleChangeKeepsDraftAsExplicitlySaveable()
    {
        var api = new Api { ConflictCreate = true }; var model = api.Model(); await model.RefreshAsync();
        api.Saved.FallbackProfileId = api.Profiles[0].Id; // concurrent writer changed automation, not just revision
        Assert.IsNull(await model.CreateHardwareAsync("new", 3, "AuraDirectRgb"));
        Assert.IsTrue(model.HasConflict); Assert.IsNull(model.Draft!.FallbackProfileId);
        model.KeepDraftAfterConflict();
        Assert.IsTrue(model.CanSave); // old local draft differs from newly read persisted config
        Assert.AreEqual(api.Profiles[0].Id, api.Saved.FallbackProfileId); // explicit review never writes
        Assert.IsTrue(await model.SaveAsync()); Assert.IsNull(api.Saved.FallbackProfileId);
    }

    [TestMethod]
    [DataRow(false)][DataRow(true)]
    public async Task InterruptedSaveRetainsSelectionAndReconcilesWithoutAutomaticReplay(bool cancelled)
    {
        var api = new Api(); var model = api.Model(); await model.RefreshAsync();
        var id = await model.CreateHardwareAsync("new", 5, "AuraDirectRgb");
        model.UpsertRule(null, "new.exe", id, 10, true); model.SetFallback(id);
        api.LoseSaveResponse = !cancelled; api.CancelSave = cancelled;
        Assert.IsFalse(await model.SaveAsync()); Assert.IsFalse(model.IsAvailable); Assert.IsFalse(model.IsBusy);
        StringAssert.Contains(model.Notice, "未确认"); Assert.AreEqual(id, model.Draft!.FallbackProfileId);
        Assert.AreEqual(id, model.Draft.Bindings.Single().ProfileId);
        Assert.HasCount(1, api.Calls.Where(c => c.Path.EndsWith("/automation") && c.Body.Length > 0));
        await model.RefreshAsync(); Assert.IsTrue(model.HasConflict);
        model.KeepDraftAfterConflict(); Assert.IsFalse(model.HasUnsavedChanges); // persisted response resolves ambiguity
        Assert.IsFalse(await model.SaveAsync()); // never silently replay an acknowledged snapshot
        Assert.HasCount(1, api.Calls.Where(c => c.Path.EndsWith("/automation") && c.Body.Length > 0));
    }

    [TestMethod]
    public async Task OldDaemonCapabilitiesBlockCreateButAllowExistingProfileTargets()
    {
        foreach (var lighting in new[] { false, true }) {
            var api = new Api { Capability = false, LightingCapability = lighting }; var model = api.Model(); await model.RefreshAsync();
            Assert.IsFalse(model.CanCreateHardware); Assert.IsNull(await model.CreateHardwareAsync("new", 3, "AuraDirectRgb"));
            StringAssert.Contains(model.Notice, "后台不支持");
            model.SetFallback(api.Profiles[0].Id); Assert.IsTrue(await model.SaveAsync());
            Assert.IsFalse(api.Calls.Any(c => c.Path.EndsWith("/create")));
        }
        var unsupported = new Api { LightingCapability = false }; var m = unsupported.Model(); await m.RefreshAsync(); Assert.IsFalse(m.CanCreateHardware);
    }

    [TestMethod]
    public async Task OldDaemonCannotSaveIndependentLightingTarget()
    {
        var api = new Api { LightingCapability = false };
        api.Profiles.Add(new() { Name = "newer data", ActivationBackend = "hardware_slot", HardwareSlot = 3,
            Lighting = new() { ControlOwner = "AuraDirectRgb" } });
        var model = api.Model(); await model.RefreshAsync(); model.SetFallback(api.Profiles[1].Id);
        Assert.IsFalse(await model.SaveAsync()); Assert.AreEqual(api.Profiles[1].Id, model.Draft!.FallbackProfileId);
        StringAssert.Contains(model.Notice, "未声明独立灯光"); Assert.IsEmpty(api.Calls.Where(c => c.Body.Length > 0));
    }

    [TestMethod]
    public async Task UnexpectedCreatedProfileDoesNotFakeHardwareBinding()
    {
        var api = new Api { WrongProfile = true }; var model = api.Model(); await model.RefreshAsync();
        Assert.IsNull(await model.CreateHardwareAsync("new", 3, "AuraDirectRgb"));
        Assert.IsFalse(model.IsAvailable); Assert.IsEmpty(model.Draft!.Bindings); Assert.IsNull(model.Draft.FallbackProfileId);
        StringAssert.Contains(model.Notice, "未确认");
    }

    [TestMethod]
    public async Task EditCancelReopenDeleteAndRapidStatusRefreshKeepDraftAndManualHold()
    {
        var api = new Api(); var model = api.Model(); await model.RefreshAsync();
        var created = await model.CreateHardwareAsync("slot3", 3, "AuraDirectRgb");
        // Cancelling a UI editor never invokes UpsertRule or SetFallback. Creation remains a separate durable result.
        Assert.IsEmpty(model.Draft!.Bindings); Assert.HasCount(2, model.Profiles);
        model.UpsertRule(null, "game.exe", created, 100, true); var rule = model.Draft.Bindings.Single().RuleId;
        await model.SaveAsync(); model.UpsertRule(rule, "game.exe", created, 150, false);
        for (int i = 0; i < 20; i++) { api.Decision.ManualHold = true; await model.RefreshDecisionAsync(); await model.RefreshAsync(); }
        Assert.AreEqual(created, model.Draft.Bindings.Single().ProfileId); Assert.AreEqual(150, model.Draft.Bindings.Single().Priority);
        Assert.IsFalse(model.Draft.Bindings.Single().Enabled); Assert.IsFalse(model.HasConflict);
        StringAssert.Contains(model.ActivationStatus, "手动选择");
        model.DiscardDraft(); Assert.AreEqual(100, model.Draft.Bindings.Single().Priority);
        model.DeleteRule(rule); Assert.IsTrue(await model.SaveAsync()); Assert.IsEmpty(api.Saved.Bindings);
        await model.RefreshAsync(); Assert.IsEmpty(model.Draft.Bindings);
        Assert.IsFalse(api.Calls.Any(c => c.Path.EndsWith("/activate")));
    }
}
