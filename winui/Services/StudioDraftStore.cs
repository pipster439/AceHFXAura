using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Aura_WinUI.Services;

public sealed class StudioPersistenceException(string message) : Exception(message);
public sealed record StudioDraft(string Name, JsonElement Json, JsonElement Publication)
{
    public static void Identity(string name) {
        if (string.IsNullOrEmpty(name) || !Regex.IsMatch(name, "^[A-Za-z_][A-Za-z0-9_]{0,47}$")) throw new StudioPersistenceException("工程标识无效。");
    }
    public static StudioDraft Parse(JsonElement value) {
        Exact(value, "name", "json", "publication");
        var draft = new StudioDraft(value.GetProperty("name").GetString()!, value.GetProperty("json").Clone(), value.GetProperty("publication").Clone());
        draft.Validate(); return draft;
    }
    internal static void Exact(JsonElement value, params string[] fields) {
        if (value.ValueKind != JsonValueKind.Object || value.EnumerateObject().Count() != fields.Length ||
            value.EnumerateObject().Any(p => !fields.Contains(p.Name))) throw new StudioPersistenceException("草稿字段无效。");
    }
    public void Validate() {
        Identity(Name); Exact(Publication, "mode", "fade_out_ms");
        var mode = Publication.GetProperty("mode").GetString(); var fade = Publication.GetProperty("fade_out_ms").GetInt32();
        if (mode is not ("continuous" or "one_shot") || fade is < 0 or > 60000 || mode == "continuous" && fade != 0 ||
            Json.ValueKind != JsonValueKind.Object || !Json.TryGetProperty("blocks", out _)) throw new StudioPersistenceException("草稿结构或播放方式无效。");
        var bytes = Encoding.UTF8.GetByteCount(Json.GetRawText());
        if (bytes > 262144) throw new StudioPersistenceException("草稿超过 256 KiB 限制。");
        var count = 0;
        void Scan(JsonElement node, int depth) {
            if (depth > 48 || ++count > 16000) throw new StudioPersistenceException("草稿层级或字段数量超过限制。");
            if (node.ValueKind == JsonValueKind.Object) foreach (var p in node.EnumerateObject()) {
                if (Regex.IsMatch(p.Name, "(?i)api[_ -]?key|credential|password|token|secret|diagnostic|compiler_path|applied_|published_"))
                    throw new StudioPersistenceException("恢复文件不能包含密钥、诊断或发布记录。");
                // Opaque Blockly IDs may contain path-looking punctuation.
                // They never select files or enter diagnostics.
                if (p.Name == "id" && p.Value.ValueKind == JsonValueKind.String) {
                    var id = p.Value.GetString()!;
                    if (id.Length > 128 || Regex.IsMatch(id, @"(?i)Bearer\s+\S+|sk-[a-z0-9_-]{8,}|(?:api[_ -]?key|password|token|secret)\s*[:=]")) throw new StudioPersistenceException("草稿标识包含密钥或超过限制。");
                } else Scan(p.Value, depth + 1);
            }
            else if (node.ValueKind == JsonValueKind.Array) foreach (var item in node.EnumerateArray()) Scan(item, depth + 1);
            else if (node.ValueKind == JsonValueKind.String) {
                var text = node.GetString()!;
                if (Regex.IsMatch(text, @"(?i)Bearer\s+\S+|sk-[a-z0-9_-]{8,}|(?:api[_ -]?key|password|token|secret)\s*[:=]|[A-Z]:[\\/]|\\\\|/(?:home|Users|tmp|var)/"))
                    throw new StudioPersistenceException("草稿包含密钥或本地路径，恢复保存已拒绝。");
            }
        }
        Scan(Json, 0);
    }
    // Block IDs are editor implementation details, regenerated for old presets.
    // Preserve coordinates, fields, variable references and lifecycle in the hash.
    public string Fingerprint() {
        Validate(); using var bytes = new MemoryStream(); using (var writer = new Utf8JsonWriter(bytes)) {
            void Canonical(JsonElement value) {
                if (value.ValueKind == JsonValueKind.Object) {
                    writer.WriteStartObject(); foreach (var p in value.EnumerateObject().OrderBy(p => p.Name, StringComparer.Ordinal)) {
                        if (p.Name == "id" && value.TryGetProperty("type", out _)) continue;
                        writer.WritePropertyName(p.Name); Canonical(p.Value);
                    } writer.WriteEndObject();
                } else if (value.ValueKind == JsonValueKind.Array) { writer.WriteStartArray(); foreach (var v in value.EnumerateArray()) Canonical(v); writer.WriteEndArray(); }
                else value.WriteTo(writer);
            }
            writer.WriteStartObject(); writer.WriteString("name", Name); writer.WritePropertyName("json"); Canonical(Json);
            writer.WritePropertyName("publication"); Canonical(Publication); writer.WriteEndObject();
        }
        return Convert.ToHexString(SHA256.HashData(bytes.ToArray()));
    }
}
public sealed record StudioRecovery(long Revision, DateTimeOffset UpdatedUtc, string SavedHash, string DraftHash, StudioDraft Draft);
public sealed record StudioSnapshot(string Id, string Reason, DateTimeOffset CreatedUtc, StudioDraft Draft);
public sealed record StudioJournal(int SchemaVersion, string Project, StudioRecovery? Recovery, List<StudioSnapshot> Snapshots, long Revision = 0);

