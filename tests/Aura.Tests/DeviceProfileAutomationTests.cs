using System.Net;
using System.Text;
using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class DeviceProfileAutomationTests
{
    private sealed class Daemon : HttpMessageHandler
    {
        public static readonly Guid A = Guid.Parse("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
        public static readonly Guid B = Guid.Parse("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
        public List<DeviceProfile> Profiles = [new() { Id = A, Name = "Desktop" }, new() { Id = B, Name = "CS2" }];
        public DeviceProfileAutomationConfig Config = new() { Enabled = true };
        public DeviceProfileAutomationDecision Decision = new() {
            Enabled = true, ForegroundProcess = "cs2.exe", DecisionKind = "NoDecision", DecisionReason = "NoMatchingRule" };
        public long Revision = 4;
        public bool Offline, ConflictNext, Unsupported, UnsupportedNull;
        public List<(string Path, string Body)> Calls = [];
        public List<long> Revisions = [];
        public TaskCompletionSource? Gate;
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        {
            if (Offline) throw new HttpRequestException("offline");
            var path = request.RequestUri!.AbsolutePath;
            var body = request.Content is null ? "" : await request.Content.ReadAsStringAsync(token);
            Calls.Add((path, body));
            if (Gate is not null) await Gate.Task.WaitAsync(token);
            if (request.Method == HttpMethod.Post) {
                Assert.AreEqual("/api/device-profiles/automation", path);
                using var parsed = JsonDocument.Parse(body);
                var expected = parsed.RootElement.GetProperty("expected_revision").GetInt64(); Revisions.Add(expected);
                if (ConflictNext) { Revision++; ConflictNext = false; }
                if (expected != Revision) return Reply("{\"status\":\"error\",\"error\":\"revision conflict\"}", HttpStatusCode.Conflict);
                Config = parsed.RootElement.GetProperty("device_profile_automation").Deserialize<DeviceProfileAutomationConfig>(ProfileJson.Options)!;
                Revision++;
            }
            // A server contract fixture; target evaluation is tested in native
            // engine tests, never reproduced as authoritative client logic.
            return Reply(JsonSerializer.Serialize(new {
                status = "ok", api_version = 1, document_revision = Revision,
                profiles = Profiles, device_profile_automation = UnsupportedNull ? (JsonElement?)null : Unsupported ?
                    JsonSerializer.SerializeToElement(new { schema_version = 999, private_future = "retain" }) : JsonSerializer.SerializeToElement(Config, ProfileJson.Options),
                automation_configuration_available = !Unsupported, automation_decision = Decision
            }, ProfileJson.Options));
        }
        private static HttpResponseMessage Reply(string json, HttpStatusCode status = HttpStatusCode.OK) => new(status) {
            Content = new StringContent(json, Encoding.UTF8, "application/json") };
        public DeviceProfileAutomationModel Model() => new(new ProfileControlClient(new HttpClient(this)));
    }

    [TestMethod]
    public async Task CrudNormalizationGuidAndFallbackUseOnlyRevisionedAutomationApi()
    {
        var server = new Daemon(); var model = server.Model(); await model.RefreshAsync();
        Assert.IsTrue(model.IsAvailable); Assert.IsFalse(model.CanSave);
        model.UpsertRule(null, @"C:\Program Files\Steam\CS2.EXE", Daemon.B, 100, true);
        var id = model.Draft!.Bindings.Single().RuleId;
        Assert.AreEqual("cs2.exe", model.Draft.Bindings.Single().ProcessName);
        Assert.AreNotEqual(Guid.Empty, id);
        model.SetFallback(Daemon.A); Assert.IsTrue(await model.SaveAsync());
        Assert.AreEqual(Daemon.B, server.Config.Bindings.Single().ProfileId);
        Assert.AreEqual(Daemon.A, server.Config.FallbackProfileId);
        server.Profiles[1].Name = "Renamed CS2"; server.Revision++; await model.RefreshAsync();
        Assert.AreEqual(Daemon.B, model.Draft.Bindings.Single().ProfileId);
        StringAssert.Contains(model.Rules.Single().Description, "Renamed CS2");
        model.UpsertRule(id, "cs2.exe", Daemon.B, 10, false);
        model.SetEnabled(false); Assert.IsTrue(await model.SaveAsync());
        Assert.IsFalse(server.Config.Enabled); Assert.IsFalse(server.Config.Bindings.Single().Enabled);
        Assert.AreEqual(10, server.Config.Bindings.Single().Priority);
        model.DeleteRule(id); model.SetFallback(null); Assert.IsTrue(await model.SaveAsync());
        Assert.IsEmpty(server.Config.Bindings); Assert.IsNull(server.Config.FallbackProfileId);
        Assert.IsTrue(server.Calls.All(c => c.Path == "/api/device-profiles/automation"));
        Assert.IsFalse(model.HasUnsavedChanges);
    }

    [TestMethod]
    public async Task InvalidInputNeverStagesRuleOrWritesAndMissingTargetNeverRetargets()
    {
        var server = new Daemon(); var model = server.Model(); await model.RefreshAsync();
        foreach (var process in new[] { "", " ", "bad?.exe", "C:\\folder\\", "a.exe.", "\ud800" })
            Assert.ThrowsExactly<InvalidDataException>(() => model.UpsertRule(null, process, Daemon.B, 100, true));
        foreach (var priority in new[] { double.NaN, 1.5, double.PositiveInfinity, (double)int.MaxValue + 1 })
            Assert.ThrowsExactly<InvalidDataException>(() => model.UpsertRule(null, "cs2.exe", Daemon.B, priority, true));
        Assert.ThrowsExactly<InvalidDataException>(() => model.UpsertRule(null, "cs2.exe", null, 100, true));
        Assert.IsEmpty(model.Draft!.Bindings); Assert.HasCount(1, server.Calls);
        model.UpsertRule(null, "cs2.exe", Daemon.B, 100, true); model.SetFallback(Daemon.B); await model.SaveAsync();
        server.Profiles.RemoveAll(p => p.Id == Daemon.B); server.Revision++;
        server.Decision = new() { Enabled = true, ForegroundProcess = "cs2.exe", ResolvedProfileId = Daemon.B,
            DecisionKind = "InvalidDecision", DecisionReason = "InvalidTargetProfile" };
        await model.RefreshAsync();
        Assert.AreEqual(Daemon.B, model.Draft.FallbackProfileId);
        Assert.AreEqual(Daemon.B, model.Draft.Bindings.Single().ProfileId);
        Assert.AreEqual("配置文件已不存在", model.WouldSelect);
        StringAssert.Contains(model.PreviewStatus, "已不存在");
        Assert.AreEqual("配置文件已不存在", model.TargetChoices(Daemon.B, true).Single(c => c.Id == Daemon.B).Label);
    }

    [TestMethod]
    public async Task ConflictReloadRetainsDraftRequiresExplicitChoiceAndConsumesReturnedRevision()
    {
        var server = new Daemon(); var model = server.Model(); await model.RefreshAsync();
        model.UpsertRule(null, "cs2.exe", Daemon.B, 100, true); server.ConflictNext = true;
        Assert.IsFalse(await model.SaveAsync()); Assert.IsTrue(model.HasConflict);
        Assert.HasCount(1, model.Draft!.Bindings); Assert.IsFalse(model.CanSave);
        Assert.AreEqual(5, model.DocumentRevision);
        model.KeepDraftAfterConflict(); Assert.IsTrue(await model.SaveAsync());
        CollectionAssert.AreEqual(new long[] { 4, 5 }, server.Revisions);
        Assert.AreEqual(6, model.DocumentRevision);
        model.SetEnabled(false); server.Config.FallbackProfileId = Daemon.A; server.Revision++;
        await model.RefreshAsync(); Assert.IsTrue(model.HasConflict);
        model.DiscardDraft(); Assert.IsFalse(model.HasUnsavedChanges);
        Assert.AreEqual(Daemon.A, model.Draft.FallbackProfileId);
    }

    [TestMethod]
    public async Task ExtensionsSurviveAtomicSaveAndPreviewIsServerDecisionNotDraftOrActive()
    {
        var server = new Daemon(); using var json = JsonDocument.Parse("{\"future\":true}");
        server.Config.Extensions = new() { ["future_section"] = json.RootElement.Clone() };
        var model = server.Model(); await model.RefreshAsync();
        model.UpsertRule(null, "cs2.exe", Daemon.B, 100, true);
        model.Draft!.Bindings[0].Extensions = new() { ["future_rule"] = json.RootElement.Clone() };
        Assert.AreEqual("不选择配置文件", model.WouldSelect); // draft never evaluates locally
        server.Decision = new() { Enabled = true, ForegroundProcess = "cs2.exe", ResolvedProfileId = Daemon.B,
            DecisionKind = "Match", DecisionReason = "MatchedRule", MatchedRuleId = model.Draft.Bindings[0].RuleId };
        await model.SaveAsync(); Assert.AreEqual("CS2", model.WouldSelect);
        Assert.AreEqual("cs2.exe", model.MatchedRule);
        Assert.DoesNotContain("已应用", model.PreviewStatus);
        Assert.IsFalse(model.Decision!.HardwareActivationAllowed);
        Assert.IsTrue(server.Config.Extensions!.ContainsKey("future_section"));
        Assert.IsTrue(server.Config.Bindings[0].Extensions!.ContainsKey("future_rule"));
        Assert.IsTrue(server.Calls.All(c => c.Path == "/api/device-profiles/automation"));
    }

    [TestMethod]
    public async Task DecisionStatesOfflineRecoveryAndUnknownSectionStayHonest()
    {
        var server = new Daemon(); var model = server.Model(); await model.RefreshAsync();
        foreach (var (decision, expected) in new[] {
            (new DeviceProfileAutomationDecision { Enabled = false }, "已关闭"),
            (new DeviceProfileAutomationDecision { Enabled = true, DebouncePending = true }, "稳定"),
            (new DeviceProfileAutomationDecision { Enabled = true, ManualHold = true, DecisionReason = "ManualHold" }, "已暂停"),
            (new DeviceProfileAutomationDecision { Enabled = true, DecisionReason = "NoMatchingRule" }, "没有匹配"),
            (new DeviceProfileAutomationDecision { Enabled = true, DecisionKind = "Fallback", ResolvedProfileId = Daemon.A }, "Desktop") }) {
            server.Decision = decision; await model.RefreshAsync(); StringAssert.Contains(model.PreviewStatus, expected);
        }
        model.SetEnabled(false); server.Offline = true; await model.RefreshAsync();
        Assert.IsFalse(model.CanSave); Assert.IsNull(model.Decision); StringAssert.Contains(model.PreviewStatus, "无法读取");
        server.Offline = false; await model.RefreshAsync(); Assert.IsTrue(model.HasUnsavedChanges); Assert.IsTrue(model.CanSave);
        server.Unsupported = true; server.Decision.ConfigurationAvailable = false; await model.RefreshAsync();
        Assert.IsFalse(model.CanSave); StringAssert.Contains(model.Notice, "原数据已保留");
        Assert.IsFalse(await model.SaveAsync());
        server.UnsupportedNull = true; await model.RefreshAsync();
        Assert.IsFalse(model.CanSave); StringAssert.Contains(model.Notice, "仍可手动管理");
    }

    [TestMethod]
    public async Task LoadingAndSavingDisableCommandsAndCoalescePreviewRequests()
    {
        var server = new Daemon { Gate = new(TaskCreationOptions.RunContinuationsAsynchronously) };
        var model = server.Model(); var load = model.RefreshAsync();
        Assert.IsTrue(model.IsBusy); Assert.IsFalse(model.CanSave);
        await model.RefreshAsync(); Assert.HasCount(1, server.Calls);
        server.Gate.SetResult(); await load; server.Gate = null;
        model.SetEnabled(false); server.Gate = new(TaskCreationOptions.RunContinuationsAsynchronously);
        var save = model.SaveAsync(); Assert.IsTrue(model.IsBusy); Assert.IsFalse(model.CanSave);
        server.Gate.SetResult(); await save; Assert.IsFalse(model.HasUnsavedChanges);
    }

    [TestMethod]
    public async Task Phase4BCoordinatorResponsesAreAcceptedWithoutClientActivation()
    {
        var server = new Daemon(); server.Decision.HardwareActivationAllowed = true;
        var model = server.Model(); await model.RefreshAsync(); Assert.IsTrue(model.IsAvailable);
        server.Decision.DecisionKind = "Match"; server.Decision.ResolvedProfileId = Daemon.B;
        server.Decision.ActivationTarget = Daemon.B;
        foreach (var (state, outcome, fragment) in new[] {
            ("Activating", (string?)null, "正在应用"), ("Deferred", "deferred", "等待键盘"),
            ("Idle", "succeeded", "已提交"), ("Idle", "no-op", "已提交"), ("Blocked", "failed", "失败") }) {
            server.Decision.CoordinatorState = state; server.Decision.ActivationOutcome = outcome;
            await model.RefreshDecisionAsync(); StringAssert.Contains(model.ActivationStatus, fragment);
        }
        Assert.IsTrue(server.Calls.All(c => c.Path is "/api/device-profiles/automation" or "/api/device-profiles/automation/status"));
        Assert.IsTrue(server.Calls.All(c => c.Body.Length == 0));
        Assert.DoesNotContain("已应用", model.PreviewStatus);
    }

    [TestMethod]
    public async Task Phase4BStatusRevisionNeverSilentlyOverwritesDraft()
    {
        var server = new Daemon(); server.Decision.HardwareActivationAllowed = true;
        var model = server.Model(); await model.RefreshAsync(); var revision = model.DocumentRevision;
        model.UpsertRule(null, "cs2.exe", Daemon.B, 100, true);
        server.Decision.ConfigurationDocumentRevision = revision + 1;
        server.Decision.CoordinatorState = "Activating";
        await model.RefreshDecisionAsync();
        Assert.AreEqual(revision, model.DocumentRevision); Assert.IsTrue(model.HasUnsavedChanges);
        Assert.IsTrue(model.NeedsConfigurationRefresh); Assert.HasCount(1, model.Draft!.Bindings);
        server.Revision++; await model.RefreshAsync();
        Assert.IsTrue(model.HasConflict); Assert.HasCount(1, model.Draft!.Bindings);
        Assert.IsFalse(await model.SaveAsync());
    }

    [TestMethod]
    public async Task Phase4BManualHoldAndSafetyAreNotDisplayedAsSuccess()
    {
        var server = new Daemon(); server.Decision.HardwareActivationAllowed = true;
        server.Decision.DecisionKind = "Match"; server.Decision.ActivationOutcome = "succeeded";
        var model = server.Model(); await model.RefreshAsync();
        server.Decision.ManualHold = true; await model.RefreshDecisionAsync(); StringAssert.Contains(model.ActivationStatus, "手动选择");
        server.Decision.ManualHold = false; server.Decision.CoordinatorState = "Blocked";
        server.Decision.ActivationOutcome = "blocked"; server.Decision.HardwareBlockReasons = ["SafetyQuarantined"];
        await model.RefreshDecisionAsync(); StringAssert.Contains(model.ActivationStatus, "安全隔离");
        server.Decision.HardwareBlockReasons = ["M605Unhealthy"];
        await model.RefreshDecisionAsync(); StringAssert.Contains(model.ActivationStatus, "尚未恢复");
        server.Decision.Enabled = false; await model.RefreshDecisionAsync(); StringAssert.Contains(model.ActivationStatus, "保持不变");
        server.Offline = true; await model.RefreshDecisionAsync(); StringAssert.Contains(model.ActivationStatus, "无法读取");
    }
}
