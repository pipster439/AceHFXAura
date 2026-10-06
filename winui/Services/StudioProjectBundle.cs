using System.IO.Compression;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Aura_WinUI.Services;

// Source-only, two fixed JSON members. Never extracts, executes, compiles or
// touches config. Graph capabilities are verified by the retained JS owner
// before a summary/confirmation or export request is permitted.
public static class StudioProjectBundle
{
    public const int MaxArchiveBytes = 524288, MaxProjectBytes = 270336, MaxManifestBytes = 16384;
    private static readonly string[] Members = ["manifest.json", "project.json"];
    public static JsonElement Parse(byte[] archive) {
        if (archive.Length is < 22 or > MaxArchiveBytes) throw Bad("工程包大小超过限制或无效。");
        try {
            using var stream = new MemoryStream(archive, false);
            using var zip = new ZipArchive(stream, ZipArchiveMode.Read);
            if (zip.Entries.Count != 2 || zip.Entries.Select(e => e.FullName).Distinct(StringComparer.OrdinalIgnoreCase).Count() != 2) throw Bad("工程包成员数量或重复路径无效。");
            var content = new Dictionary<string, JsonElement>(); long expanded = 0;
            foreach (var entry in zip.Entries) {
                var mode = ((uint)entry.ExternalAttributes >> 16) & 0xf000;
                if (!Members.Contains(entry.FullName) || mode is not (0 or 0x8000) || (entry.ExternalAttributes & 0x410) != 0) throw Bad("工程包路径、链接或成员类型无效。");
                var bound = entry.FullName == "manifest.json" ? MaxManifestBytes : MaxProjectBytes;
                expanded = checked(expanded + entry.Length);
                if (entry.Length is <= 0 || entry.Length > bound || expanded > MaxManifestBytes + MaxProjectBytes) throw Bad("工程包解压大小超过限制。");
                using var input = entry.Open(); using var data = new MemoryStream(); var buffer = new byte[4096]; int read;
                while ((read = input.Read(buffer)) != 0) { if (data.Length + read > bound) throw Bad("工程包解压大小超过限制。"); data.Write(buffer, 0, read); }
                if (data.Length != entry.Length) throw Bad("工程包成员长度不一致。");
                using var doc = JsonDocument.Parse(data.ToArray(), new JsonDocumentOptions { MaxDepth = 64 });
                NoDuplicateJson(doc.RootElement); content[entry.FullName] = doc.RootElement.Clone();
            }
            var payload = JsonSerializer.SerializeToElement(new { manifest = content["manifest.json"], project = content["project.json"] });
            Validate(payload); return payload;
        } catch (Exception ex) when (ex is InvalidDataException or JsonException or OverflowException or KeyNotFoundException or ArgumentException) { throw Bad("工程包格式无效；未导入任何工程。"); }
    }
    public static byte[] Export(JsonElement payload, string version) {
        Validate(payload); SafeText(version, 80);
        var manifest = payload.GetProperty("manifest").EnumerateObject().ToDictionary(p => p.Name, p => p.Value.Clone());
        manifest["created_with_version"] = JsonSerializer.SerializeToElement(version);
        using var output = new MemoryStream();
        using (var zip = new ZipArchive(output, ZipArchiveMode.Create, true)) {
            foreach (var name in Members) {
                using var entry = zip.CreateEntry(name, CompressionLevel.Optimal).Open();
                entry.Write(name == "manifest.json" ? JsonSerializer.SerializeToUtf8Bytes(manifest) : JsonSerializer.SerializeToUtf8Bytes(payload.GetProperty("project")));
            }
        }
        var result = output.ToArray(); if (result.Length > MaxArchiveBytes) throw Bad("工程包超过限制。"); return result;
    }
    public static void Validate(JsonElement payload) {
        try {
            NoDuplicateJson(payload); StudioDraft.Exact(payload, "manifest", "project");
            var m = payload.GetProperty("manifest");
            var required = new[] { "schema_version", "name", "description", "tags", "capabilities", "created_with_version" };
            if (m.ValueKind != JsonValueKind.Object || required.Any(k => !m.TryGetProperty(k, out _)) || m.EnumerateObject().Any(p => !required.Contains(p.Name) && p.Name != "author") || m.GetProperty("schema_version").GetInt32() != 1 || Encoding.UTF8.GetByteCount(m.GetRawText()) > MaxManifestBytes) throw Bad("工程包 manifest 无效。");
            var draft = StudioDraft.Parse(payload.GetProperty("project"));
            if (m.GetProperty("name").GetString() != draft.Name || Encoding.UTF8.GetByteCount(payload.GetProperty("project").GetRawText()) > MaxProjectBytes) throw Bad("工程包名称或工程大小无效。");
            SafeText(m.GetProperty("description").GetString()!, 1024); SafeText(m.GetProperty("created_with_version").GetString()!, 80);
            if (m.TryGetProperty("author", out var author)) SafeText(author.GetString()!, 80);
            Strings(m.GetProperty("tags"), 12, 40);
            var c = m.GetProperty("capabilities"); StudioDraft.Exact(c, "schema_version", "inputs", "outputs", "features", "gsi_fields", "diagnostics");
            if (c.GetProperty("schema_version").GetInt32() != 1) throw Bad("能力 schema 无效。");
            Strings(c.GetProperty("inputs"), 4, 32, ["keyboard", "cs2_gsi", "foreground_process", "time"]);
            Strings(c.GetProperty("outputs"), 1, 32, ["keyboard_rgb"]);
            Strings(c.GetProperty("features"), 3, 32, ["stateful", "event_driven", "simulation_supported"]);
            Strings(c.GetProperty("gsi_fields"), 200, 128);
            Strings(c.GetProperty("diagnostics"), 0, 80);
            foreach (var path in c.GetProperty("gsi_fields").EnumerateArray()) if (!Regex.IsMatch(path.GetString()!, "^[a-z][a-z0-9_.]{0,127}$")) throw Bad("GSI 能力字段无效。");
            void Scan(JsonElement n) {
                if (n.ValueKind == JsonValueKind.Object) foreach (var p in n.EnumerateObject()) { if (Regex.IsMatch(p.Name, "^(code|script|source_code|binary|dll|exe)$", RegexOptions.IgnoreCase)) throw Bad("工程包不能包含脚本或编译产物。"); Scan(p.Value); }
                else if (n.ValueKind == JsonValueKind.Array) foreach (var v in n.EnumerateArray()) Scan(v);
                else if (n.ValueKind == JsonValueKind.String) SafeText(n.GetString()!, MaxProjectBytes);
            }
            Scan(draft.Json);
        } catch (Exception ex) when (ex is JsonException or InvalidOperationException or KeyNotFoundException or FormatException) { throw Bad("工程包字段类型无效。"); }
    }
    private static void NoDuplicateJson(JsonElement n) {
        if (n.ValueKind == JsonValueKind.Object) { var keys = new HashSet<string>(StringComparer.Ordinal); foreach (var p in n.EnumerateObject()) { if (!keys.Add(p.Name)) throw Bad("工程包 JSON 字段重复。"); NoDuplicateJson(p.Value); } }
        else if (n.ValueKind == JsonValueKind.Array) foreach (var v in n.EnumerateArray()) NoDuplicateJson(v);
    }
    private static void Strings(JsonElement a, int count, int length, string[]? allowed = null) {
        if (a.ValueKind != JsonValueKind.Array || a.GetArrayLength() > count) throw Bad("工程包列表超过限制。");
        var values = new HashSet<string>(StringComparer.Ordinal);
        foreach (var v in a.EnumerateArray()) { var text = v.GetString()!; SafeText(text, length); if (string.IsNullOrWhiteSpace(text) || !values.Add(text) || allowed != null && !allowed.Contains(text)) throw Bad("工程包列表值无效。"); }
    }
    private static void SafeText(string value, int length) {
        if (value == null || value.Length > length || Regex.IsMatch(value, @"(?i)(?<![a-z0-9_])sk-[a-z0-9_-]{8,}|Bearer\s+\S+|(?:api[_ -]?key|password|token|secret)\s*[:=]|[a-z]:[\\/]|\\\\|/(?:home|Users|tmp|var)/|<script")) throw Bad("工程包包含密钥、本地路径或非法文本。");
    }
    private static StudioPersistenceException Bad(string message) => new(message);
}
