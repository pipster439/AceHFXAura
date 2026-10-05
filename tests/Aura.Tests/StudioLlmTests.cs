using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public class StudioLlmTests
{
    private const string Secret = "fixture-only-key";
    private static readonly StudioLlmSettings Settings = new("http://127.0.0.1:32199/v1/", "fixture", 1);
    private static string Success(string content = "ok", string finish = "stop") => JsonSerializer.Serialize(new { choices = new[] { new { finish_reason = finish, message = new { content } } } });
    private sealed class Handler(Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> send) : HttpMessageHandler {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken) => send(request, cancellationToken);
    }
    private static HttpClient Client(string body, HttpStatusCode status = HttpStatusCode.OK) => new(new Handler((_, _) => Task.FromResult(new HttpResponseMessage(status) { Content = new StringContent(body) })));
    private sealed class StreamedContent(byte[] data) : HttpContent {
        protected override bool TryComputeLength(out long length) { length = 0; return false; }
        protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context) => stream.WriteAsync(data).AsTask();
    }
    [TestMethod]
    public async Task SuccessFiltersSecretPathsAndSupportsStrictSchema()
    {
        using var client = new HttpClient(new Handler(async (req, ct) => {
            Assert.AreEqual(Secret, req.Headers.Authorization!.Parameter);
            var body = await req.Content!.ReadAsStringAsync(ct);
            Assert.DoesNotContain(Secret, body); Assert.DoesNotContain("Users", body);
            using var doc = JsonDocument.Parse(body);
            Assert.AreEqual("json_schema", doc.RootElement.GetProperty("response_format").GetProperty("type").GetString());
            return new HttpResponseMessage(HttpStatusCode.OK) { Content = new StringContent(Success("Bearer " + Secret)) };
        }));
        using var schema = JsonDocument.Parse("{\"type\":\"object\"}");
        var result = await new StudioLlmProvider(client).CompleteAsync(Settings with { StructuredOutput = true }, Secret, "JSON only", @"C:\Users\fixture\project " + Secret, schema: schema.RootElement);
        Assert.DoesNotContain(Secret, result);
    }
    [TestMethod]
    [DataRow(401, "auth")][DataRow(403, "auth")][DataRow(429, "rate_limit")][DataRow(500, "http")][DataRow(503, "http")]
    public async Task HttpErrorsNeverRetainProviderSecret(int status, string code)
    {
        using var client = Client(Secret, (HttpStatusCode)status);
        var ex = await Assert.ThrowsExactlyAsync<StudioLlmException>(() => new StudioLlmProvider(client).CompleteAsync(Settings, Secret, "JSON", "context"));
        Assert.AreEqual(code, ex.Code); Assert.DoesNotContain(Secret, ex.ToString());
    }
    [TestMethod]
    [DataRow("{")][DataRow("{}")] [DataRow("{\"choices\":[]}")]
    public async Task MalformedAndTruncatedJsonAreRejected(string body) {
        using var client = Client(body);
        await Assert.ThrowsExactlyAsync<StudioLlmException>(() => new StudioLlmProvider(client).CompleteAsync(Settings, Secret, "JSON", "context"));
    }
    [TestMethod]
    public async Task IncompleteCompletionAndOversizedResponsesAreRejected() {
        using var truncated = Client(Success("partial", "length"));
        var ex = await Assert.ThrowsExactlyAsync<StudioLlmException>(() => new StudioLlmProvider(truncated).CompleteAsync(Settings, Secret, "JSON", "context"));
        Assert.AreEqual("truncated", ex.Code);
        using var huge = Client(new string('x', StudioLlmProvider.MaxResponseBytes + 1));
        ex = await Assert.ThrowsExactlyAsync<StudioLlmException>(() => new StudioLlmProvider(huge).CompleteAsync(Settings, Secret, "JSON", "context"));
        Assert.AreEqual("response_size", ex.Code);
    }
    [TestMethod]
    public async Task StreamingResponseLimitsDoNotDependOnContentLength() {
        using var client = new HttpClient(new Handler((_, _) => Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) {
            Content = new StreamedContent(new byte[StudioLlmProvider.MaxResponseBytes + 1]) })));
        var ex = await Assert.ThrowsExactlyAsync<StudioLlmException>(() => new StudioLlmProvider(client).CompleteAsync(Settings, Secret, "JSON", "fixture"));
        Assert.AreEqual("response_size", ex.Code);
    }
    [TestMethod]
    public async Task FailuresDoNotAutomaticallyRetryOrPublish() {
        var calls = 0;
        using var client = new HttpClient(new Handler((_, _) => { calls++; return Task.FromResult(new HttpResponseMessage(HttpStatusCode.InternalServerError) { Content = new StringContent(Secret) }); }));
        await Assert.ThrowsExactlyAsync<StudioLlmException>(() => new StudioLlmProvider(client).CompleteAsync(Settings, Secret, "JSON", "fixture"));
        Assert.AreEqual(1, calls);
    }
    [TestMethod]
    public async Task TimeoutAndUserCancellationAreDistinct() {
        using var client = new HttpClient(new Handler(async (_, ct) => { await Task.Delay(Timeout.Infinite, ct); return new(); }));
        var provider = new StudioLlmProvider(client);
        var ex = await Assert.ThrowsExactlyAsync<StudioLlmException>(() => provider.CompleteAsync(Settings, Secret, "JSON", "context"));
        Assert.AreEqual("timeout", ex.Code);
        using var cancelled = new CancellationTokenSource(); cancelled.Cancel();
        await Assert.ThrowsExactlyAsync<TaskCanceledException>(() => provider.CompleteAsync(Settings, Secret, "JSON", "context", cancelled.Token));
    }
    [TestMethod]
    [DataRow("http://external.test/v1")][DataRow("file:///tmp/x")][DataRow("https://user:pass@example.test/")][DataRow("https://example.test/?token=x")][DataRow("relative")]
    public void InvalidUrlsAreRejected(string url) => Assert.ThrowsExactly<StudioLlmException>(() => (Settings with { BaseUrl = url }).Endpoint());
    [TestMethod]
    public async Task RequestLimitsApplyBeforeHttp() {
        using var client = Client(Success());
        var ex = await Assert.ThrowsExactlyAsync<StudioLlmException>(() => new StudioLlmProvider(client).CompleteAsync(Settings, Secret, "JSON", new string('x', 65537)));
        Assert.AreEqual("request_size", ex.Code);
    }
    private sealed class Vault : IStudioCredentialStore {
        public readonly Dictionary<string, string> Keys = [];
        public string? Read(string target) => Keys.GetValueOrDefault(target);
        public void Set(string target, string key) => Keys[target] = key;
        public void Remove(string target) => Keys.Remove(target);
    }
    [TestMethod]
    public void RepairBudgetStopsAfterTwoAndResetsOnlyForNewRequest() {
        var budget = new StudioAssistantRepairBudget(); budget.TakeRepair(); budget.TakeRepair();
        Assert.ThrowsExactly<StudioLlmException>(() => budget.TakeRepair()); Assert.AreEqual(2, budget.Count);
        budget.StartRequest(); budget.TakeRepair(); Assert.AreEqual(1, budget.Count);
    }
    [TestMethod]
    public void AssistantContextAndReplyAreStrictAndSecretFiltered() {
        var context = JsonSerializer.Serialize(new { intent = "modify", name = "fixture", publication = new { mode = "continuous", fade_out_ms = 0 },
            capabilities = new { schema_version = 1, inputs = Array.Empty<string>(), outputs = new[] { "keyboard_rgb" }, features = new[] { "simulation_supported" }, gsi_fields = Array.Empty<string>(), diagnostics = Array.Empty<string>() },
            nodes = new[] { new { node_id = "n0", type = "math_number", parent = "color_rgb", input = "R", value = 0, min = 0, max = 255 } },
            presets = new[] { new { id = "template_smooth_breath", name = "模板", description = "双色", tags = new[] { "光效" }, required_inputs = Array.Empty<string>(), capabilities = new[] { "blockly" } } }, diagnostic = "", diagnostic_kind = "none" });
        var safe = StudioAssistantContracts.Context(context, Secret + @" C:\Users\fixture\test", Secret);
        Assert.DoesNotContain(Secret, safe); Assert.DoesNotContain("Users", safe);
        Assert.ThrowsExactly<StudioLlmException>(() => StudioAssistantContracts.Context(context.Replace("keyboard_rgb", "hid_write"), "prompt", Secret));
        Assert.ThrowsExactly<StudioLlmException>(() => StudioAssistantContracts.Context(context.Replace("\"intent\":\"modify\"", "\"intent\":\"publish\""), "prompt", Secret));
        Assert.ThrowsExactly<StudioLlmException>(() => StudioAssistantContracts.Context(context.Replace("\"name\":\"fixture\"", "\"name\":\"../file\""), "prompt", Secret));
        var reply = "{\"action\":\"propose\",\"summary\":\"调整\",\"preset\":null,\"edits\":[{\"node_id\":\"n0\",\"value\":2}]}";
        Assert.AreEqual(reply, StudioAssistantContracts.ValidateReply(reply));
        Assert.ThrowsExactly<StudioLlmException>(() => StudioAssistantContracts.ValidateReply(reply.Replace("propose", "shell")));
        Assert.ThrowsExactly<StudioLlmException>(() => StudioAssistantContracts.ValidateReply(reply.Replace("\"preset\":null", "\"preset\":null,\"path\":\"../x\"")));
        Assert.ThrowsExactly<StudioLlmException>(() => StudioAssistantContracts.ValidateReply(reply.Replace("n0", "../x")));
    }
    [TestMethod]
    public void SettingsContainNoKeyAndEndpointChangesCannotReuseIt() {
        var folder = Path.Combine(Path.GetTempPath(), "aura-llm-test-" + Guid.NewGuid());
        try {
            var path = Path.Combine(folder, "settings.json"); var vault = new Vault();
            var store = new StudioLlmSettingsStore(path, vault); store.Save(Settings, Secret);
            Assert.DoesNotContain(Secret, File.ReadAllText(path)); Assert.IsTrue(store.HasKey);
            store.Save(Settings with { BaseUrl = "https://different.test/v1" }); Assert.IsFalse(store.HasKey);
            store.Save(Settings); store.RemoveKey(); Assert.IsFalse(store.HasKey);
            var loaded = new StudioLlmSettingsStore(path, vault); loaded.Load(); Assert.AreEqual(Settings, loaded.Settings);
        } finally { if (Directory.Exists(folder)) Directory.Delete(folder, true); }
    }
    [TestMethod]
    public void WindowsCredentialRoundTripUsesOnlyRandomTestTarget() {
        if (!OperatingSystem.IsWindows()) { Assert.Inconclusive("Windows-only credential API"); return; }
        var vault = new StudioCredentialStore(); var target = "Aura/StudioLLM/test-" + Guid.NewGuid();
        try { Assert.IsNull(vault.Read(target)); vault.Set(target, Secret); Assert.AreEqual(Secret, vault.Read(target)); vault.Remove(target); Assert.IsNull(vault.Read(target)); }
        finally { vault.Remove(target); }
    }
    [TestMethod]
    public async Task ActualLoopbackHttpFixtureSucceeds() {
        using var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var server = Task.Run(async () => {
            using var connection = await listener.AcceptTcpClientAsync(); await using var stream = connection.GetStream();
            var buffer = new byte[4096]; var header = "";
            while (!header.Contains("\r\n\r\n")) header += Encoding.UTF8.GetString(buffer, 0, await stream.ReadAsync(buffer));
            Assert.StartsWith("POST /v1/chat/completions", header);
            var body = Success(); var bytes = Encoding.UTF8.GetBytes($"HTTP/1.1 200 OK\r\nContent-Length: {Encoding.UTF8.GetByteCount(body)}\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{body}");
            await stream.WriteAsync(bytes);
        });
        using var client = StudioLlmProvider.CreateHttpClient();
        Assert.AreEqual("ok", await new StudioLlmProvider(client).CompleteAsync(Settings with { BaseUrl = $"http://127.0.0.1:{port}/v1", TimeoutSeconds = 5 }, Secret, "JSON", "fixture"));
        await server;
    }
}
