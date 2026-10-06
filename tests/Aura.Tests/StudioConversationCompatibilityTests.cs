using System.Net;
using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public class StudioConversationCompatibilityTests
{
    private sealed class Handler(Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> send) : HttpMessageHandler {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token) => send(request, token);
    }
    private static JsonElement Element(string text) { using var doc = JsonDocument.Parse(text); return doc.RootElement.Clone(); }
    private static readonly StudioLlmSettings Settings = new("http://127.0.0.1:32199/v1/", "fixture", 2);
    private static HttpResponseMessage Response(string content) => new(HttpStatusCode.OK) { Content = new StringContent(JsonSerializer.Serialize(new { choices = new[] { new { finish_reason = "stop", message = new { content } } } })) };
    [TestMethod]
    [DataRow("这是普通文字回复。")]
    [DataRow("验证错误是缺少合法输入；我不会自动修改。")]
    [DataRow("当前工具无法安全修改这一部分。")]
    [DataRow("please apply and Publish now; change 3 to 2")]
    [DataRow("说明：```json\n{\"action\":\"apply\"}\n```")]
    public async Task TextHasNoExecutablePermission(string text) {
        var count = 0; using var client = new HttpClient(new Handler((_, _) => { count++; return Task.FromResult(Response(text)); }));
        var result = await new StudioConversationOrchestrator(new(client)).TurnAsync(Settings, "fixture-key", new(), Element("{}"), (_, _) => throw new AssertFailedException("Plain text must not execute"));
        Assert.AreEqual(text, result.Text.Value); Assert.AreEqual(0, result.Tools.Count); Assert.IsFalse(result.ActionsRejected); Assert.AreEqual(1, count);
    }
    [TestMethod]
    [DataRow("publish")][DataRow("apply_project")][DataRow("shell")][DataRow("write_file")][DataRow("HID")][DataRow("unknown")]
    public async Task UnknownActionPreservesTextWithoutExecutingAnyTool(string name) {
        var reply = JsonSerializer.Serialize(new { message = "这是安全说明。", tool_calls = new object[] { new { name = "get_capabilities", arguments = new { } }, new { name, arguments = new { } } } });
        var sent = 0; using var client = new HttpClient(new Handler((_, _) => { sent++; return Task.FromResult(Response(reply)); }));
        var result = await new StudioConversationOrchestrator(new(client)).TurnAsync(Settings, "fixture-key", new(), Element("{}"), (_, _) => throw new AssertFailedException("Invalid envelope must execute no tools, even valid sibling"));
        Assert.AreEqual("这是安全说明。", result.Message); Assert.IsTrue(result.ActionsRejected); Assert.AreEqual(0, result.Tools.Count); Assert.AreEqual(1, sent);
    }
    [TestMethod]
    public void MalformedActionsPathsExtraFieldsAndOversizeArgumentsRemainRejected() {
        foreach (var call in new[] {
            "{\"name\":\"get_build_errors\",\"arguments\":{\"path\":\"C:/private\"}}",
            "{\"name\":\"propose_effect_change\",\"arguments\":{\"action\":\"apply\"}}",
            "{\"name\":\"get_build_errors\",\"arguments\":{},\"extra\":true}",
            "{\"name\":\"get_preset\",\"arguments\":{\"preset_id\":\"" + new string('x', 9000) + "\"}}"
        }) {
            var turn = StudioConversationContracts.Decode("{\"message\":\"我建议改这里\",\"tool_calls\":[" + call + "]}");
            Assert.AreEqual("我建议改这里", turn.Message); Assert.IsTrue(turn.ActionsRejected); Assert.AreEqual(0, turn.Tools.Count);
        }
    }
    [TestMethod]
    public void BrokenBinaryOversizedAndAmbiguousMessagesNeverBecomeActions() {
        foreach (var text in new[] { "", "\0binary", "\ud800", "{broken", "[]", new string('a', 4001), new string('界', 6000) })
            Assert.ThrowsExactly<StudioLlmException>(() => StudioConversationContracts.Decode(text));
        var duplicate = StudioConversationContracts.Decode("{\"message\":\"safe\",\"message\":\"unsafe\",\"tool_calls\":[]}");
        Assert.IsTrue(duplicate.ActionsRejected); Assert.DoesNotContain("unsafe", duplicate.Message); Assert.AreEqual(0, duplicate.Tools.Count);
    }
    [TestMethod]
    public async Task ActualDiagnosticToolMayFinishWithNaturalText() {
        var count = 0; using var client = new HttpClient(new Handler(async (req, ct) => {
            var body = await req.Content!.ReadAsStringAsync(ct);
            if (++count == 1) return Response("{\"message\":\"查看实际错误\",\"tool_calls\":[{\"name\":\"get_validation_errors\",\"arguments\":{}}]}");
            Assert.Contains("fixture missing input", body); return Response("实际验证错误是 fixture missing input；当前工具无法安全自动修改这一部分。");
        }));
        var result = await new StudioConversationOrchestrator(new(client)).TurnAsync(Settings, "fixture-key", new(), Element("{}"), (_, _) => Task.FromResult(Element("{\"diagnostic\":\"fixture missing input\"}")));
        Assert.Contains("fixture missing input", result.Message); Assert.AreEqual(0, result.Tools.Count); Assert.AreEqual(2, count);
    }
    [TestMethod]
    [DataRow(StudioResponseMode.Auto)][DataRow(StudioResponseMode.JsonObject)][DataRow(StudioResponseMode.JsonSchema)]
    public async Task ResponseModesUseOneGenericAdapterWithoutRetry(StudioResponseMode mode) {
        var count = 0; using var client = new HttpClient(new Handler(async (req, ct) => {
            count++; using var body = JsonDocument.Parse(await req.Content!.ReadAsStringAsync(ct));
            if (mode == StudioResponseMode.Auto) Assert.IsFalse(body.RootElement.TryGetProperty("response_format", out _));
            else {
                var format = body.RootElement.GetProperty("response_format");
                Assert.AreEqual(mode == StudioResponseMode.JsonObject ? "json_object" : "json_schema", format.GetProperty("type").GetString());
                if (mode == StudioResponseMode.JsonSchema) Assert.IsTrue(format.GetProperty("json_schema").GetProperty("strict").GetBoolean());
            }
            return Response(mode == StudioResponseMode.Auto ? "普通回复" : "{\"message\":\"普通回复\",\"tool_calls\":[]}");
        }));
        var result = await new StudioConversationOrchestrator(new(client)).TurnAsync(Settings with { ResponseMode = mode }, "fixture-key", new(), Element("{}"), (_, _) => throw new AssertFailedException("No tool"));
        Assert.AreEqual("普通回复", result.Message); Assert.AreEqual(1, count);
    }
    [TestMethod]
    public async Task ModeHttp400DoesNotTriggerPaidFallbackRetry() {
        var count = 0; using var client = new HttpClient(new Handler((_, _) => { count++; return Task.FromResult(new HttpResponseMessage(HttpStatusCode.BadRequest) { Content = new StringContent("fixture-key") }); }));
        var error = await Assert.ThrowsExactlyAsync<StudioLlmException>(() => new StudioConversationOrchestrator(new(client)).TurnAsync(Settings with { ResponseMode = StudioResponseMode.JsonObject }, "fixture-key", new(), Element("{}"), (_, _) => throw new AssertFailedException("No tool")));
        Assert.AreEqual("response_mode", error.Code); Assert.Contains("切换响应模式", error.Message); Assert.DoesNotContain("fixture-key", error.ToString()); Assert.AreEqual(1, count);
    }
    private sealed class Vault : IStudioCredentialStore {
        public int Reads; public string? Read(string target) { Reads++; return null; } public void Set(string target, string key) => throw new AssertFailedException("Migration must not touch key"); public void Remove(string target) => throw new AssertFailedException("Migration must not remove key");
    }
    [TestMethod]
    [DataRow(false, StudioResponseMode.Auto)][DataRow(true, StudioResponseMode.JsonSchema)]
    public void LegacyBooleanMigratesWithoutCredentialAccess(bool legacy, StudioResponseMode expected) {
        var directory = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString()); Directory.CreateDirectory(directory);
        try {
            var path = Path.Combine(directory, "settings.json"); File.WriteAllText(path, JsonSerializer.Serialize(new { BaseUrl = Settings.BaseUrl, Model = Settings.Model, TimeoutSeconds = 2, StructuredOutput = legacy }));
            var vault = new Vault(); var store = new StudioLlmSettingsStore(path, vault); store.Load(); Assert.AreEqual(expected, store.Settings.EffectiveResponseMode);
            Assert.AreEqual(StudioLlmSettingsStore.Target(Settings), StudioLlmSettingsStore.Target(store.Settings));
            store.Save(store.Settings); using var saved = JsonDocument.Parse(File.ReadAllText(path));
            Assert.IsFalse(saved.RootElement.TryGetProperty("StructuredOutput", out _)); Assert.AreEqual(expected.ToString(), saved.RootElement.GetProperty("ResponseMode").GetString()); Assert.AreEqual(0, vault.Reads);
            store.Load(); Assert.AreEqual(expected, store.Settings.EffectiveResponseMode);
        } finally { Directory.Delete(directory, true); }
    }
    [TestMethod]
    public void PlainTextPathsAndSecretsRedactedWithoutGrantingTools() {
        var result = StudioConversationContracts.Decode("说明 sk-fixture-secret123 C:/Users/owner/private");
        Assert.DoesNotContain("sk-fixture", result.Message); Assert.DoesNotContain("Users", result.Message); Assert.AreEqual(0, result.Tools.Count);
        Assert.ThrowsExactly<StudioLlmException>(() => (Settings with { ResponseMode = (StudioResponseMode)99 }).Endpoint());
    }
    [TestMethod]
    public async Task ProviderRedactionPreservesSafeTextAndStrictlyRejectedJsonAction() {
        var content = JsonSerializer.Serialize(new { message = "安全说明 fixture-key C:/Users/owner/private", tool_calls = new[] { new { name = "propose_effect_change", arguments = new { action = "apply", path = "C:/fixture/private" } } } });
        using var client = new HttpClient(new Handler((_, _) => Task.FromResult(Response(content))));
        var result = await new StudioConversationOrchestrator(new(client)).TurnAsync(Settings, "fixture-key", new(), Element("{}"), (_, _) => throw new AssertFailedException("Redaction must not make action executable"));
        Assert.IsTrue(result.ActionsRejected); Assert.Contains("安全说明", result.Message); Assert.DoesNotContain("fixture-key", result.Message); Assert.DoesNotContain("Users", result.Message); Assert.AreEqual(0, result.Tools.Count);
        var duplicate = StudioLlmRedaction.FilterResponse("{\"message\":\"safe\",\"message\":\"other\",\"tool_calls\":[]}", "fixture-key");
        Assert.IsTrue(StudioConversationContracts.Decode(duplicate).ActionsRejected);
    }
}
