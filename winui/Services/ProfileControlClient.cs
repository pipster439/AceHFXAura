using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Aura_WinUI.Services;

// A transport DTO only. The daemon owns all document and runtime state.
public sealed class ProfileApiResponse
{
    public string Status { get; set; } = "";
    public int ApiVersion { get; set; }
    public long DocumentRevision { get; set; }
    public ulong RuntimeRevision { get; set; }
    public ulong MutationRevision { get; set; }
    public ulong M605SessionGeneration { get; set; }
    public HardwareRtGateStatus? HardwareRtGate { get; set; }
    public Guid SelectedProfileId { get; set; }
    public Guid? ActiveProfileId { get; set; }
    public bool Dirty { get; set; } = true;
    public string? LastActivationReason { get; set; }
    public ProfileApiLastOutcome? LastApplyOutcome { get; set; }
    public string? Outcome { get; set; }
    public string? Error { get; set; }
    public string? ImportWarning { get; set; }
    public DeviceProfile? Profile { get; set; }
    public List<DeviceProfile>? Profiles { get; set; }
    public ProfileMagnetic? GlobalDefaults { get; set; }
    // Daemon-resolved host desired baseline; may include trusted saved HostProfile values.
    public ProfileMagnetic? EffectiveGlobalDefaults { get; set; }
    public List<ProfileApiOperation>? Operations { get; set; }
    public JsonElement? Diagnostics { get; set; }
    public JsonElement? DeviceProfileAutomation { get; set; }
    public DeviceProfileAutomationDecision? AutomationDecision { get; set; }
    public bool? AutomationConfigurationAvailable { get; set; }
    public string? AutomationWarning { get; set; }
    public bool PriorIntentResubmitted { get; set; }
    [JsonExtensionData] public Dictionary<string, JsonElement>? Extensions { get; set; }
}

// Observed device status, never a Profile desired field or submission shadow.
public sealed class HardwareRtGateStatus
{
    public string State { get; set; } = "unknown";
    public string? Source { get; set; }
    public string? ObservedAtUtc { get; set; }
    public ulong ObservationSequence { get; set; }
    public ulong InputSessionGeneration { get; set; }
    public bool InputCollectionConnected { get; set; }
    public string? UnavailableReason { get; set; }
}

public sealed class ProfileApiLastOutcome
{
    public string? Outcome { get; set; }
    public string? Error { get; set; }
    public bool PriorIntentResubmitted { get; set; }
}

public sealed class ProfileApiOperation
{
    public string Identity { get; set; } = "";
    public string Kind { get; set; } = "";
    public ushort LogicalId { get; set; }
    public bool Succeeded { get; set; }
    public string Error { get; set; } = "";
    public bool PriorIntentResubmission { get; set; }
}

public enum ProfileApiErrorCategory { Api, Transport, InvalidResponse, IncompatibleDaemon }

public sealed class ProfileApiException(string message, HttpStatusCode? statusCode = null,
    ProfileApiErrorCategory category = ProfileApiErrorCategory.Api, string? responsePreview = null) : Exception(message)
{
    public HttpStatusCode? StatusCode { get; } = statusCode;
    public ProfileApiErrorCategory Category { get; } = category;
    public string? ResponsePreview { get; } = responsePreview;
    public bool DaemonUnavailable => Category == ProfileApiErrorCategory.Transport;
    public bool IncompatibleDaemon => Category == ProfileApiErrorCategory.IncompatibleDaemon;
    public bool RevisionConflict => StatusCode == HttpStatusCode.Conflict && Category == ProfileApiErrorCategory.Api;
}

