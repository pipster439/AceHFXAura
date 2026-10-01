using System.Net;
using System.Text;
using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class ProfileControlClientTests
{
    [TestMethod]
    public async Task HardwareGateIsTypedCachedGetOnly()
    {
        var handler = new Capture { ReplyOverride = "{\"status\":\"ok\",\"api_version\":1,\"hardware_rt_gate\":{\"state\":\"off\",\"observation_sequence\":12,\"input_session_generation\":2}}" };
        var client = new ProfileControlClient(new HttpClient(handler));
        var result = await client.GetHardwareRtGateAsync();
        Assert.AreEqual("off", result.HardwareRtGate!.State);
        Assert.AreEqual(12UL, result.HardwareRtGate.ObservationSequence);
        Assert.AreEqual(2UL, result.HardwareRtGate.InputSessionGeneration);
        Assert.AreEqual("/api/device-profiles/hardware-rt-gate", handler.Calls.Single().Path);
        Assert.AreEqual("", handler.Calls.Single().Body);
        Assert.AreEqual(HttpMethod.Get, handler.Methods.Single());
    }
    private sealed class Capture : HttpMessageHandler
    {
        public readonly List<(string Path, string Body)> Calls = [];
        public readonly List<HttpMethod> Methods = [];
        public bool Conflict;
        public bool Offline;
        public bool ApplyFailure;
        public string? ReplyOverride;
        public HttpStatusCode? ReplyStatus;
        public string ReplyMediaType = "application/json";
        public TaskCompletionSource? DelayReply;
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,
            CancellationToken cancellationToken)
        {
            if (Offline) throw new HttpRequestException("mock daemon stopped");
            Methods.Add(request.Method);
            Calls.Add((request.RequestUri!.AbsolutePath,
                request.Content is null ? "" : await request.Content.ReadAsStringAsync(cancellationToken)));
            if (DelayReply is not null) await DelayReply.Task.WaitAsync(cancellationToken);
            if (Conflict) return new HttpResponseMessage(HttpStatusCode.Conflict) {
                Content = new StringContent("{\"status\":\"error\",\"error\":\"revision conflict\"}",
                    Encoding.UTF8, "application/json") };
            if (ApplyFailure) return new HttpResponseMessage(HttpStatusCode.Conflict) {
                Content = new StringContent("{\"status\":\"ok\",\"api_version\":1," +
                    "\"outcome\":\"failed\",\"dirty\":true,\"error\":\"mock operation failed\"," +
                    "\"operations\":[{\"identity\":\"KeyActuation:1026\",\"kind\":\"KeyActuation\"," +
                    "\"logical_id\":1026,\"succeeded\":false,\"error\":\"mock failure\"}]}",
                    Encoding.UTF8, "application/json") };
            var response = "{\"status\":\"ok\",\"api_version\":1,\"document_revision\":3," +
                "\"selected_profile_id\":\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\"," +
                "\"active_profile_id\":null,\"dirty\":true,\"mutation_revision\":7," +
                "\"m605_session_generation\":2,\"profiles\":[]}";
            return new HttpResponseMessage(ReplyStatus ?? HttpStatusCode.OK) {
                Content = new StringContent(ReplyOverride ?? response, Encoding.UTF8, ReplyMediaType) };
        }
    }

    [TestMethod]
    public async Task AllProfileActionsUseDaemonApiAndExpectedRevision()
    {
        var capture = new Capture();
        var client = new ProfileControlClient(new HttpClient(capture));
        var id = Guid.Parse("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
        var state = await client.ListAsync();
        Assert.AreEqual(3, state.DocumentRevision);
        Assert.AreEqual(7ul, state.MutationRevision);
        Assert.AreEqual(2ul, state.M605SessionGeneration);
        await client.GetAsync(id);
        await client.GetRuntimeAsync();
        await client.CreateAsync("CS2", 3);
        await client.DuplicateAsync(id, "Copy", 4);
        await client.RenameAsync(id, "Game", 5);
        await client.UpdateAsync(new DeviceProfile { Id = id, Name = "Changed" }, 6);
        await client.DeleteAsync(id, 7);
        await client.UpdateDefaultsAsync(new ProfileMagnetic(), 8);
        await client.ActivateAsync(id, ProfileActivationReason.Manual, 9);
        CollectionAssert.AreEqual(new[] { "/api/device-profiles", $"/api/device-profiles/{id:D}",
            "/api/device-profiles/runtime", "/api/device-profiles/create", "/api/device-profiles/duplicate",
            "/api/device-profiles/rename", "/api/device-profiles/update", "/api/device-profiles/delete",
            "/api/device-profiles/defaults", "/api/device-profiles/activate" },
            capture.Calls.Select(c => c.Path).ToArray());
        using var create = JsonDocument.Parse(capture.Calls[3].Body);
        Assert.AreEqual(3, create.RootElement.GetProperty("expected_revision").GetInt64());
        using var activation = JsonDocument.Parse(capture.Calls[^1].Body);
        Assert.AreEqual("Manual", activation.RootElement.GetProperty("reason").GetString());
        Assert.AreEqual(9, activation.RootElement.GetProperty("expected_revision").GetInt64());
        Assert.AreEqual(id, activation.RootElement.GetProperty("profile_id").GetGuid());
    }

    [TestMethod]
    public async Task UnavailableDaemonNeverFallsBackToLocalProfileFile()
    {
        var capture = new Capture { Offline = true };
        var client = new ProfileControlClient(new HttpClient(capture));
        var error = await Assert.ThrowsExactlyAsync<ProfileApiException>(() => client.CreateAsync("Offline", 0));
        Assert.IsTrue(error.DaemonUnavailable);
        capture.Offline = false;
        Assert.AreEqual(3, (await client.ListAsync()).DocumentRevision);
        capture.Conflict = true;
        error = await Assert.ThrowsExactlyAsync<ProfileApiException>(() => client.RenameAsync(Guid.NewGuid(), "Other", 0));
        Assert.IsTrue(error.RevisionConflict);
        capture.Conflict = false;
        capture.ApplyFailure = true;
        var failed = await client.ActivateAsync(Guid.NewGuid(), ProfileActivationReason.Manual, 0);
        Assert.AreEqual("failed", failed.Outcome);
        Assert.IsTrue(failed.Dirty);
        Assert.AreEqual("KeyActuation:1026", failed.Operations!.Single().Identity);
    }

    [TestMethod]
    public async Task RuntimeBaselineFieldsDeserializeIndependentlyOfAppliedSession()
    {
        var capture = new Capture { ReplyOverride = "{\"status\":\"ok\",\"api_version\":1," +
            "\"global_defaults\":{\"global_actuation_mm\":1.0,\"global_deadzone\":null}," +
            "\"effective_global_defaults\":{\"global_actuation_mm\":1.0," +
            "\"global_deadzone\":{\"top_mm\":0.0,\"bottom_mm\":0.1}}}" };
        var runtime = await new ProfileControlClient(new HttpClient(capture)).GetRuntimeAsync();
        Assert.AreEqual(1.0, runtime.GlobalDefaults!.GlobalActuationMm);
        Assert.IsNull(runtime.GlobalDefaults.GlobalDeadzone);
        Assert.AreEqual(0.1, runtime.EffectiveGlobalDefaults!.GlobalDeadzone!.BottomMm);
        Assert.AreEqual("/api/device-profiles/runtime", capture.Calls.Single().Path);
    }

    [TestMethod]
    public async Task DiagnosticExportUsesOnlyGetAndPreservesDaemonSnapshot()
    {
        var capture = new Capture { ReplyOverride = "{\"status\":\"ok\",\"api_version\":1," +
            "\"diagnostics\":{\"diagnostic_schema_version\":1,\"runtime\":{\"dirty\":true}," +
            "\"m605\":{\"transport\":{\"connected_now\":null},\"persistent_safety_quarantine\":true}," +
            "\"future_field\":{\"preserved\":true}}}" };
        var model = new ProfileDiagnosticsModel(new ProfileControlClient(new HttpClient(capture)));
        await model.LoadAsync();
        Assert.AreEqual("", model.Error);
        Assert.IsFalse(model.IsBusy);
        using var document = JsonDocument.Parse(model.Json!);
        Assert.IsTrue(document.RootElement.GetProperty("m605").GetProperty("persistent_safety_quarantine").GetBoolean());
        Assert.AreEqual(JsonValueKind.Null, document.RootElement.GetProperty("m605").GetProperty("transport").GetProperty("connected_now").ValueKind);
        Assert.IsTrue(document.RootElement.GetProperty("future_field").GetProperty("preserved").GetBoolean());
        Assert.AreEqual("/api/device-profiles/diagnostics", capture.Calls.Single().Path);
        Assert.AreEqual("", capture.Calls.Single().Body);
        Assert.AreEqual(HttpMethod.Get, capture.Methods.Single());
    }

    [TestMethod]
    public async Task DiagnosticUnavailableIncompatibleAndRecoveryNeverExportStaleJson()
    {
        var capture = new Capture { ReplyOverride = "{\"status\":\"ok\",\"api_version\":1," +
            "\"diagnostics\":{\"diagnostic_schema_version\":1}}" };
        var model = new ProfileDiagnosticsModel(new ProfileControlClient(new HttpClient(capture)));
        await model.LoadAsync(); Assert.IsNotNull(model.Json);
        capture.Offline = true;
        await model.LoadAsync(); Assert.IsNull(model.Json);
        StringAssert.Contains(model.Error, "后台服务不可用");
        capture.Offline = false; capture.ReplyStatus = HttpStatusCode.NotFound; capture.ReplyOverride = "";
        await model.LoadAsync(); Assert.IsNull(model.Json);
        StringAssert.Contains(model.Error, "不支持诊断导出");
        capture.ReplyStatus = HttpStatusCode.OK;
        foreach (var payload in new[] { "{bad", "{\"status\":\"ok\",\"api_version\":1}",
            "{\"status\":\"ok\",\"api_version\":1,\"diagnostics\":{\"diagnostic_schema_version\":2}}",
            "{\"status\":\"ok\",\"api_version\":1,\"diagnostics\":{\"diagnostic_schema_version\":\"bad\"}}" }) {
            capture.ReplyOverride = payload; await model.LoadAsync();
            Assert.IsNull(model.Json); StringAssert.Contains(model.Error, "响应异常");
            Assert.DoesNotContain("JsonException", model.Error);
        }
        capture.ReplyOverride = "{\"status\":\"ok\",\"api_version\":1,\"diagnostics\":{\"diagnostic_schema_version\":1}}";
        await model.LoadAsync(); Assert.IsNotNull(model.Json); Assert.AreEqual("", model.Error);
    }

    [TestMethod]
    public async Task DiagnosticCancellationIsPropagatedAndClearsBusy()
    {
        using var token = new CancellationTokenSource(); token.Cancel();
        var model = new ProfileDiagnosticsModel(new ProfileControlClient(new HttpClient(new Capture())));
        await Assert.ThrowsAsync<OperationCanceledException>(() => model.LoadAsync(token.Token));
        Assert.IsFalse(model.IsBusy); Assert.IsNull(model.Json);
    }

    [TestMethod]
    public async Task DiagnosticLoadingSuppressesDuplicateReads()
    {
        var capture = new Capture { DelayReply = new(TaskCreationOptions.RunContinuationsAsynchronously),
            ReplyOverride = "{\"status\":\"ok\",\"api_version\":1,\"diagnostics\":{\"diagnostic_schema_version\":1}}" };
        var model = new ProfileDiagnosticsModel(new ProfileControlClient(new HttpClient(capture)));
        var loading = model.LoadAsync();
        Assert.IsTrue(model.IsBusy); Assert.IsNull(model.Json);
        await model.LoadAsync();
        Assert.HasCount(1, capture.Calls);
        capture.DelayReply.SetResult(); await loading;
        Assert.IsFalse(model.IsBusy); Assert.IsNotNull(model.Json);
    }

    [TestMethod]
    public void DiagnosticExportRejectsLiveConfigurationFileNames()
    {
        Assert.IsFalse(ProfileDiagnosticsModel.CanExportFileName("device-profiles.json"));
        Assert.IsFalse(ProfileDiagnosticsModel.CanExportFileName("DEVICE-PROFILES.JSON"));
        Assert.IsFalse(ProfileDiagnosticsModel.CanExportFileName("config.json"));
        Assert.IsTrue(ProfileDiagnosticsModel.CanExportFileName("Aura-diagnostics-20260930.json"));
    }

    [TestMethod]
    public async Task DiagnosticExportPreservesAdditiveDecisionWithoutChangingRuntimeOrWriting()
    {
        var capture = new Capture { ReplyOverride = "{\"status\":\"ok\",\"api_version\":1," +
            "\"diagnostics\":{\"diagnostic_schema_version\":1," +
            "\"runtime\":{\"selected_profile_id\":\"desktop\",\"active_profile_id\":null,\"dirty\":true}," +
            "\"automation\":{\"schema_version\":1,\"resolved_profile_id\":\"cs2\"," +
            "\"decision_kind\":\"Match\",\"hardware_activation_allowed\":false," +
            "\"hardware_block_reason\":\"PhaseHardwareActivationDisabled\"}}}" };
        var model = new ProfileDiagnosticsModel(new ProfileControlClient(new HttpClient(capture)));
        await model.LoadAsync();
        Assert.IsNotNull(model.Json);
        using var export = JsonDocument.Parse(model.Json);
        Assert.AreEqual("desktop", export.RootElement.GetProperty("runtime").GetProperty("selected_profile_id").GetString());
        Assert.AreEqual("cs2", export.RootElement.GetProperty("automation").GetProperty("resolved_profile_id").GetString());
        Assert.IsFalse(export.RootElement.GetProperty("automation").GetProperty("hardware_activation_allowed").GetBoolean());
        Assert.HasCount(1, capture.Calls);
        Assert.AreEqual(HttpMethod.Get, capture.Methods.Single());
        Assert.AreEqual("/api/device-profiles/diagnostics", capture.Calls.Single().Path);
    }

    [TestMethod]
    public async Task UnknownProfileFieldsSurviveClientReadAndUpdate()
    {
        var id = Guid.NewGuid();
        var profile = new DeviceProfile { Id = id, Name = "Extended" };
        using var value = JsonDocument.Parse("{\"future\":true}");
        profile.Extensions = new Dictionary<string, JsonElement> { ["profile_extension"] = value.RootElement.Clone() };
        var capture = new Capture { ReplyOverride = "{\"status\":\"ok\",\"api_version\":1,\"profile\":" +
            JsonSerializer.Serialize(profile, ProfileJson.Options) + "}" };
        var client = new ProfileControlClient(new HttpClient(capture));
        var loaded = (await client.GetAsync(id)).Profile!;
        Assert.IsTrue(loaded.Extensions!.ContainsKey("profile_extension"));
        await client.UpdateAsync(loaded, 0);
        using var body = JsonDocument.Parse(capture.Calls.Last().Body);
        Assert.IsTrue(body.RootElement.GetProperty("profile").GetProperty("profile_extension")
            .GetProperty("future").GetBoolean());
    }

    [TestMethod]
    public async Task EmptyAndNonProfile404IdentifyIncompatibleDaemonWithoutJsonException()
    {
        foreach (var (body, mediaType) in new[] {
            ("", "application/json"), ("<html>Not Found</html>", "text/html"),
            ("Not Found", "text/plain"), ("{\"message\":\"Not Found\"}", "application/json"),
            ("{\"status\":\"error\",\"error\":\"Not Found\"}", "application/json") }) {
            var capture = new Capture { ReplyStatus = HttpStatusCode.NotFound,
                ReplyOverride = body, ReplyMediaType = mediaType };
            var error = await Assert.ThrowsExactlyAsync<ProfileApiException>(() =>
                new ProfileControlClient(new HttpClient(capture)).ListAsync());
            Assert.AreEqual(HttpStatusCode.NotFound, error.StatusCode);
            Assert.IsTrue(error.IncompatibleDaemon);
            Assert.IsFalse(error.DaemonUnavailable);
            Assert.DoesNotContain("JsonException", error.Message);
        }
    }

    [TestMethod]
    public async Task Empty500AndEmpty200HaveProtocolErrorsWithHttpStatus()
    {
        foreach (var status in new[] { HttpStatusCode.InternalServerError, HttpStatusCode.OK,
            HttpStatusCode.Conflict }) {
            var capture = new Capture { ReplyStatus = status, ReplyOverride = "" };
            var error = await Assert.ThrowsExactlyAsync<ProfileApiException>(() =>
                new ProfileControlClient(new HttpClient(capture)).ListAsync());
            Assert.AreEqual(status, error.StatusCode);
            Assert.AreEqual(ProfileApiErrorCategory.InvalidResponse, error.Category);
            Assert.IsFalse(error.RevisionConflict);
            StringAssert.Contains(error.Message, $"HTTP {(int)status} with an empty response");
        }
    }

    [TestMethod]
    public async Task TypedMissingProfileIsNotMistakenForMissingProfileApi()
    {
        var capture = new Capture { ReplyStatus = HttpStatusCode.NotFound,
            ReplyOverride = "{\"status\":\"error\",\"error\":\"Profile not found\"}" };
        var error = await Assert.ThrowsExactlyAsync<ProfileApiException>(() =>
            new ProfileControlClient(new HttpClient(capture)).GetAsync(Guid.NewGuid()));
        Assert.AreEqual(HttpStatusCode.NotFound, error.StatusCode);
        Assert.IsFalse(error.IncompatibleDaemon);
    }

    [TestMethod]
    public async Task MalformedJsonAndWrongContentTypeAreBoundedProtocolErrors()
    {
        foreach (var (body, mediaType) in new[] { ("{bad", "application/json"),
            (new string('x', 1000), "text/plain") }) {
            var capture = new Capture { ReplyOverride = body, ReplyMediaType = mediaType };
            var error = await Assert.ThrowsExactlyAsync<ProfileApiException>(() =>
                new ProfileControlClient(new HttpClient(capture)).ListAsync());
            Assert.AreEqual(HttpStatusCode.OK, error.StatusCode);
            Assert.AreEqual(ProfileApiErrorCategory.InvalidResponse, error.Category);
            StringAssert.Contains(error.Message, "invalid response");
            Assert.IsLessThanOrEqualTo(200, error.ResponsePreview!.Length);
            Assert.DoesNotContain("JsonException", error.Message);
        }
    }
}
