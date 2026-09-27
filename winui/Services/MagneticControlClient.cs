using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Aura_WinUI.Services;

public sealed class MagneticActuationValue
{
    [JsonPropertyName("logical_id")] public ushort LogicalId { get; set; }
    [JsonPropertyName("raw")] public byte Raw { get; set; }
    [JsonPropertyName("source")] public string Source { get; set; } = "Unknown";
}

public sealed class MagneticRapidTriggerValue
{
    [JsonPropertyName("logical_id")] public ushort LogicalId { get; set; }
    [JsonPropertyName("enabled")] public bool Enabled { get; set; }
    [JsonPropertyName("press_raw")] public byte PressRaw { get; set; }
    [JsonPropertyName("release_raw")] public byte ReleaseRaw { get; set; }
    [JsonPropertyName("source")] public string Source { get; set; } = "Unknown";
}

public sealed class MagneticDeadzoneValue
{
    [JsonPropertyName("logical_id")] public ushort LogicalId { get; set; }
    [JsonPropertyName("top_raw")] public byte TopRaw { get; set; }
    [JsonPropertyName("bottom_raw")] public byte BottomRaw { get; set; }
    [JsonPropertyName("source")] public string Source { get; set; } = "Unknown";
}

public sealed class MagneticBatchKeyResult
{
    [JsonPropertyName("logical_id")] public ushort LogicalId { get; set; }
    [JsonPropertyName("status")] public string Status { get; set; } = "NotExecuted";
    [JsonPropertyName("detail")] public string? Detail { get; set; }
}

public sealed class MagneticBatchResult
{
    [JsonPropertyName("requested_count")] public int RequestedCount { get; set; }
    [JsonPropertyName("applied_count")] public int AppliedCount { get; set; }
    [JsonPropertyName("failed_count")] public int FailedCount { get; set; }
    [JsonPropertyName("not_executed_count")] public int NotExecutedCount { get; set; }
    [JsonPropertyName("completed_fully")] public bool CompletedFully { get; set; }
    [JsonPropertyName("health")] public string Health { get; set; } = "Stopped";
    [JsonPropertyName("results")] public List<MagneticBatchKeyResult> Results { get; set; } = [];
    [JsonPropertyName("configured_dks_keys")] public List<ushort> ConfiguredDksKeys { get; set; } = [];
    [JsonPropertyName("unknown_dks_keys")] public List<ushort> UnknownDksKeys { get; set; } = [];
}

public sealed class MagneticKnownRaw
{
    [JsonPropertyName("known")] public bool Known { get; set; }
    [JsonPropertyName("raw")] public byte Raw { get; set; }
    [JsonPropertyName("source")] public string Source { get; set; } = "Unknown";
}

public sealed class MagneticKnownBool
{
    [JsonPropertyName("known")] public bool Known { get; set; }
    [JsonPropertyName("value")] public bool Value { get; set; }
    [JsonPropertyName("source")] public string Source { get; set; } = "Unknown";
}

public sealed class MagneticGlobalActuationState
{
    [JsonPropertyName("known")] public bool Known { get; set; }
    [JsonPropertyName("raw")] public byte Raw { get; set; }
    [JsonPropertyName("source")] public string Source { get; set; } = "Unknown";
}

public sealed class MagneticGlobalDeadzoneState
{
    [JsonPropertyName("known")] public bool Known { get; set; }
    [JsonPropertyName("top_raw")] public byte TopRaw { get; set; }
    [JsonPropertyName("bottom_raw")] public byte BottomRaw { get; set; }
    [JsonPropertyName("source")] public string Source { get; set; } = "Unknown";
}

public sealed class MagneticGlobalRapidTriggerState
{
    [JsonPropertyName("known")] public bool Known { get; set; }
    [JsonPropertyName("separate_mode")] public bool? SeparateMode { get; set; }
    [JsonPropertyName("press_raw")] public byte PressRaw { get; set; }
    [JsonPropertyName("release_raw")] public byte ReleaseRaw { get; set; }
    [JsonPropertyName("top_raw")] public byte? TopRaw { get; set; }
    [JsonPropertyName("bottom_raw")] public byte? BottomRaw { get; set; }
    [JsonPropertyName("source")] public string Source { get; set; } = "Unknown";
}