public interface IProfileControlClient
{
    Task<ProfileApiResponse> ListAsync(CancellationToken token = default);
    Task<ProfileApiResponse> GetAsync(Guid id, CancellationToken token = default);
    Task<ProfileApiResponse> GetRuntimeAsync(CancellationToken token = default);
    Task<ProfileApiResponse> GetDiagnosticsAsync(CancellationToken token = default);
    Task<ProfileApiResponse> GetHardwareRtGateAsync(CancellationToken token = default) => throw new NotSupportedException();
    Task<ProfileApiResponse> GetAutomationAsync(CancellationToken token = default) => throw new NotSupportedException();
    Task<ProfileApiResponse> GetAutomationStatusAsync(CancellationToken token = default) => GetAutomationAsync(token);
    Task<ProfileApiResponse> UpdateAutomationAsync(DeviceProfileAutomationConfig configuration, long expectedRevision,
        CancellationToken token = default) => throw new NotSupportedException();
    Task<ProfileApiResponse> CreateAsync(string name, long expectedRevision, CancellationToken token = default);
    Task<ProfileApiResponse> DuplicateAsync(Guid id, string name, long expectedRevision, CancellationToken token = default);
    Task<ProfileApiResponse> RenameAsync(Guid id, string name, long expectedRevision, CancellationToken token = default);
    Task<ProfileApiResponse> UpdateAsync(DeviceProfile profile, long expectedRevision, CancellationToken token = default);
    Task<ProfileApiResponse> DeleteAsync(Guid id, long expectedRevision, CancellationToken token = default);
    Task<ProfileApiResponse> UpdateDefaultsAsync(ProfileMagnetic defaults, long expectedRevision,
        CancellationToken token = default);
    Task<ProfileApiResponse> ActivateAsync(Guid id, ProfileActivationReason reason,
        long expectedRevision, ProfileMagnetic? temporaryOverride = null, CancellationToken token = default);
}

public enum ProfileActivationReason { Manual, Automation, Startup, Reconnect, Restore }

public sealed class ProfileControlClient(HttpClient? http = null) : IProfileControlClient
{
    private readonly HttpClient _http = http ?? new HttpClient { Timeout = TimeSpan.FromMinutes(3) };
    private const string Base = "http://127.0.0.1:19897/api/device-profiles";

    public Task<ProfileApiResponse> ListAsync(CancellationToken token = default) => SendAsync(HttpMethod.Get, Base, null, token);
    public Task<ProfileApiResponse> GetAsync(Guid id, CancellationToken token = default) =>
        SendAsync(HttpMethod.Get, $"{Base}/{id:D}", null, token);
    public Task<ProfileApiResponse> GetRuntimeAsync(CancellationToken token = default) =>
        SendAsync(HttpMethod.Get, Base + "/runtime", null, token);
    public Task<ProfileApiResponse> GetDiagnosticsAsync(CancellationToken token = default) =>
        SendAsync(HttpMethod.Get, Base + "/diagnostics", null, token);
    public Task<ProfileApiResponse> GetHardwareRtGateAsync(CancellationToken token = default) =>
        SendAsync(HttpMethod.Get, Base + "/hardware-rt-gate", null, token);
    public Task<ProfileApiResponse> GetAutomationAsync(CancellationToken token = default) =>
        SendAsync(HttpMethod.Get, Base + "/automation", null, token);
    public Task<ProfileApiResponse> GetAutomationStatusAsync(CancellationToken token = default) =>
        SendAsync(HttpMethod.Get, Base + "/automation/status", null, token);
    public Task<ProfileApiResponse> UpdateAutomationAsync(DeviceProfileAutomationConfig configuration, long expectedRevision,
        CancellationToken token = default) => Mutation("automation", new {
            device_profile_automation = configuration, expected_revision = expectedRevision }, token);
    public Task<ProfileApiResponse> CreateAsync(string name, long expectedRevision, CancellationToken token = default) =>
        Mutation("create", new { name, expected_revision = expectedRevision }, token);
    public Task<ProfileApiResponse> DuplicateAsync(Guid id, string name, long expectedRevision,
        CancellationToken token = default) => Mutation("duplicate", new {
            profile_id = id, name, expected_revision = expectedRevision }, token);
    public Task<ProfileApiResponse> RenameAsync(Guid id, string name, long expectedRevision,
        CancellationToken token = default) => Mutation("rename", new {
            profile_id = id, name, expected_revision = expectedRevision }, token);
    public Task<ProfileApiResponse> UpdateAsync(DeviceProfile profile, long expectedRevision,
        CancellationToken token = default) => Mutation("update", new {
            profile_id = profile.Id, profile, expected_revision = expectedRevision }, token);
    public Task<ProfileApiResponse> DeleteAsync(Guid id, long expectedRevision, CancellationToken token = default) =>
        Mutation("delete", new { profile_id = id, expected_revision = expectedRevision }, token);
    public Task<ProfileApiResponse> UpdateDefaultsAsync(ProfileMagnetic defaults, long expectedRevision,
        CancellationToken token = default) => Mutation("defaults", new {
            global_defaults = defaults, expected_revision = expectedRevision }, token);
    public Task<ProfileApiResponse> ActivateAsync(Guid id, ProfileActivationReason reason,
        long expectedRevision, ProfileMagnetic? temporaryOverride = null, CancellationToken token = default) =>
        Mutation("activate", new { profile_id = id, reason = reason.ToString(),
            expected_revision = expectedRevision, temporary_override = temporaryOverride }, token);

