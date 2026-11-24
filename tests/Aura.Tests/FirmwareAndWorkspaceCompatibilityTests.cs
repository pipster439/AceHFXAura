using System.Net;
using System.Text;
using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class FirmwareAndWorkspaceCompatibilityTests
{
    [DataTestMethod]
    [DataRow("Unknown")][DataRow("Unsupported")][DataRow("ReadFailed")]
    public async Task FirmwareDenialIsDiagnosticAndCannotBecomeClientSuccess(string state)
    {
        var error = "Firmware" + state + ": fixture compatibility rejection";
        var handler = new Reply(HttpStatusCode.Conflict, JsonSerializer.Serialize(new {
            status = "error", api_version = 1, health = "Clean", available = true,
            last_error = error, firmware_compatibility = new { state, version = state == "Unsupported" ? "1.00.58" : null, reason = error, session_generation = 7 }
        }));
        var client = new MagneticControlClient(new HttpClient(handler));
        var result = await client.SetActuationAsync(0x0402, 1);
        Assert.IsFalse(result.Succeeded); Assert.AreEqual(error, result.LastError);
        Assert.AreEqual(state, result.FirmwareCompatibility.State);
        Assert.AreEqual(7UL, result.FirmwareCompatibility.SessionGeneration);
        Assert.IsFalse(handler.Body!.Contains("supported", StringComparison.OrdinalIgnoreCase));
        Assert.IsFalse(handler.Body.Contains("firmware", StringComparison.OrdinalIgnoreCase));
    }

    [TestMethod]
    public async Task OldStatusWithoutFirmwareFieldRemainsReadableAndDefaultsUnknown()
    {
        var handler = new Reply(HttpStatusCode.OK, "{\"status\":\"ok\",\"api_version\":1,\"health\":\"Clean\",\"available\":true}");
        var result = await new MagneticControlClient(new HttpClient(handler)).GetStatusAsync();
        Assert.IsTrue(result.Succeeded); Assert.AreEqual("Unknown", result.FirmwareCompatibility.State);
        Assert.IsNull(result.FirmwareCompatibility.Version);
    }

    [TestMethod]
    public async Task ProfileFirmwareFailurePreservesErrorAndRevisionRequest()
    {
        var handler = new Reply(HttpStatusCode.Conflict, "{\"status\":\"error\",\"error\":\"FirmwareUnsupported: 1.00.58\",\"error_code\":\"FirmwareAdmissionDenied\"}");
        var client = new ProfileControlClient(new HttpClient(handler));
        var error = await Assert.ThrowsExactlyAsync<ProfileApiException>(() => client.ActivateAsync(Guid.NewGuid(), ProfileActivationReason.Manual, 19));
        StringAssert.Contains(error.Message, "FirmwareUnsupported");
        Assert.IsFalse(error.RevisionConflict, "Firmware refusal must not be classified as concurrent document revision");
        using var body = JsonDocument.Parse(handler.Body!);
        Assert.AreEqual(19, body.RootElement.GetProperty("expected_revision").GetInt32());
        Assert.IsFalse(body.RootElement.TryGetProperty("firmware_compatibility", out _));
    }

    [DataTestMethod]
    [DataRow("{}")]
    [DataRow("{\"variables\":[]}")]
    [DataRow("{\"workspaceComments\":[{\"id\":\"comment_fixture\",\"text\":\"工作区说明\",\"x\":12,\"y\":24,\"width\":200,\"height\":100}]}")]
    public void RealEmptyAndCommentWorkspacesSurviveJournalSnapshotAndBundle(string graph)
    {
        var root = Path.Combine(Path.GetTempPath(), "aura-empty-compat-" + Guid.NewGuid());
        Directory.CreateDirectory(root);
        try {
            var draft = new StudioDraft("fixture", JsonDocument.Parse(graph).RootElement.Clone(),
                JsonDocument.Parse("{\"mode\":\"continuous\",\"fade_out_ms\":0}").RootElement.Clone());
            draft.Validate();
            var baseline = draft with { Json = JsonDocument.Parse("{\"blocks\":{\"blocks\":[{\"type\":\"math_number\",\"fields\":{\"NUM\":3}}]}}").RootElement.Clone() };
            var store = new StudioDraftStore(root); store.ObserveSaved(baseline); store.Autosave(draft);
            var restarted = new StudioDraftStore(root); restarted.ObserveSaved(baseline);
            Assert.AreEqual(draft.Fingerprint(), restarted.RestoreRecovery("fixture").Fingerprint());
            var snapshot = restarted.Snapshot(draft, "before_ai_apply");
            Assert.AreEqual(draft.Fingerprint(), restarted.RestoreSnapshot("fixture", snapshot.Id).Fingerprint());
            var payload = JsonSerializer.SerializeToElement(new {
                manifest = new { schema_version=1, name="fixture", description="", tags=Array.Empty<string>(),
                    created_with_version="fixture", capabilities=new { schema_version=1, inputs=Array.Empty<string>(), outputs=Array.Empty<string>(), features=new[]{"simulation_supported"}, gsi_fields=Array.Empty<string>(), diagnostics=Array.Empty<string>() } },
                project = new { name=draft.Name, json=draft.Json, publication=draft.Publication }
            });
            var parsed = StudioProjectBundle.Parse(StudioProjectBundle.Export(payload, "fixture"));
            Assert.AreEqual(draft.Fingerprint(), StudioDraft.Parse(parsed.GetProperty("project")).Fingerprint());
        } finally { Directory.Delete(root, true); }
    }

    private sealed class Reply(HttpStatusCode status, string payload) : HttpMessageHandler
    {
        public string? Body { get; private set; }
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        {
            Body = request.Content is null ? null : await request.Content.ReadAsStringAsync(token);
            return new(status) { Content = new StringContent(payload, Encoding.UTF8, "application/json") };
        }
    }
}