public sealed class MagneticHostProfileState
{
    [JsonPropertyName("active_profile_id")] public int? ActiveProfileId { get; set; }
    [JsonPropertyName("global_actuation")] public MagneticKnownRaw GlobalActuation { get; set; } = new();
    [JsonPropertyName("global_rt_press")] public MagneticKnownRaw GlobalRtPress { get; set; } = new();
    [JsonPropertyName("global_rt_release")] public MagneticKnownRaw GlobalRtRelease { get; set; } = new();
    [JsonPropertyName("global_deadzone_top")] public MagneticKnownRaw GlobalDeadzoneTop { get; set; } = new();
    [JsonPropertyName("global_deadzone_bottom")] public MagneticKnownRaw GlobalDeadzoneBottom { get; set; } = new();
    [JsonPropertyName("per_key_rt_list_known")] public bool PerKeyRtListKnown { get; set; }
}

public sealed class MagneticDksTarget
{
    [JsonPropertyName("kind")] public string Kind { get; set; } = "DefaultSentinel";
    [JsonPropertyName("logical_id")] public ushort? LogicalId { get; set; }
}

public sealed class MagneticDksSlot
{
    [JsonPropertyName("target")] public MagneticDksTarget Target { get; set; } = new();
    [JsonPropertyName("down_start")] public string DownStart { get; set; } = "Inactive";
    [JsonPropertyName("down_end")] public string DownEnd { get; set; } = "Inactive";
    [JsonPropertyName("up_start")] public string UpStart { get; set; } = "Inactive";
    [JsonPropertyName("up_end")] public string UpEnd { get; set; } = "Inactive";
}

public sealed class MagneticDksValue
{
    [JsonPropertyName("logical_id")] public ushort LogicalId { get; set; }
    [JsonPropertyName("start_raw")] public byte StartRaw { get; set; }
    [JsonPropertyName("end_raw")] public byte EndRaw { get; set; }
    [JsonPropertyName("slots")] public List<MagneticDksSlot> Slots { get; set; } = [];
    [JsonPropertyName("standard_runtime_configuration")] public bool StandardRuntimeConfiguration { get; set; }
    [JsonPropertyName("source")] public string Source { get; set; } = "Unknown";
}

public sealed class MagneticSpeedTapPair
{
    [JsonPropertyName("key1")] public ushort Key1 { get; set; }
    [JsonPropertyName("key2")] public ushort Key2 { get; set; }
    [JsonPropertyName("enabled")] public bool Enabled { get; set; }
    [JsonPropertyName("source")] public string Source { get; set; } = "Unknown";
}

public sealed class MagneticSpeedTapState
{
    [JsonPropertyName("pair_knowledge")] public string PairKnowledge { get; set; } = "Unknown";
    [JsonPropertyName("pair_submissions")] public List<MagneticSpeedTapPair> PairSubmissions { get; set; } = [];
    [JsonPropertyName("saved_profile_pairs")] public List<MagneticSpeedTapPair> SavedProfilePairs { get; set; } = [];
    [JsonPropertyName("saved_profile_pairs_known")] public bool SavedProfilePairsKnown { get; set; }
    [JsonPropertyName("master")] public MagneticKnownBool Master { get; set; } = new();
}

public sealed class MagneticStatus
{
    [JsonPropertyName("status")] public string Status { get; set; } = "error";
    [JsonPropertyName("api_version")] public int ApiVersion { get; set; }
    [JsonPropertyName("health")] public string Health { get; set; } = "Unknown";
    [JsonPropertyName("available")] public bool Available { get; set; }
    [JsonPropertyName("persistent_safety_quarantine")] public bool PersistentSafetyQuarantine { get; set; }
    [JsonPropertyName("last_error")] public string LastError { get; set; } = "";
    [JsonPropertyName("actuation")] public List<MagneticActuationValue> Actuation { get; set; } = [];
    [JsonPropertyName("rapid_trigger")] public List<MagneticRapidTriggerValue> RapidTrigger { get; set; } = [];
    [JsonPropertyName("deadzone")] public List<MagneticDeadzoneValue> Deadzone { get; set; } = [];
    [JsonPropertyName("dks")] public List<MagneticDksValue> Dks { get; set; } = [];
    [JsonPropertyName("global_actuation")] public MagneticGlobalActuationState GlobalActuation { get; set; } = new();
    [JsonPropertyName("global_deadzone")] public MagneticGlobalDeadzoneState GlobalDeadzone { get; set; } = new();
    [JsonPropertyName("global_rapid_trigger")] public MagneticGlobalRapidTriggerState GlobalRapidTrigger { get; set; } = new();
    [JsonPropertyName("host_profile")] public MagneticHostProfileState HostProfile { get; set; } = new();
    [JsonPropertyName("speedtap")] public MagneticSpeedTapState SpeedTap { get; set; } = new();
    [JsonPropertyName("static_analog_effect")] public MagneticKnownBool StaticAnalogEffect { get; set; } = new();
    [JsonPropertyName("rapid_trigger_master")] public MagneticKnownBool RapidTriggerMaster { get; set; } = new();
    [JsonPropertyName("batch_result")] public MagneticBatchResult? BatchResult { get; set; }
    public bool Succeeded => Status == "ok";
}

