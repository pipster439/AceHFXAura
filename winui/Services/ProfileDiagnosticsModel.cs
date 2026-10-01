using System.Text.Json;

namespace Aura_WinUI.Services;

// Export snapshot only. Does not update editor/runtime state or issue mutations.
public sealed class ProfileDiagnosticsModel(IProfileControlClient client)
{
    public bool IsBusy { get; private set; }
    public string? Json { get; private set; }
    public string Error { get; private set; } = "";

    // A diagnostic export must never become a second live configuration writer.
    public static bool CanExportFileName(string name) =>
        !name.Equals("device-profiles.json", StringComparison.OrdinalIgnoreCase) &&
        !name.Equals("config.json", StringComparison.OrdinalIgnoreCase);

    public async Task LoadAsync(CancellationToken token = default)
    {
        if (IsBusy) return;
        IsBusy = true;
        Json = null;
        Error = "";
        try {
            token.ThrowIfCancellationRequested();
            var response = await client.GetDiagnosticsAsync(token);
            token.ThrowIfCancellationRequested();
            if (response.Diagnostics is not { ValueKind: JsonValueKind.Object } snapshot ||
                !snapshot.TryGetProperty("diagnostic_schema_version", out var version) ||
                version.ValueKind != JsonValueKind.Number || !version.TryGetInt32(out var number) || number != 1)
                throw new ProfileApiException("Unsupported diagnostic response", category: ProfileApiErrorCategory.InvalidResponse);
            Json = JsonSerializer.Serialize(snapshot, new JsonSerializerOptions { WriteIndented = true });
        }
        catch (ProfileApiException ex) {
            Error = ex.DaemonUnavailable ? "Aura 后台服务不可用，请恢复连接后重试。" :
                ex.IncompatibleDaemon ? "当前 Aura 后台版本不支持诊断导出，请重启 Aura 以加载匹配版本的后台服务。" :
                "诊断服务响应异常，请重试；若持续出现，请检查后台版本。";
        }
        finally { IsBusy = false; }
    }
}
