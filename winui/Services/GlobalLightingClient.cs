namespace Aura_WinUI.Services;

public sealed class GlobalLightingSettings
{
    public string Status { get; set; } = "";
    public int ApiVersion { get; set; }
    public string Revision { get; set; } = "";
    public int Fps { get; set; }
}
public sealed class LightingWriteAcknowledgement
{
    public string Status { get; set; } = "";
    public int ApiVersion { get; set; }
    public string Revision { get; set; } = "";
}
public sealed partial class AuraControlClient
{
    public async Task<ApiResult<GlobalLightingSettings>> GetGlobalLightingAsync(CancellationToken token = default)
    {
        var result = await LocalApi.Send<GlobalLightingSettings>(_http, 19897, "/api/lighting/global", System.Net.Http.HttpMethod.Get, null, token);
        return result.IsSuccess && (result.Value!.Status != "ok" || result.Value.ApiVersion != 1 || string.IsNullOrEmpty(result.Value.Revision) || result.Value.Fps is < 10 or > 100)
            ? new(null, "全局刷新率接口不兼容") : result;
    }
    public async Task<ApiResult<LightingWriteAcknowledgement>> UpdateGlobalLightingAsync(int fps, string revision, CancellationToken token = default)
    {
        var result = await LocalApi.Send<LightingWriteAcknowledgement>(_http, 19897, "/api/lighting/global", System.Net.Http.HttpMethod.Patch,
            new { fps, expected_revision = revision }, token);
        return result.IsSuccess && (result.Value!.Status != "ok" || result.Value.ApiVersion != 1 || string.IsNullOrEmpty(result.Value.Revision))
            ? new(null, "保存结果未经核心确认") : result;
    }
}
