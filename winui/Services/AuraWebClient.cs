using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Aura_WinUI.Services;

internal static class LocalApi
{
    internal static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };
    internal static async Task<ApiResult<T>> Send<T>(HttpClient http, int port, string route, HttpMethod method, object? body,
        CancellationToken token) where T : class
    {
        try
        {
            using var request = new HttpRequestMessage(method, $"http://127.0.0.1:{port}{route}");
            if (body != null) request.Content = new StringContent(JsonSerializer.Serialize(body, Options), Encoding.UTF8, "application/json");
            using var response = await http.SendAsync(request, token);
            var json = await response.Content.ReadAsStringAsync(token);
            if (!response.IsSuccessStatusCode)
            {
                string error = $"HTTP {(int)response.StatusCode}";
                try { using var document = JsonDocument.Parse(json); if (document.RootElement.TryGetProperty("message", out var message)) error += ": " + message.GetString(); }
                catch (JsonException) { }
                return new(null, error, (int)response.StatusCode);
            }
            var value = JsonSerializer.Deserialize<T>(json, Options);
            return value is null ? new(null, "Empty service response") : new(value, StatusCode: (int)response.StatusCode);
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { throw; }
        catch (Exception ex) { return new(null, ex.Message); }
    }
}

public sealed class AuraWebClient
{
    private readonly HttpClient _http;
    public AuraWebClient(HttpClient? http = null) => _http = http ?? new() { Timeout = TimeSpan.FromSeconds(3) };
    public Task<ApiResult<WebStatus>> GetStatusAsync(CancellationToken token = default) => LocalApi.Send<WebStatus>(_http, 19898, "/api/status", HttpMethod.Get, null, token);
    public async Task<ApiResult<GsiCfgInfo>> GetCfgAsync(CancellationToken token = default)
    {
        var result = await LocalApi.Send<GsiCfgInfo>(_http, 19898, "/api/gsi/cfg", HttpMethod.Get, null, token);
        return result.IsSuccess && (result.Value!.GsiApiVersion != 1 || result.Value.Paths == null || result.Value.Paths.Any(p => p == null || string.IsNullOrEmpty(p.Path)))
            ? new(null, "Unsupported GSI configuration contract") : result;
    }
    public async Task<ApiResult<GsiInstallResult>> InstallCfgAsync(GsiCfgPath target, CancellationToken token = default)
    {
        var result = await LocalApi.Send<GsiInstallResult>(_http, 19898, "/api/gsi/install-cfg", HttpMethod.Post,
            new { target_dir = target.Path, expected_cfg_revision = target.Revision }, token);
        return result.IsSuccess && result.Value!.Status != "ok" ? new(null, "Installation was not confirmed") : result;
    }
}

public sealed partial class AuraControlClient
{
    public async Task<ApiResult<GsiCurrent>> GetGsiAsync(CancellationToken token = default)
    {
        var result = await LocalApi.Send<GsiCurrent>(_http, 19897, "/api/gsi/current", HttpMethod.Get, null, token);
        return result.IsSuccess && (result.Value!.GsiApiVersion != 1 || string.IsNullOrEmpty(result.Value.InstanceId) || result.Value.Data == null || result.Value.Source is not ("real" or "simulation"))
            ? new(null, "Unsupported GSI contract") : result;
    }
    public async Task<ApiResult<SimulationState>> GetSimulationAsync(CancellationToken token = default)
    {
        var result = await LocalApi.Send<SimulationState>(_http, 19897, "/api/gsi/simulation", HttpMethod.Get, null, token);
        return result.IsSuccess && (result.Value!.GsiApiVersion != 1 || string.IsNullOrEmpty(result.Value.InstanceId) || result.Value.Source is not ("real" or "simulation"))
            ? new(null, "Unsupported simulation contract") : result;
    }
    public async Task<ApiResult<SimulationState>> UpdateSimulationAsync(SimulationPatch patch, CancellationToken token = default)
    {
        // No automatic POST retry: increment_kill is not idempotent.
        var queued = await LocalApi.Send<QueuedSimulation>(_http, 19897, "/api/gsi/simulation", HttpMethod.Post, patch, token);
        if (!queued.IsSuccess) return new(null, queued.Error, queued.StatusCode);
        if (queued.StatusCode != 202 || queued.Value!.Status != "queued" || queued.Value.Sequence == 0 || string.IsNullOrEmpty(queued.Value.InstanceId))
            return new(null, "Invalid simulation acknowledgement");
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
        deadline.CancelAfter(TimeSpan.FromSeconds(8));
        try
        {
            while (true)
            {
                var state = await GetSimulationAsync(deadline.Token);
                if (!state.IsSuccess) return state;
                if (state.Value!.InstanceId != queued.Value.InstanceId) return new(null, "核心已重启；操作结果不确定，请刷新，不要自动重发");
                if (state.Value.AppliedSequence >= queued.Value.Sequence) return state;
                await Task.Delay(100, deadline.Token);
            }
        }
        catch (OperationCanceledException) when (!token.IsCancellationRequested)
        { return new(null, "尚未收到核心应用确认；操作可能仍在队列中，请刷新"); }
    }
}