public interface IMagneticControlClient
{
    Task<MagneticStatus> GetStatusAsync();
    Task<MagneticStatus> SetActuationAsync(ushort logicalId, double mm);
    Task<MagneticStatus> SetRapidTriggerAsync(ushort logicalId, double pressMm, double releaseMm, bool resolveDks);
    Task<MagneticStatus> DisableRapidTriggerAsync(ushort logicalId);
    Task<MagneticStatus> SetDeadzoneAsync(ushort logicalId, double topMm, double bottomMm);
    Task<MagneticStatus> ResetAllDeadzoneAsync();
    Task<MagneticStatus> SetDksAsync(ushort logicalId, double startMm, double endMm, IReadOnlyList<MagneticDksSlot> slots, bool resolveRt);
    Task<MagneticStatus> RestoreDksStandardAsync(ushort logicalId);
    Task<MagneticStatus> SetSpeedTapPairAsync(ushort key1, ushort key2);
    Task<MagneticStatus> DisableSpeedTapPairAsync(ushort key1, ushort key2);
    Task<MagneticStatus> SetSpeedTapMasterAsync(bool enabled);
    Task<MagneticStatus> ResetSpeedTapToProfileAsync();
    Task<MagneticStatus> SetStaticAnalogEffectAsync(bool enabled);
    Task<MagneticStatus> SetGlobalActuationAsync(double mm);
    Task<MagneticStatus> SetGlobalDeadzoneAsync(double topMm, double bottomMm);
    Task<MagneticStatus> SetGlobalRapidTriggerAsync(double pressMm, double releaseMm, double topMm, double bottomMm, bool separateMode);
    Task<MagneticStatus> AcknowledgeExternalResynchronizationAsync();
    Task<MagneticStatus> SetBatchActuationAsync(IReadOnlyList<ushort> logicalIds, double millimeters);
    Task<MagneticStatus> SetBatchRapidTriggerAsync(IReadOnlyList<ushort> logicalIds, bool enable,
        double? pressMm = null, double? releaseMm = null, bool resolveDks = false);
    Task<MagneticStatus> SetBatchDeadzoneAsync(IReadOnlyList<ushort> logicalIds, double topMm, double bottomMm);
}

public sealed class MagneticControlClient(HttpClient? http = null) : IMagneticControlClient
{
    private readonly HttpClient _http = http ?? new HttpClient { Timeout = TimeSpan.FromMinutes(3) };
    private const string Base = "http://127.0.0.1:19897/api/magnetic/";