// Separate draft journal. This class never reads/writes the daemon config or any
// applied/plugin metadata. Every filename comes from a validated logical ID.
public sealed class StudioDraftStore(string dataRoot, Action<string>? beforeReplace = null, TimeProvider? time = null)
{
    private readonly object _gate = new();
    private readonly string _root = Path.Combine(Path.GetFullPath(dataRoot), "Studio", "drafts");
    private readonly Dictionary<string, string> _saved = new(StringComparer.Ordinal);
    private readonly HashSet<string> _offered = new(StringComparer.Ordinal);
    private readonly TimeProvider _time = time ?? TimeProvider.System;
    internal static readonly JsonSerializerOptions Options = new() { PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower, MaxDepth = 64, UnmappedMemberHandling = System.Text.Json.Serialization.JsonUnmappedMemberHandling.Disallow };
    public static DateTimeOffset? DurableTimestamp(JsonElement config, string name) {
        StudioDraft.Identity(name);
        if (config.ValueKind != JsonValueKind.Object || !config.TryGetProperty("blockly_effects", out var effects) || effects.ValueKind != JsonValueKind.Object || !effects.TryGetProperty(name, out var effect) || effect.ValueKind != JsonValueKind.Object) return null;
        foreach (var field in new[] { "source_updated_at", "updated_at" }) {
            if (effect.TryGetProperty(field, out var value) && value.ValueKind == JsonValueKind.Number && value.TryGetInt64(out var ms) && ms is > 0 and <= 253402300799999) return DateTimeOffset.FromUnixTimeMilliseconds(ms);
        }
        // Legacy projects lack a per-effect save timestamp. Their saved graph
        // fingerprint remains the baseline; unrelated config mtime is irrelevant.
        return null;
    }
    public string JournalPath(string name) {
        StudioDraft.Identity(name);
        // Windows device names (CON/NUL/COM1) and case-insensitive filenames
        // must not change the case-sensitive logical project identity.
        var file = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(name))).ToLowerInvariant();
        return Path.Combine(_root, file + ".json");
    }
    private StudioJournal Read(string name) {
        var path = JournalPath(name); if (!File.Exists(path)) return new(1, name, null, []);
        if (new FileInfo(path).Length > 3100000) throw new StudioPersistenceException("草稿日志超过限制。");
        try {
            var j = JsonSerializer.Deserialize<StudioJournal>(File.ReadAllBytes(path), Options)!;
            if (j == null || j.SchemaVersion != 1 || j.Project != name || j.Snapshots == null || j.Snapshots.Count > 10 || j.Revision < 0)
                throw new StudioPersistenceException("草稿日志无效；原文件保留。");
            if (j.Recovery != null) { j.Recovery.Draft.Validate(); if (j.Recovery.Draft.Name != name || j.Recovery.Draft.Fingerprint() != j.Recovery.DraftHash) throw new StudioPersistenceException("恢复草稿校验失败。"); }
            foreach (var s in j.Snapshots) { s.Draft.Validate(); if (s.Draft.Name != name || !Regex.IsMatch(s.Id, "^[a-f0-9]{32}$") || s.Reason is not ("before_ai_apply" or "before_publish")) throw new StudioPersistenceException("快照无效。"); }
            return j;
        } catch (JsonException) { throw new StudioPersistenceException("草稿日志格式无效；原文件保留。"); }
    }
    private void Write(StudioJournal journal) {
        Directory.CreateDirectory(_root);
        var path = JournalPath(journal.Project);
        if (!File.Exists(path) && Directory.EnumerateFiles(_root, "*.json").Take(201).Count() >= 200) throw new StudioPersistenceException("恢复工程数量已达到 200 上限。");
        var data = JsonSerializer.SerializeToUtf8Bytes(journal, Options);
        if (data.Length > 3100000) throw new StudioPersistenceException("草稿日志超过限制。");
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try {
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.WriteThrough)) { stream.Write(data); stream.Flush(true); }
            beforeReplace?.Invoke(temporary);
            File.Move(temporary, path, true);
        } finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
    public StudioRecovery? ObserveSaved(StudioDraft draft, DateTimeOffset? durableTimestamp = null) {
        lock (_gate) {
            var hash = draft.Fingerprint(); _saved[draft.Name] = hash; var journal = Read(draft.Name);
            var recovery = journal.Recovery is { } entry && entry.DraftHash != hash &&
                (durableTimestamp == null || entry.UpdatedUtc > durableTimestamp) ? entry : null;
            if (recovery != null) _offered.Add(draft.Name); else _offered.Remove(draft.Name);
            return recovery;
        }
    }
    public void Autosave(StudioDraft draft) {
        lock (_gate) {
            if (!_saved.TryGetValue(draft.Name, out var saved)) throw new StudioPersistenceException("尚未读取保存版本，自动保存已暂停。");
            if (_offered.Contains(draft.Name)) throw new StudioPersistenceException("请先恢复或放弃可恢复草稿。");
            var hash = draft.Fingerprint(); var j = Read(draft.Name); var revision = j.Revision + 1;
            Write(j with { Revision = revision, Recovery = hash == saved ? null : new(revision, _time.GetUtcNow(), saved, hash, draft) });
        }
    }
    public void MarkSaved(StudioDraft draft) {
        lock (_gate) { _saved[draft.Name] = draft.Fingerprint(); _offered.Remove(draft.Name); var j = Read(draft.Name); Write(j with { Recovery = null, Revision = j.Revision + 1 }); }
    }
    public void Discard(string name) {
        lock (_gate) { var j = Read(name); Write(j with { Recovery = null, Revision = j.Revision + 1 }); _offered.Remove(name); }
    }
    public StudioDraft RestoreRecovery(string name) {
        lock (_gate) { var value = Read(name).Recovery ?? throw new StudioPersistenceException("没有可恢复草稿。"); _offered.Remove(name); return value.Draft; }
    }
    public IReadOnlyList<StudioSnapshot> Snapshots(string name) { lock (_gate) return Read(name).Snapshots.ToArray(); }
    public StudioSnapshot Snapshot(StudioDraft draft, string reason) {
        if (reason is not ("before_ai_apply" or "before_publish")) throw new StudioPersistenceException("快照来源无效。");
        draft.Validate(); lock (_gate) {
            var j = Read(draft.Name); var s = new StudioSnapshot(Guid.NewGuid().ToString("N"), reason, _time.GetUtcNow(), draft);
            Write(j with { Snapshots = new[] { s }.Concat(j.Snapshots).Take(10).ToList() }); return s;
        }
    }
    public StudioDraft RestoreSnapshot(string name, string id) {
        lock (_gate) return Read(name).Snapshots.SingleOrDefault(s => s.Id == id)?.Draft ?? throw new StudioPersistenceException("快照不存在或已过期。");
    }
}

