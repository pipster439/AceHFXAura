using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Aura_WinUI.Services;

public sealed class MagneticHallKeySample
{
    [JsonPropertyName("logical_id")] public ushort LogicalId { get; set; }
    [JsonPropertyName("hid_usage")] public byte HidUsage { get; set; }
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("raw")] public ushort Raw { get; set; }
    [JsonPropertyName("depth_mm")] public double DepthMm { get; set; }
    [JsonPropertyName("normalized")] public double Normalized { get; set; }
    [JsonPropertyName("digital_down")] public bool DigitalDown { get; set; }
}

public sealed class MagneticHallSnapshot
{
    [JsonPropertyName("status")] public string Status { get; set; } = "error";
    [JsonPropertyName("api_version")] public int ApiVersion { get; set; }
    [JsonPropertyName("active")] public bool Active { get; set; }
    [JsonPropertyName("event_channel_ok")] public bool EventChannelOk { get; set; }
    [JsonPropertyName("shared_memory_ok")] public bool SharedMemoryOk { get; set; }
    [JsonPropertyName("shared_memory_name")] public string SharedMemoryName { get; set; } = "";
    [JsonPropertyName("packet_count")] public ulong PacketCount { get; set; }
    [JsonPropertyName("packet_rate_hz")] public double PacketRateHz { get; set; }
    [JsonPropertyName("last_error")] public string LastError { get; set; } = "";
    [JsonPropertyName("keys")] public List<MagneticHallKeySample> Keys { get; set; } = [];
}

public sealed class MagneticHallVerifyItem
{
    [JsonPropertyName("kind")] public string Kind { get; set; } = "";
    [JsonPropertyName("label")] public string Label { get; set; } = "";
    [JsonPropertyName("target_mm")] public double TargetMm { get; set; }
    [JsonPropertyName("measured_mm")] public double MeasuredMm { get; set; }
    [JsonPropertyName("error_mm")] public double ErrorMm { get; set; }
    [JsonPropertyName("passed")] public bool Passed { get; set; }
    [JsonPropertyName("detail")] public string Detail { get; set; } = "";
}

public sealed class MagneticHallVerifyStatus
{
    [JsonPropertyName("status")] public string Status { get; set; } = "error";
    [JsonPropertyName("api_version")] public int ApiVersion { get; set; }
    [JsonPropertyName("state")] public string State { get; set; } = "Idle";
    [JsonPropertyName("logical_id")] public ushort LogicalId { get; set; }
    [JsonPropertyName("key_name")] public string KeyName { get; set; } = "";
    [JsonPropertyName("mode")] public string Mode { get; set; } = "";
    [JsonPropertyName("instruction")] public string Instruction { get; set; } = "";
    [JsonPropertyName("step_index")] public int StepIndex { get; set; }
    [JsonPropertyName("step_total")] public int StepTotal { get; set; }
    [JsonPropertyName("live_depth_mm")] public double LiveDepthMm { get; set; }
    [JsonPropertyName("live_raw")] public ushort LiveRaw { get; set; }
    [JsonPropertyName("digital_down")] public bool DigitalDown { get; set; }
    [JsonPropertyName("bound_bit")] public string BoundBit { get; set; } = "";
    [JsonPropertyName("error")] public string Error { get; set; } = "";
    [JsonPropertyName("results")] public List<MagneticHallVerifyItem> Results { get; set; } = [];
}

public sealed class MagneticHallVerifyStartRequest
{
    [JsonPropertyName("logical_id")] public ushort LogicalId { get; set; }
    [JsonPropertyName("mode")] public string Mode { get; set; } = "actuation";
    [JsonPropertyName("actuation_points")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IReadOnlyList<double>? ActuationPoints { get; set; }
    [JsonPropertyName("rt_points")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IReadOnlyList<double>? RtPoints { get; set; }
    [JsonPropertyName("dz_top_mm")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public double? DzTopMm { get; set; }
    [JsonPropertyName("dz_bottom_mm")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public double? DzBottomMm { get; set; }
    [JsonPropertyName("dz_rt_mm")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public double? DzRtMm { get; set; }
    [JsonPropertyName("restore_actuation_mm")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public double? RestoreActuationMm { get; set; }
    [JsonPropertyName("unlock_fa")] public bool UnlockFa { get; set; }
}

public sealed class MagneticHallClient(HttpClient? http = null)
{
    private readonly HttpClient _http = http ?? new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
    private const string Base = "http://127.0.0.1:19897/api/magnetic/hall/";

    public Task<MagneticHallSnapshot> StartAsync(bool unlockFa = false) =>
        SendAsync<MagneticHallSnapshot>("start", new { unlock_fa = unlockFa },
            () => new MagneticHallSnapshot { LastError = "无法启动霍尔实时流" });

    public Task<MagneticHallSnapshot> StopAsync() =>
        SendAsync<MagneticHallSnapshot>("stop", new { },
            () => new MagneticHallSnapshot { LastError = "停止霍尔实时流失败" });

    public Task<MagneticHallSnapshot> GetSnapshotAsync() =>
        SendAsync<MagneticHallSnapshot>("snapshot", null,
            () => new MagneticHallSnapshot { LastError = "读取霍尔实时数据失败" });

    public Task<MagneticHallVerifyStatus> StartVerifyAsync(MagneticHallVerifyStartRequest request) =>
        SendAsync<MagneticHallVerifyStatus>("verify/start", request,
            () => new MagneticHallVerifyStatus { State = "Failed", Error = "启动闭环校验失败" });

    public Task<MagneticHallVerifyStatus> GetVerifyStatusAsync() =>
        SendAsync<MagneticHallVerifyStatus>("verify/status", null,
            () => new MagneticHallVerifyStatus { State = "Failed", Error = "获取闭环校验状态失败" });

    public Task<MagneticHallVerifyStatus> StopVerifyAsync() =>
        SendAsync<MagneticHallVerifyStatus>("verify/stop", new { },
            () => new MagneticHallVerifyStatus { State = "Failed", Error = "停止闭环校验失败" });

    private async Task<T> SendAsync<T>(string path, object? body, Func<T> fallback) where T : class
    {
        try
        {
            using var request = new HttpRequestMessage(body == null ? HttpMethod.Get : HttpMethod.Post, Base + path);
            if (body != null)
            {
                request.Content = new StringContent(JsonSerializer.Serialize(body), Encoding.UTF8, "application/json");
            }
            using var response = await _http.SendAsync(request).ConfigureAwait(false);
            var json = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
            return JsonSerializer.Deserialize<T>(json) ?? fallback();
        }
        catch (Exception ex)
        {
            var item = fallback();
            if (item is MagneticHallSnapshot s && string.IsNullOrEmpty(s.LastError)) s.LastError = ex.Message;
            else if (item is MagneticHallVerifyStatus v) v.Error = ex.Message;
            return item;
        }
    }
}