    public Task<MagneticStatus> GetStatusAsync() => SendAsync("status", null);
    public Task<MagneticStatus> SetActuationAsync(ushort logicalId, double mm) =>
        SendAsync("actuation", new { logical_id = logicalId, mm });
    public Task<MagneticStatus> SetRapidTriggerAsync(ushort logicalId, double pressMm, double releaseMm, bool resolveDks) =>
        SendAsync("rapid-trigger", new { logical_id = logicalId, press_mm = pressMm, release_mm = releaseMm, resolve_dks = resolveDks });
    public Task<MagneticStatus> DisableRapidTriggerAsync(ushort logicalId) =>
        SendAsync("rapid-trigger/disable", new { logical_id = logicalId });
    public Task<MagneticStatus> SetDeadzoneAsync(ushort logicalId, double topMm, double bottomMm) =>
        SendAsync("deadzone", new { logical_id = logicalId, top_mm = topMm, bottom_mm = bottomMm });
    public Task<MagneticStatus> ResetAllDeadzoneAsync() => SendAsync("deadzone/reset-all", new { confirm_all_keys = true });
    public Task<MagneticStatus> SetDksAsync(ushort logicalId, double startMm, double endMm,
        IReadOnlyList<MagneticDksSlot> slots, bool resolveRt) =>
        SendAsync("dks", new { logical_id = logicalId, start_mm = startMm, end_mm = endMm,
            slots = slots.Select(s => new { target = s.Target.Kind == "DefaultSentinel" ?
                (object)new { kind = "DefaultSentinel" } : new { kind = "LogicalKey", logical_id = s.Target.LogicalId },
                down_start = s.DownStart, down_end = s.DownEnd, up_start = s.UpStart, up_end = s.UpEnd }).ToArray(),
            resolve_rt = resolveRt });
    public Task<MagneticStatus> RestoreDksStandardAsync(ushort logicalId) =>
        SendAsync("dks/standard", new { logical_id = logicalId });
    public Task<MagneticStatus> SetSpeedTapPairAsync(ushort key1, ushort key2) =>
        SendAsync("speedtap/pair", new { key1, key2 });
    public Task<MagneticStatus> DisableSpeedTapPairAsync(ushort key1, ushort key2) =>
        SendAsync("speedtap/pair/disable", new { key1, key2 });
    public Task<MagneticStatus> SetSpeedTapMasterAsync(bool enabled) =>
        SendAsync("speedtap/master", new { enabled });
    public Task<MagneticStatus> ResetSpeedTapToProfileAsync() =>
        SendAsync("speedtap/profile-reset", new { confirm_profile_baseline = true });
    public Task<MagneticStatus> SetStaticAnalogEffectAsync(bool enabled) =>
        SendAsync("analog-effect/static", new { enabled });
    public Task<MagneticStatus> SetGlobalActuationAsync(double mm) =>
        SendAsync("global/actuation", new { mm });
    public Task<MagneticStatus> SetGlobalDeadzoneAsync(double topMm, double bottomMm) =>
        SendAsync("global/deadzone", new { top_mm = topMm, bottom_mm = bottomMm });
    public Task<MagneticStatus> SetGlobalRapidTriggerAsync(double pressMm, double releaseMm, double topMm, double bottomMm, bool separateMode) =>
        SendAsync("global/rapid-trigger", new { press_mm = pressMm, release_mm = releaseMm, top_mm = topMm, bottom_mm = bottomMm, separate_mode = separateMode });
    public Task<MagneticStatus> AcknowledgeExternalResynchronizationAsync() =>
        SendAsync("safety/acknowledge-external-resynchronization", new { confirm_external_resynchronization = true });

    public Task<MagneticStatus> SetBatchActuationAsync(IReadOnlyList<ushort> logicalIds, double millimeters) =>
        SendAsync("batch/actuation", new { logical_ids = logicalIds, millimeters }, batch: true);
    public Task<MagneticStatus> SetBatchRapidTriggerAsync(IReadOnlyList<ushort> logicalIds, bool enable,
        double? pressMm = null, double? releaseMm = null, bool resolveDks = false) =>
        enable ? SendAsync("batch/rapid-trigger", new { logical_ids = logicalIds, action = "enable",
            press_mm = pressMm, release_mm = releaseMm, resolve_dks = resolveDks }, batch: true) :
            SendAsync("batch/rapid-trigger", new { logical_ids = logicalIds, action = "disable" }, batch: true);
    public Task<MagneticStatus> SetBatchDeadzoneAsync(IReadOnlyList<ushort> logicalIds, double topMm, double bottomMm) =>
        SendAsync("batch/deadzone", new { logical_ids = logicalIds, top_mm = topMm, bottom_mm = bottomMm }, batch: true);

    private async Task<MagneticStatus> SendAsync(string path, object? body, bool batch = false)
    {
        try
        {
            using var request = new HttpRequestMessage(body == null ? HttpMethod.Get : HttpMethod.Post, Base + path);
            // 68 sequential transactions take at least 68 * (210 + 400) ms;
            // RT conflict resolution can double that. Keep ordinary calls at 15 s.
            using var timeout = new CancellationTokenSource(batch ? TimeSpan.FromMinutes(3) : TimeSpan.FromSeconds(15));
            if (body != null)
                request.Content = new StringContent(JsonSerializer.Serialize(body), Encoding.UTF8, "application/json");
            using var response = await _http.SendAsync(request, timeout.Token).ConfigureAwait(false);
            var json = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
            var status = JsonSerializer.Deserialize<MagneticStatus>(json);
            if (status == null || status.ApiVersion != 1 ||
                status.Health is not ("Clean" or "TransactionInProgress" or "IndeterminateStagedState" or "PersistentSafetyQuarantine" or "Stopped"))
                return new MagneticStatus { LastError = "磁轴服务返回了无法识别的状态；请刷新后再操作。" };
            if (!response.IsSuccessStatusCode) status.Status = "error";
            return status;
        }
        catch (Exception ex)
        {
            // The request might have reached the daemon before the connection
            // failed. Keep controls locked until an explicit status refresh.
            return new MagneticStatus { LastError = $"无法确认磁轴操作结果：{ex.Message}" };
        }
    }
}
