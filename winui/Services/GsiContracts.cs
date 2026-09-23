using System.Text.Json;
using System.Text.Json.Serialization;

namespace Aura_WinUI.Services;

public sealed class RuntimeIdentityDto
{
    [JsonPropertyName("service")] public string Service { get; set; } = "";
    [JsonPropertyName("process_id")] public int ProcessId { get; set; }
    [JsonPropertyName("instance_id")] public string InstanceId { get; set; } = "";
    [JsonPropertyName("product_version")] public string ProductVersion { get; set; } = "";
    [JsonPropertyName("config_path")] public string ConfigPath { get; set; } = "";
}
public sealed class StudioWebStateDto { public bool Suppressed { get; set; } }
public sealed record ApiResult<T>(T? Value, string Error = "", int StatusCode = 0)
{
    public bool IsSuccess => Value != null && Error.Length == 0;
}
public sealed class GsiFreshness
{
    public bool Fresh { get; set; }
    public ulong? AgeMs { get; set; }
    public ulong ThresholdMs { get; set; }
    public ulong EvaluatedAtMs { get; set; }
}
public sealed class GsiCurrent
{
    public int GsiApiVersion { get; set; }
    public string InstanceId { get; set; } = "";
    public string Source { get; set; } = "";
    public bool Connected { get; set; }
    public bool IsCs2Foreground { get; set; }
    public double LastUpdatedSec { get; set; } = -1;
    public string ForegroundProcess { get; set; } = "";
    public GsiFreshness? Freshness { get; set; }
    public Dictionary<string, JsonElement> Data { get; set; } = new();
    public string Field(params string[] keys)
    {
        foreach (var key in keys) if (Data.TryGetValue(key, out var value) && value.ValueKind != JsonValueKind.Null) return value.ToString();
        return "—";
    }
}
public sealed class SimulationState
{
    public int GsiApiVersion { get; set; }
    public string InstanceId { get; set; } = "";
    public bool Enabled { get; set; }
    public string Source { get; set; } = "";
    public bool Heartbeat { get; set; }
    public int HeartbeatMs { get; set; }
    public ulong AppliedSequence { get; set; }
    public int Pending { get; set; }
    public string ForegroundProcess { get; set; } = "";
    public GsiFreshness? Freshness { get; set; }
    public JsonElement Payload { get; set; }
}
public sealed class SimulationPatch
{
    public bool? Enabled { get; set; }
    public bool? Heartbeat { get; set; }
    public string? ForegroundProcess { get; set; }
    public int? Health { get; set; }
    public int? Armor { get; set; }
    public int? RoundKills { get; set; }
    public string? Bomb { get; set; }
    public string? RoundPhase { get; set; }
    public bool? IncrementKill { get; set; }
}
public sealed class QueuedSimulation
{
    public string Status { get; set; } = "";
    public string InstanceId { get; set; } = "";
    public ulong Sequence { get; set; }
}
public sealed class WebStatus
{
    public string Service { get; set; } = "";
    public int WebApiVersion { get; set; }
    public string DaemonInstanceId { get; set; } = "";
    public bool StudioPublishReady { get; set; }
    public bool SdkHeadersFound { get; set; }
    public bool MsvcFound { get; set; }
}
public sealed class GsiCfgPath
{
    public string Path { get; set; } = "";
    public bool Exists { get; set; }
    public string TemplateMatch { get; set; } = "";
    public string Revision { get; set; } = "";
    public override string ToString() => Path;
}
public sealed class GsiCfgInfo
{
    public int GsiApiVersion { get; set; }
    public List<GsiCfgPath> Paths { get; set; } = new();
}
public sealed class GsiInstallResult
{
    public string Status { get; set; } = "";
    public string Path { get; set; } = "";
}

public static class GsiPresentation
{
    public static string Label(string value) => value switch {
        "carried" => "携带中", "dropped" => "已掉落", "planting" => "正在安放", "planted" => "已安放",
        "defusing" => "正在拆除", "defused" => "已拆除", "exploded" => "已爆炸", "freezetime" => "准备阶段",
        "live" => "进行中", "over" => "已结束", _ => value };
}