    private Task<ProfileApiResponse> Mutation(string action, object body, CancellationToken token) =>
        SendAsync(HttpMethod.Post, Base + "/" + action, body, token);

    private async Task<ProfileApiResponse> SendAsync(HttpMethod method, string url, object? body, CancellationToken token)
    {
        try
        {
            using var request = new HttpRequestMessage(method, url);
            if (body is not null) request.Content = JsonContent.Create(body, options: ProfileJson.Options);
            using var response = await _http.SendAsync(request, token).ConfigureAwait(false);
            var payload = response.Content is null ? "" :
                await response.Content.ReadAsStringAsync(token).ConfigureAwait(false);
            var code = (int)response.StatusCode;
            var mediaType = response.Content?.Headers.ContentType?.MediaType;
            var preview = payload.Length == 0 ? null : payload[..Math.Min(payload.Length, 200)];
            var missingRoute = response.StatusCode == HttpStatusCode.NotFound;
            var missingCapabilityRoute = missingRoute && method == HttpMethod.Get &&
                (url == Base || url == Base + "/runtime" || url == Base + "/diagnostics" || url == Base + "/automation" ||
                 url == Base + "/hardware-rt-gate");
            if (string.IsNullOrWhiteSpace(payload))
                throw new ProfileApiException(missingRoute ?
                    "Daemon Profile API is unavailable (HTTP 404); the running daemon may be outdated." :
                    $"Daemon Profile API returned HTTP {code} with an empty response.",
                    response.StatusCode, missingRoute ? ProfileApiErrorCategory.IncompatibleDaemon :
                        ProfileApiErrorCategory.InvalidResponse);
            if (mediaType is null || !(mediaType.Equals("application/json", StringComparison.OrdinalIgnoreCase) ||
                    mediaType.EndsWith("+json", StringComparison.OrdinalIgnoreCase)))
                throw new ProfileApiException($"Daemon Profile API returned an invalid response (HTTP {code} / {mediaType ?? "unknown"}).",
                    response.StatusCode, missingRoute ? ProfileApiErrorCategory.IncompatibleDaemon :
                        ProfileApiErrorCategory.InvalidResponse, preview);
            if (missingCapabilityRoute)
                throw new ProfileApiException("Daemon Profile API is unavailable (HTTP 404); the running daemon may be outdated.",
                    response.StatusCode, ProfileApiErrorCategory.IncompatibleDaemon, preview);
            ProfileApiResponse? result;
            try { result = JsonSerializer.Deserialize<ProfileApiResponse>(payload, ProfileJson.Options); }
            catch (JsonException) {
                throw new ProfileApiException($"Daemon Profile API returned an invalid response (HTTP {code} / {mediaType}).",
                    response.StatusCode, missingRoute ? ProfileApiErrorCategory.IncompatibleDaemon :
                        ProfileApiErrorCategory.InvalidResponse, preview);
            }
            if (result is null || result.Status is not ("ok" or "error"))
                throw new ProfileApiException($"Daemon Profile API returned an invalid response (HTTP {code} / {mediaType}).",
                    response.StatusCode, missingRoute ? ProfileApiErrorCategory.IncompatibleDaemon :
                        ProfileApiErrorCategory.InvalidResponse, preview);
            // An apply failure is a complete daemon result with per-operation
            // diagnostics. A revision conflict is an error envelope instead.
            var applyFailure = response.StatusCode == HttpStatusCode.Conflict &&
                result.Status == "ok" && result.Outcome == "failed";
            if ((!response.IsSuccessStatusCode && !applyFailure) || result.Status != "ok")
                throw new ProfileApiException(result.Error ?? "Daemon Profile request failed", response.StatusCode);
            if (result.ApiVersion != 1) throw new ProfileApiException("Unsupported daemon Profile API version",
                response.StatusCode, ProfileApiErrorCategory.IncompatibleDaemon);
            return result;
        }
        catch (HttpRequestException ex) { throw new ProfileApiException("Daemon Profile API unavailable: " + ex.Message,
            category: ProfileApiErrorCategory.Transport); }
        catch (TaskCanceledException ex) when (!token.IsCancellationRequested) {
            throw new ProfileApiException("Daemon Profile API timed out: " + ex.Message,
                category: ProfileApiErrorCategory.Transport);
        }
    }
}
