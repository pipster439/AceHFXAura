using System.Text.Json;

namespace Aura_WinUI.Services;

public sealed class StudioShellModel
{
    public string Name { get; private set; } = "工作室";
    public string WorkType { get; private set; } = "effect";
    public string Validation { get; private set; } = "等待编辑器";
    public string Build { get; private set; } = "尚未构建";
    public string Plugin { get; private set; } = "未发布";
    public string Lifecycle { get; private set; } = "草稿";
    public bool Playing { get; private set; }
    public bool Busy { get; private set; }
    public string Diagnostics { get; private set; } = "";
    public IReadOnlyList<string> Projects { get; private set; } = [];
    public static bool TrustedSource(string source) => Uri.TryCreate(source, UriKind.Absolute, out var uri) &&
        uri.Scheme == "http" && uri.Host == "127.0.0.1" && uri.Port == 19898;
    public static string Command(string command, string? name = null)
    {
        if (command is not ("new" or "open" or "select" or "save" or "preview" or "validate" or "build" or "publish" or "details" or "bench"))
            throw new ArgumentOutOfRangeException(nameof(command));
        if (name?.Length > 128) throw new ArgumentOutOfRangeException(nameof(name));
        return name == null ? JsonSerializer.Serialize(new { type = "studio_command", command }) :
            JsonSerializer.Serialize(new { type = "studio_command", command, name });
    }
    public bool Receive(string json)
    {
        if (json.Length > 65536) return false;
        try
        {
            using var doc = JsonDocument.Parse(json);
            var m = doc.RootElement;
            if (m.GetProperty("type").GetString() != "studio_state") return false;
            var name = Text(m, "name", 128); var kind = Text(m, "workType", 24);
            if (kind is not ("effect" or "orchestration")) return false;
            var validation = Text(m, "validation", 2048); var build = Text(m, "build", 256);
            var lifecycle = Text(m, "lifecycle", 128); var plugin = Text(m, "plugin", 256); var diagnostics = Text(m, "diagnostics", 16384);
            var projects = m.GetProperty("projects").EnumerateArray().Select(x => x.GetString()!).ToArray();
            if (projects.Length > 200 || projects.Any(x => x == null || x.Length > 128)) return false;
            var playing = m.GetProperty("playing").GetBoolean(); var busy = m.GetProperty("busy").GetBoolean();
            Name = name; WorkType = kind; Validation = validation; Build = build; Lifecycle = lifecycle;
            Diagnostics = diagnostics; Plugin = plugin;
            Projects = projects; Playing = playing; Busy = busy;
            return true;
        }
        catch (Exception ex) when (ex is JsonException or KeyNotFoundException or InvalidOperationException or ArgumentException) { return false; }
    }
    private static string Text(JsonElement m, string key, int max)
    {
        var value = m.GetProperty(key).GetString();
        return value != null && value.Length <= max ? value : throw new ArgumentException(key);
    }
}