public sealed class StudioAutosaveCoordinator(StudioDraftStore store, Func<TimeSpan, CancellationToken, Task>? delay = null, TimeProvider? time = null)
{
    public const int DebounceMs = 800, MaxWaitMs = 5000;
    private readonly object _gate = new();
    private readonly Dictionary<string, (StudioDraft Draft, CancellationTokenSource Cancel, long FirstTimestamp)> _pending = new();
    private readonly Func<TimeSpan, CancellationToken, Task> _delay = delay ?? Task.Delay;
    private readonly TimeProvider _time = time ?? TimeProvider.System;
    public event Action<string, string>? Status;
    public void Queue(StudioDraft draft) {
        draft.Validate(); CancellationTokenSource cancel = new(); TimeSpan wait;
        lock (_gate) {
            var first = _time.GetTimestamp();
            if (_pending.Remove(draft.Name, out var old)) { first = old.FirstTimestamp; old.Cancel.Cancel(); }
            if (_pending.Count >= 200) throw new StudioPersistenceException("待保存工程数量超过限制。");
            wait = TimeSpan.FromMilliseconds(Math.Max(0, Math.Min(DebounceMs, MaxWaitMs - _time.GetElapsedTime(first).TotalMilliseconds)));
            _pending[draft.Name] = (draft, cancel, first);
        }
        _ = PersistAsync(draft.Name, cancel, wait);
    }
    private async Task PersistAsync(string name, CancellationTokenSource cancel, TimeSpan wait) {
        try {
            await _delay(wait, cancel.Token);
            lock (_gate) {
                if (!_pending.TryGetValue(name, out var pending) || pending.Cancel != cancel) return;
                store.Autosave(pending.Draft); _pending.Remove(name);
            }
            Status?.Invoke(name, "草稿已自动保存。");
        } catch (OperationCanceledException) when (cancel.IsCancellationRequested) { }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or StudioPersistenceException) {
            lock (_gate) { if (_pending.TryGetValue(name, out var pending) && pending.Cancel == cancel) _pending.Remove(name); }
            Status?.Invoke(name, "自动保存失败；草稿保留在编辑器中。");
        }
        finally { cancel.Dispose(); }
    }
    public void Saved(StudioDraft draft) { lock (_gate) { Cancel(draft.Name); store.MarkSaved(draft); } }
    public void Cancel(string name) { lock (_gate) { if (_pending.Remove(name, out var old)) old.Cancel.Cancel(); } }
    public void Flush() {
        lock (_gate) {
            var entries = _pending.Values.ToArray(); foreach (var entry in entries) entry.Cancel.Cancel();
            _pending.Clear();
            Exception? error = null;
            foreach (var entry in entries) {
                try { store.Autosave(entry.Draft); }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or StudioPersistenceException) { error = ex; }
            }
            if (error != null) throw new StudioPersistenceException("退出时部分草稿未能自动保存。");
        }
    }
}
