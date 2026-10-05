using System.Text.Json;

namespace Aura_WinUI.Services;

public sealed record StudioRecentDocument(int SchemaVersion, string[] Projects);

// Logical project identities only. Missing/deleted IDs are pruned against the
// current authoritative project catalog; this never opens paths or edits config.
public sealed class StudioRecentStore(string dataRoot, Action<string>? beforeReplace = null)
{
    private readonly string _path = Path.Combine(Path.GetFullPath(dataRoot), "Studio", "recent.json");
    private readonly object _gate = new();
    private string[] Read() {
        if (!File.Exists(_path)) return [];
        if (new FileInfo(_path).Length > 8192) throw new StudioPersistenceException("最近工程记录超过限制。");
        try {
            var document = JsonSerializer.Deserialize<StudioRecentDocument>(File.ReadAllBytes(_path), StudioDraftStore.Options);
            if (document == null || document.SchemaVersion != 1 || document.Projects == null || document.Projects.Length > 10 || document.Projects.Distinct(StringComparer.Ordinal).Count() != document.Projects.Length) throw new StudioPersistenceException("最近工程记录无效；原文件保留。");
            foreach (var id in document.Projects) StudioDraft.Identity(id);
            return document.Projects;
        } catch (JsonException) { throw new StudioPersistenceException("最近工程记录不可读取；原文件保留。"); }
    }
    private void Write(string[] projects) {
        Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
        var temporary = _path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try {
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.WriteThrough)) {
                stream.Write(JsonSerializer.SerializeToUtf8Bytes(new StudioRecentDocument(1, projects), StudioDraftStore.Options)); stream.Flush(true);
            }
            beforeReplace?.Invoke(temporary); File.Move(temporary, _path, true);
        } finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
    public IReadOnlyList<string> Order(IEnumerable<string> available, string? opened = null) {
        lock (_gate) {
            var catalog = available.Distinct(StringComparer.Ordinal).Take(201).ToArray();
            if (catalog.Length > 200) throw new StudioPersistenceException("工程数量超过限制。");
            // Existing legacy names may not be safe persistence identities. Keep
            // them in the UI catalog without adding them to the recent file.
            var original = Read(); var recent = original.Where(catalog.Contains).ToList();
            if (opened != null && catalog.Contains(opened)) {
                StudioDraft.Identity(opened); recent.Remove(opened); recent.Insert(0, opened);
            }
            var bounded = recent.Take(10).ToArray(); if (!original.SequenceEqual(bounded)) Write(bounded);
            return bounded.Concat(catalog.Where(id => !bounded.Contains(id))).ToArray();
        }
    }
    public void Clear() { lock (_gate) Write([]); }
}
