using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Aura_WinUI.Services;

public sealed class NativePluginItem
{
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("kind")] public string Kind { get; set; } = "native_dll";
    [JsonPropertyName("generation_id")] public ulong GenerationId { get; set; }
    [JsonPropertyName("path")] public string Path { get; set; } = "";
    [JsonPropertyName("has_lifecycle")] public bool HasLifecycle { get; set; }
    [JsonPropertyName("has_logical_keyboard")] public bool HasLogicalKeyboard { get; set; }
    [JsonPropertyName("has_hall_analog")] public bool HasHallAnalog { get; set; }
}

public sealed class BridgePluginItem
{
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("description")] public string Description { get; set; } = "";
    [JsonPropertyName("source_dir")] public string SourceDir { get; set; } = "";
    [JsonPropertyName("dll_path")] public string DllPath { get; set; } = "";
    [JsonPropertyName("install_script_path")] public string InstallScriptPath { get; set; } = "";
    [JsonPropertyName("installed_path")] public string InstalledPath { get; set; } = "";
    [JsonPropertyName("built")] public bool Built { get; set; }
    [JsonPropertyName("installed")] public bool Installed { get; set; }
    [JsonPropertyName("shared_memory_name")] public string SharedMemoryName { get; set; } = "";
    [JsonPropertyName("shared_memory_ok")] public bool SharedMemoryOk { get; set; }
    [JsonPropertyName("hall_active")] public bool HallActive { get; set; }
    [JsonPropertyName("hall_rate_hz")] public double HallRateHz { get; set; }
}

public sealed class PluginOverviewResponse
{
    [JsonPropertyName("status")] public string Status { get; set; } = "error";
    [JsonPropertyName("api_version")] public int ApiVersion { get; set; }
    [JsonPropertyName("plugins_dir")] public string PluginsDir { get; set; } = "";
    [JsonPropertyName("native_plugins")] public List<NativePluginItem> NativePlugins { get; set; } = [];
    [JsonPropertyName("bridges")] public List<BridgePluginItem> Bridges { get; set; } = [];
}

public sealed class PluginOverviewClient(HttpClient? http = null)
{
    private readonly HttpClient _http = http ?? new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
    private const string Base = "http://127.0.0.1:19897/api/plugins/";

    public Task<PluginOverviewResponse> GetOverviewAsync() => SendAsync("overview", null);
    public Task<PluginOverviewResponse> RescanAsync() => SendAsync("rescan", new { });

    private async Task<PluginOverviewResponse> SendAsync(string path, object? body)
    {
        try
        {
            using var req = new HttpRequestMessage(body == null ? HttpMethod.Get : HttpMethod.Post, Base + path);
            if (body != null)
            {
                req.Content = new StringContent(JsonSerializer.Serialize(body), Encoding.UTF8, "application/json");
            }
            using var resp = await _http.SendAsync(req).ConfigureAwait(false);
            var json = await resp.Content.ReadAsStringAsync().ConfigureAwait(false);
            return JsonSerializer.Deserialize<PluginOverviewResponse>(json) ?? new PluginOverviewResponse();
        }
        catch
        {
            return new PluginOverviewResponse();
        }
    }
}
