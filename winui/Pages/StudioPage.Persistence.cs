using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed partial class StudioPage
{
    private StudioDraftStore? _draftStore;
    private StudioAutosaveCoordinator? _autosave;
    private StudioDraftStore DraftStore {
        get {
            if (_draftStore == null) {
                _draftStore = new(RuntimeLayoutResolver.DataRoot); _autosave = new(_draftStore);
                _autosave.Status += (name, text) => DispatcherQueue.TryEnqueue(() => PostStorage(new { type = "studio_storage_status", name, text }));
            }
            return _draftStore;
        }
    }
    private void PostStorage(object value) => PostAssistant(JsonSerializer.SerializeToElement(value, StudioDraftStore.Options));
    private async Task<bool> ReceivePersistenceAsync(string json)
    {
        if (json.Length > 300000) return false;
        string? id = null;
        try {
            using var document = JsonDocument.Parse(json, new JsonDocumentOptions { MaxDepth = 64 }); var m = document.RootElement;
            if (!m.TryGetProperty("type", out var t) || t.GetString() != "studio_storage") return false;
            var allowed = new[] { "type", "request_id", "operation", "draft", "reason", "snapshot_id" };
            if (m.EnumerateObject().Any(p => !allowed.Contains(p.Name))) throw new StudioPersistenceException("未知草稿请求字段。");
            id = m.GetProperty("request_id").GetString();
            if (id == null || !System.Text.RegularExpressions.Regex.IsMatch(id, "^[a-f0-9]{32}$")) return true;
            var draft = StudioDraft.Parse(m.GetProperty("draft")); var operation = m.GetProperty("operation").GetString(); var store = DraftStore;
            var reason = m.TryGetProperty("reason", out var r) ? r.GetString() : null;
            var snapshotId = m.TryGetProperty("snapshot_id", out var s) ? s.GetString() : null;
            var response = await Task.Run<object>(() => {
                switch (operation) {
                    case "observe":
                        DateTimeOffset? savedTime = null;
                        if (File.Exists(RuntimeLayoutResolver.Resolve().ConfigPath)) {
                            using var config = JsonDocument.Parse(File.ReadAllBytes(RuntimeLayoutResolver.Resolve().ConfigPath));
                            savedTime = StudioDraftStore.DurableTimestamp(config.RootElement, draft.Name);
                        }
                        return new { recovery = store.ObserveSaved(draft, savedTime), snapshots = SnapshotMetadata(store, draft.Name) };
                    case "autosave": _autosave!.Queue(draft); return new { text = "正在等待自动保存…" };
                    case "saved": _autosave!.Saved(draft); return new { text = "草稿已保存。", snapshots = SnapshotMetadata(store, draft.Name) };
                    case "snapshot": store.Snapshot(draft, reason!); return new { snapshots = SnapshotMetadata(store, draft.Name) };
                    case "list": return new { snapshots = SnapshotMetadata(store, draft.Name) };
                    case "restore_recovery": _autosave!.Cancel(draft.Name); return new { draft = store.RestoreRecovery(draft.Name) };
                    case "discard": _autosave!.Cancel(draft.Name); store.Discard(draft.Name); return new { text = "可恢复草稿已放弃。" };
                    case "restore_snapshot": return new { draft = store.RestoreSnapshot(draft.Name, snapshotId!) };
                    default: throw new StudioPersistenceException("未知草稿操作。");
                }
            });
            PostStorage(new { type = "studio_storage_result", request_id = id, result = response });
        } catch (Exception ex) when (ex is JsonException or KeyNotFoundException or InvalidOperationException or ArgumentException or IOException or UnauthorizedAccessException or StudioPersistenceException) {
            PostStorage(new { type = "studio_storage_result", request_id = id, error = ex is StudioPersistenceException ? ex.Message : "草稿存储不可用；原有文件保留。" });
        }
        return true;
    }
    private static object SnapshotMetadata(StudioDraftStore store, string name) => store.Snapshots(name).Select(s => new { id = s.Id, reason = s.Reason, created_utc = s.CreatedUtc }).ToArray();
    public static async Task FlushDraftsAsync() {
        if (_host?._autosave is not { } saver) return;
        try { await Task.Run(saver.Flush); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or StudioPersistenceException) { _host?.PostAssistant(new { type = "studio_storage_status", name = _host._shell.Name, text = "退出时自动保存失败；请检查草稿恢复文件。" }); }
    }
}
