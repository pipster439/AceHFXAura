using System.Text.Json;
using Aura_WinUI.Services;
using Windows.Storage.Pickers;

namespace Aura_WinUI.Pages;

public sealed partial class StudioPage
{
    private bool _bundleBusy;
    // Validation fixture may provide its own picker delegates. Production never
    // accepts a path from WebView/manifest and always uses the user's picker.
    internal Func<Task<byte[]?>>? BundleImportFixture;
    internal Func<byte[], Task>? BundleExportFixture;
    private async Task<bool> ReceiveBundleAsync(string json) {
        if (json.Length > 300000) return false;
        string? id = null; var ownsBusy = false;
        try {
            using var doc = JsonDocument.Parse(json, new JsonDocumentOptions { MaxDepth = 64 }); var m = doc.RootElement;
            if (!m.TryGetProperty("type", out var t) || t.GetString() != "studio_bundle") return false;
            id = m.GetProperty("request_id").GetString();
            if (id == null || !System.Text.RegularExpressions.Regex.IsMatch(id, "^[a-f0-9]{32}$")) return true;
            var op = m.GetProperty("operation").GetString();
            StudioDraft.Exact(m, op == "export" ? ["type", "request_id", "operation", "payload"] : ["type", "request_id", "operation"]);
            if (_bundleBusy || _shell.WorkType != "effect") throw new StudioPersistenceException("请等待当前操作完成后再导入或导出。");
            _bundleBusy = true; ownsBusy = true; object result;
            var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(MainWindow.CurrentInstance);
            if (op == "import") {
                byte[]? data;
                if (BundleImportFixture != null && Aura_WinUI.Validation.StudioPolishValidation.Requested) data = await BundleImportFixture();
                else {
                    var picker = new FileOpenPicker(); picker.FileTypeFilter.Add(".auraeffect"); WinRT.Interop.InitializeWithWindow.Initialize(picker, hwnd);
                    var file = await picker.PickSingleFileAsync();
                    if (file == null) data = null;
                    else { var size = (await file.GetBasicPropertiesAsync()).Size; if (size > StudioProjectBundle.MaxArchiveBytes) throw new StudioPersistenceException("工程包超过 512 KiB 限制。"); using var input = await file.OpenStreamForReadAsync(); using var bytes = new MemoryStream(); var buffer = new byte[4096]; int read;
                        while ((read = await input.ReadAsync(buffer)) != 0) { if (bytes.Length + read > StudioProjectBundle.MaxArchiveBytes) throw new StudioPersistenceException("工程包超过限制。"); bytes.Write(buffer, 0, read); } data = bytes.ToArray(); }
                }
                result = data == null ? new { cancelled = true } : (object)new { payload = await Task.Run(() => StudioProjectBundle.Parse(data)) };
            } else if (op == "export") {
                var payload = m.GetProperty("payload").Clone(); var data = await Task.Run(() => StudioProjectBundle.Export(payload, ClientSettings.Version));
                var cancelled = false;
                if (BundleExportFixture != null && Aura_WinUI.Validation.StudioPolishValidation.Requested) await BundleExportFixture(data);
                else {
                    var picker = new FileSavePicker { SuggestedFileName = payload.GetProperty("project").GetProperty("name").GetString() };
                    picker.FileTypeChoices.Add("Aura 光效工程", [".auraeffect"]); WinRT.Interop.InitializeWithWindow.Initialize(picker, hwnd);
                    var file = await picker.PickSaveFileAsync();
                    if (file == null) cancelled = true;
                    else { using var output = await file.OpenStreamForWriteAsync(); output.SetLength(0); await output.WriteAsync(data); await output.FlushAsync(); }
                }
                result = new { cancelled, text = cancelled ? "导出已取消。" : "已导出工程源文件包；不包含密钥或运行记录。" };
            } else throw new StudioPersistenceException("未知工程包操作。");
            PostAssistant(new { type = "studio_bundle_result", request_id = id, result });
        } catch (Exception ex) when (ex is JsonException or KeyNotFoundException or InvalidOperationException or ArgumentException or IOException or UnauthorizedAccessException or StudioPersistenceException) {
            PostAssistant(new { type = "studio_bundle_result", request_id = id, error = ex is StudioPersistenceException ? ex.Message : "工程包操作失败；未改变已有工程。" });
        } finally { if (ownsBusy) _bundleBusy = false; }
        return true;
    }
}
