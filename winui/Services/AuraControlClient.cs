using System;
using System.Net.Http;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;

namespace Aura_WinUI.Services;

public sealed class HardwareStatusDto
{
    [JsonPropertyName("connected")]
    public bool Connected { get; set; }

    [JsonPropertyName("state")]
    public string State { get; set; } = "uninitialized";

    [JsonPropertyName("configured_backend")]
    public string ConfiguredBackend { get; set; } = "auto";

    [JsonPropertyName("active_backend")]
    public string ActiveBackend { get; set; } = "unknown";

    [JsonPropertyName("device_path")]
    public string DevicePath { get; set; } = "";

    [JsonPropertyName("last_error")]
    public string LastError { get; set; } = "";
}

public sealed class RuntimeInfoStatusDto
{
    [JsonPropertyName("active_profile")]
    public string ActiveProfile { get; set; } = "";

    [JsonPropertyName("fps")]
    public int Fps { get; set; } = 25;

    [JsonPropertyName("dry_run")]
    public bool DryRun { get; set; }

    [JsonPropertyName("foreground_process")]
    public string ForegroundProcess { get; set; } = "";
}

public sealed class GsiStatusDto
{
    [JsonPropertyName("source")]
    public string Source { get; set; } = "";
    [JsonPropertyName("active")]
    public bool Active { get; set; }
}

public sealed class RuntimeStatusResponseDto
{
    [JsonPropertyName("identity")]
    public RuntimeIdentityDto? Identity { get; set; }
    [JsonPropertyName("studio_web")]
    public StudioWebStateDto StudioWeb { get; set; } = new();
    [JsonPropertyName("status")]
    public string Status { get; set; } = "";

    [JsonPropertyName("api_version")]
    public int ApiVersion { get; set; }

    [JsonPropertyName("hardware")]
    public HardwareStatusDto Hardware { get; set; } = new();

    [JsonPropertyName("runtime")]
    public RuntimeInfoStatusDto Runtime { get; set; } = new();

    [JsonPropertyName("gsi")]
    public GsiStatusDto Gsi { get; set; } = new();
}

public sealed class RuntimeStatus
{
    public bool IsOnline { get; init; }
    public string ErrorMessage { get; init; } = "";
    public RuntimeStatusResponseDto? Data { get; init; }

    public bool IsDryRun => Data?.Runtime.DryRun == true || string.Equals(Data?.Hardware.ActiveBackend, "dry_run", StringComparison.OrdinalIgnoreCase);

    public string DeviceStatusDisplayName
    {
        get
        {
            if (!IsOnline || Data == null)
            {
                return "Disconnected";
            }
            if (IsDryRun)
            {
                return "Dry-Run";
            }
            if (Data.Hardware.Connected)
            {
                return "Connected";
            }
            var st = Data.Hardware.State;
            if (string.Equals(st, "connecting", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(st, "backoff_wait", StringComparison.OrdinalIgnoreCase))
            {
                return "Reconnecting";
            }
            return "Disconnected";
        }
    }

    public string BackendDisplayName
    {
        get
        {
            if (!IsOnline || Data == null)
            {
                return "Unknown";
            }
            if (IsDryRun)
            {
                return "Dry-Run";
            }
            var b = Data.Hardware.ActiveBackend;
            return b switch
            {
                "native_hid" => "Native HID",
                "legacy_hal" => "ASUS HAL",
                _ => "Unknown"
            };
        }
    }

    public string CoreStatusDisplayName => IsOnline ? "Running" : "Offline";

    public string ActiveProfileDisplayName
    {
        get
        {
            if (!IsOnline || Data == null || string.IsNullOrWhiteSpace(Data.Runtime.ActiveProfile))
            {
                return "--";
            }
            return Data.Runtime.ActiveProfile;
        }
    }

    public string FpsDisplayName
    {
        get
        {
            if (!IsOnline || Data == null)
            {
                return "--";
            }
            return $"{Data.Runtime.Fps} FPS";
        }
    }

    public string GsiStatusDisplayName
    {
        get
        {
            if (!IsOnline || Data == null)
            {
                return "Idle";
            }
            return (Data.Gsi.Source switch { "simulation" => "SIMULATION · ", "real" => "REAL · ", _ => "" }) + (Data.Gsi.Active ? "Active" : "Idle");
        }
    }

    public static RuntimeStatus Offline(string error = "Daemon Offline") => new()
    {
        IsOnline = false,
        ErrorMessage = error,
        Data = null
    };

    public static RuntimeStatus Online(RuntimeStatusResponseDto data) => new()
    {
        IsOnline = true,
        ErrorMessage = "",
        Data = data
    };
}

// ========================================================
// Lighting Control API v1 DTOs
// ========================================================

public sealed class ProfileListItemDto
{
    [JsonPropertyName("name")]
    public string Name { get; set; } = "";

    [JsonPropertyName("type")]
    public string Type { get; set; } = "";
}

public sealed class ProfileListResponseDto
{
    [JsonPropertyName("status")]
    public string Status { get; set; } = "";

    [JsonPropertyName("api_version")]
    public int ApiVersion { get; set; }

    [JsonPropertyName("revision")]
    public string Revision { get; set; } = "";

    [JsonPropertyName("profiles")]
    public List<ProfileListItemDto> Profiles { get; set; } = new();
}

public sealed class ProfileDetailDto
{
    [JsonPropertyName("name")]
    public string Name { get; set; } = "";

    [JsonPropertyName("type")]
    public string Type { get; set; } = "";

    [JsonPropertyName("brightness")]
    public double Brightness { get; set; } = 1.0;

    [JsonPropertyName("fps")]
    public int Fps { get; set; } = 25;

    [JsonPropertyName("fps_inherited")]
    public bool FpsInherited { get; set; } = true;

    [JsonPropertyName("supports_period")]
    public bool SupportsPeriod { get; set; } = false;

    [JsonPropertyName("period_ms")]
    public int? PeriodMs { get; set; }
}

public sealed class ProfileDetailResponseDto
{
    [JsonPropertyName("status")]
    public string Status { get; set; } = "";

    [JsonPropertyName("api_version")]
    public int ApiVersion { get; set; }

    [JsonPropertyName("revision")]
    public string Revision { get; set; } = "";

    [JsonPropertyName("profile")]
    public ProfileDetailDto? Profile { get; set; }
}

public sealed class ProfilePatchDto
{
    [JsonPropertyName("expected_revision")]
    public string ExpectedRevision { get; set; } = "";

    [JsonPropertyName("brightness")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public double? Brightness { get; set; }

    [JsonPropertyName("period_ms")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? PeriodMs { get; set; }

    [JsonPropertyName("fps")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? Fps { get; set; }
}

public sealed class ProfilePatchResponseDto
{
    [JsonPropertyName("status")]
    public string Status { get; set; } = "";

    [JsonPropertyName("api_version")]
    public int ApiVersion { get; set; }

    [JsonPropertyName("message")]
    public string Message { get; set; } = "";

    [JsonPropertyName("revision")]
    public string Revision { get; set; } = "";
}

// ========================================================
// Phase 3.5 & 3.6: Lighting Presets & Base Lighting DTOs
// ========================================================

public enum EffectParamType
{
    Color,
    Boolean,
    Enum,
    Number
}

public sealed class EffectParamOptionDto
{
    [JsonPropertyName("value")]
    public string Value { get; set; } = "";

    [JsonPropertyName("label")]
    public string Label { get; set; } = "";
}

public sealed class EffectParamSchemaDto
{
    [JsonPropertyName("key")]
    public string Key { get; set; } = "";

    [JsonPropertyName("display_name")]
    public string DisplayName { get; set; } = "";

    [JsonPropertyName("type")]
    public string TypeString { get; set; } = "";

    [JsonIgnore]
    public EffectParamType Type => TypeString.ToLowerInvariant() switch
    {
        "color" => EffectParamType.Color,
        "boolean" => EffectParamType.Boolean,
        "enum" => EffectParamType.Enum,
        "number" => EffectParamType.Number,
        _ => EffectParamType.Color
    };

    [JsonPropertyName("default_value")]
    public JsonElement DefaultValue { get; set; }

    [JsonPropertyName("min")]
    public double? Min { get; set; }

    [JsonPropertyName("max")]
    public double? Max { get; set; }

    [JsonPropertyName("step")]
    public double? Step { get; set; }

    [JsonPropertyName("options")]
    public List<EffectParamOptionDto> Options { get; set; } = new();
}

public static class EffectParamValueComparer
{
    public static bool AreValuesEqual(EffectParamType type, object? a, object? b)
    {
        return type switch
        {
            EffectParamType.Color => ColorsEqual(a, b),
            EffectParamType.Boolean => BooleansEqual(a, b),
            EffectParamType.Enum => StringsEqual(a, b),
            EffectParamType.Number => NumbersEqual(a, b),
            _ => Equals(a, b)
        };
    }

    public static (int r, int g, int b)? ExtractRgb(object? val)
    {
        if (val == null) return null;
        if (val is int[] arr && arr.Length >= 3) return (arr[0], arr[1], arr[2]);
        if (val is List<int> list && list.Count >= 3) return (list[0], list[1], list[2]);
        if (val is ValueTuple<int, int, int> tuple) return (tuple.Item1, tuple.Item2, tuple.Item3);
        if (val is JsonElement je)
        {
            if (je.ValueKind == JsonValueKind.Array)
            {
                var items = new List<int>();
                foreach (var el in je.EnumerateArray())
                {
                    if (el.TryGetInt32(out int n)) items.Add(n);
                    else if (el.TryGetDouble(out double d)) items.Add((int)d);
                }
                if (items.Count >= 3) return (items[0], items[1], items[2]);
            }
        }
        return null;
    }

    public static bool ColorsEqual(object? a, object? b)
    {
        var rgbA = ExtractRgb(a);
        var rgbB = ExtractRgb(b);
        if (rgbA.HasValue && rgbB.HasValue)
        {
            return rgbA.Value.r == rgbB.Value.r &&
                   rgbA.Value.g == rgbB.Value.g &&
                   rgbA.Value.b == rgbB.Value.b;
        }
        return rgbA.HasValue == rgbB.HasValue;
    }

    public static bool? ExtractBool(object? val)
    {
        if (val == null) return null;
        if (val is bool b) return b;
        if (val is JsonElement je)
        {
            if (je.ValueKind == JsonValueKind.True) return true;
            if (je.ValueKind == JsonValueKind.False) return false;
        }
        return null;
    }

    public static bool BooleansEqual(object? a, object? b)
    {
        var bA = ExtractBool(a);
        var bB = ExtractBool(b);
        return bA == bB;
    }

    public static string? ExtractString(object? val)
    {
        if (val == null) return null;
        if (val is string s) return s;
        if (val is JsonElement je)
        {
            if (je.ValueKind == JsonValueKind.String) return je.GetString();
        }
        return val?.ToString();
    }

    public static bool StringsEqual(object? a, object? b)
    {
        var sA = ExtractString(a);
        var sB = ExtractString(b);
        return string.Equals(sA, sB, StringComparison.Ordinal);
    }

    public static double? ExtractDouble(object? val)
    {
        if (val == null) return null;
        if (val is double d) return d;
        if (val is float f) return f;
        if (val is int i) return i;
        if (val is long l) return l;
        if (val is JsonElement je)
        {
            if (je.ValueKind == JsonValueKind.Number && je.TryGetDouble(out double jd)) return jd;
        }
        if (double.TryParse(val?.ToString(), out double parsed)) return parsed;
        return null;
    }

    public static bool NumbersEqual(object? a, object? b)
    {
        var dA = ExtractDouble(a);
        var dB = ExtractDouble(b);
        if (dA.HasValue && dB.HasValue)
        {
            return Math.Abs(dA.Value - dB.Value) < 0.001;
        }
        return dA.HasValue == dB.HasValue;
    }
}

public sealed class LightingPresetItemDto
{
    [JsonPropertyName("id")]
    public string Id { get; set; } = "";

    [JsonPropertyName("display_name")]
    public string DisplayName { get; set; } = "";

    [JsonPropertyName("effect")]
    public string Effect { get; set; } = "";

    [JsonPropertyName("supports_period")]
    public bool SupportsPeriod { get; set; }

    [JsonPropertyName("default_period_ms")]
    public int? DefaultPeriodMs { get; set; }

    [JsonPropertyName("parameter_schema")]
    public List<EffectParamSchemaDto> ParameterSchema { get; set; } = new();
}

public sealed class LightingPresetListResponseDto
{
    [JsonPropertyName("status")]
    public string Status { get; set; } = "";

    [JsonPropertyName("api_version")]
    public int ApiVersion { get; set; }

    [JsonPropertyName("presets")]
    public List<LightingPresetItemDto> Presets { get; set; } = new();
}

public sealed class BaseLightingDto
{
    [JsonPropertyName("profile_name")]
    public string ProfileName { get; set; } = "";

    [JsonPropertyName("effect")]
    public string Effect { get; set; } = "";

    [JsonPropertyName("preset_id")]
    public string PresetId { get; set; } = "";

    [JsonPropertyName("is_builtin_preset")]
    public bool IsBuiltinPreset { get; set; }

    [JsonPropertyName("brightness")]
    public double Brightness { get; set; } = 1.0;

    [JsonPropertyName("supports_period")]
    public bool SupportsPeriod { get; set; }

    [JsonPropertyName("period_ms")]
    public int? PeriodMs { get; set; }

    [JsonPropertyName("parameters")]
    public Dictionary<string, JsonElement>? Parameters { get; set; }
}

public sealed class BaseLightingResponseDto
{
    [JsonPropertyName("status")]
    public string Status { get; set; } = "";

    [JsonPropertyName("api_version")]
    public int ApiVersion { get; set; }

    [JsonPropertyName("revision")]
    public string Revision { get; set; } = "";

    [JsonPropertyName("lighting")]
    public BaseLightingDto? Lighting { get; set; }
}

public sealed class BaseLightingPatchDto
{
    [JsonPropertyName("expected_revision")]
    public string ExpectedRevision { get; set; } = "";

    [JsonPropertyName("preset")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? Preset { get; set; }

    [JsonPropertyName("brightness")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public double? Brightness { get; set; }

    [JsonPropertyName("period_ms")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? PeriodMs { get; set; }

    [JsonPropertyName("parameters")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public Dictionary<string, object>? Parameters { get; set; }
}

public sealed class BaseLightingPatchResponseDto
{
    [JsonPropertyName("status")]
    public string Status { get; set; } = "";

    [JsonPropertyName("api_version")]
    public int ApiVersion { get; set; }

    [JsonPropertyName("message")]
    public string Message { get; set; } = "";

    [JsonPropertyName("revision")]
    public string Revision { get; set; } = "";
}

public sealed class ApiErrorResponseDto
{
    [JsonPropertyName("status")]
    public string Status { get; set; } = "";

    [JsonPropertyName("error")]
    public string Error { get; set; } = "";

    [JsonPropertyName("message")]
    public string Message { get; set; } = "";

    [JsonPropertyName("current_revision")]
    public string CurrentRevision { get; set; } = "";
}

// ========================================================
// Client Result Models
// ========================================================

public sealed class LightingPresetListResult
{
    public bool IsSuccess { get; init; }
    public string ErrorMessage { get; init; } = "";
    public IReadOnlyList<LightingPresetItemDto> Presets { get; init; } = Array.Empty<LightingPresetItemDto>();

    public static LightingPresetListResult Success(IReadOnlyList<LightingPresetItemDto> presets) => new()
    {
        IsSuccess = true,
        Presets = presets
    };

    public static LightingPresetListResult Failure(string error) => new()
    {
        IsSuccess = false,
        ErrorMessage = error
    };
}

public sealed class BaseLightingResult
{
    public bool IsSuccess { get; init; }
    public string ErrorMessage { get; init; } = "";
    public string Revision { get; init; } = "";
    public BaseLightingDto? Lighting { get; init; }

    public static BaseLightingResult Success(BaseLightingDto lighting, string revision) => new()
    {
        IsSuccess = true,
        Lighting = lighting,
        Revision = revision
    };

    public static BaseLightingResult Failure(string error) => new()
    {
        IsSuccess = false,
        ErrorMessage = error
    };
}

public sealed class UpdateBaseLightingResult
{
    public UpdateProfileStatus Status { get; init; }
    public bool IsSuccess => Status == UpdateProfileStatus.Success;
    public bool IsConflict => Status == UpdateProfileStatus.Conflict;
    public string ErrorMessage { get; init; } = "";
    public string NewRevision { get; init; } = "";
    public string CurrentRevision { get; init; } = "";

    public static UpdateBaseLightingResult Success(string newRevision) => new()
    {
        Status = UpdateProfileStatus.Success,
        NewRevision = newRevision
    };

    public static UpdateBaseLightingResult Conflict(string currentRevision, string message) => new()
    {
        Status = UpdateProfileStatus.Conflict,
        CurrentRevision = currentRevision,
        ErrorMessage = string.IsNullOrEmpty(message) ? "配置已被其他编辑器修改" : message
    };

    public static UpdateBaseLightingResult Failure(UpdateProfileStatus status, string message) => new()
    {
        Status = status,
        ErrorMessage = message
    };
}

public sealed class ProfileListResult
{
    public bool IsSuccess { get; init; }
    public string ErrorMessage { get; init; } = "";
    public string Revision { get; init; } = "";
    public IReadOnlyList<ProfileListItemDto> Profiles { get; init; } = Array.Empty<ProfileListItemDto>();

    public static ProfileListResult Success(IReadOnlyList<ProfileListItemDto> profiles, string revision) => new()
    {
        IsSuccess = true,
        Profiles = profiles,
        Revision = revision
    };

    public static ProfileListResult Failure(string error) => new()
    {
        IsSuccess = false,
        ErrorMessage = error
    };
}

public sealed class ProfileDetailResult
{
    public bool IsSuccess { get; init; }
    public string ErrorMessage { get; init; } = "";
    public string Revision { get; init; } = "";
    public ProfileDetailDto? Profile { get; init; }

    public static ProfileDetailResult Success(ProfileDetailDto profile, string revision) => new()
    {
        IsSuccess = true,
        Profile = profile,
        Revision = revision
    };

    public static ProfileDetailResult Failure(string error) => new()
    {
        IsSuccess = false,
        ErrorMessage = error
    };
}

public enum UpdateProfileStatus
{
    Success,
    Conflict,
    ValidationError,
    NotFound,
    Failure
}

public sealed class UpdateProfileResult
{
    public UpdateProfileStatus Status { get; init; }
    public bool IsSuccess => Status == UpdateProfileStatus.Success;
    public bool IsConflict => Status == UpdateProfileStatus.Conflict;
    public string ErrorMessage { get; init; } = "";
    public string NewRevision { get; init; } = "";
    public string CurrentRevision { get; init; } = "";

    public static UpdateProfileResult Success(string newRevision) => new()
    {
        Status = UpdateProfileStatus.Success,
        NewRevision = newRevision
    };

    public static UpdateProfileResult Conflict(string currentRevision, string message) => new()
    {
        Status = UpdateProfileStatus.Conflict,
        CurrentRevision = currentRevision,
        ErrorMessage = string.IsNullOrEmpty(message) ? "配置已被其他编辑器修改" : message
    };

    public static UpdateProfileResult Failure(UpdateProfileStatus status, string message) => new()
    {
        Status = status,
        ErrorMessage = message
    };
}

public interface IAuraControlClient
{
    Task<RuntimeStatus> GetRuntimeStatusAsync(CancellationToken cancellationToken = default);
    Task<LightingPresetListResult> GetLightingPresetsAsync(CancellationToken cancellationToken = default);
    Task<BaseLightingResult> GetBaseLightingAsync(CancellationToken cancellationToken = default);
    Task<UpdateBaseLightingResult> UpdateBaseLightingAsync(BaseLightingPatchDto patch, CancellationToken cancellationToken = default);
    Task<ProfileListResult> GetProfilesAsync(CancellationToken cancellationToken = default);
    Task<ProfileDetailResult> GetProfileAsync(string name, CancellationToken cancellationToken = default);
    Task<UpdateProfileResult> UpdateProfileAsync(string name, ProfilePatchDto patch, CancellationToken cancellationToken = default);


}

public sealed partial class AuraControlClient : IAuraControlClient
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    private readonly HttpClient _http;

    private static AuraControlClient? _instance;
    public static AuraControlClient Instance => _instance ??= new AuraControlClient();

    public AuraControlClient(HttpClient? client = null)
    {
        _http = client ?? new HttpClient { Timeout = TimeSpan.FromMilliseconds(2500) };
    }

    public async Task<RuntimeStatus> GetRuntimeStatusAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            using var req = new HttpRequestMessage(HttpMethod.Get, "http://127.0.0.1:19897/api/runtime/status");
            using var res = await _http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);

            if (!res.IsSuccessStatusCode)
            {
                return RuntimeStatus.Offline($"HTTP {(int)res.StatusCode}: {res.ReasonPhrase}");
            }

            var json = await res.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            var dto = JsonSerializer.Deserialize<RuntimeStatusResponseDto>(json, JsonOptions);

            if (dto == null)
            {
                return RuntimeStatus.Offline("Empty or invalid JSON payload");
            }

            // 严格校验状态与版本契约 (Constraint 6)
            if (!string.Equals(dto.Status, "ok", StringComparison.OrdinalIgnoreCase))
            {
                return RuntimeStatus.Offline($"Daemon reported non-ok status: {dto.Status}");
            }

            if (dto.ApiVersion != 1)
            {
                return RuntimeStatus.Offline($"Unsupported API version: {dto.ApiVersion} (expected 1)");
            }

            return RuntimeStatus.Online(dto);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return RuntimeStatus.Offline("Request canceled");
        }
        catch (Exception ex)
        {
            return RuntimeStatus.Offline(ex.Message);
        }
    }

    public async Task<ProfileListResult> GetProfilesAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            using var req = new HttpRequestMessage(HttpMethod.Get, "http://127.0.0.1:19897/api/lighting/profiles");
            using var res = await _http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);

            var json = await res.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            if (!res.IsSuccessStatusCode)
            {
                return ProfileListResult.Failure($"HTTP {(int)res.StatusCode}: {res.ReasonPhrase}");
            }

            var dto = JsonSerializer.Deserialize<ProfileListResponseDto>(json, JsonOptions);
            if (dto == null)
            {
                return ProfileListResult.Failure("Empty or invalid JSON payload");
            }

            if (!string.Equals(dto.Status, "ok", StringComparison.OrdinalIgnoreCase))
            {
                return ProfileListResult.Failure($"Daemon returned non-ok status: {dto.Status}");
            }

            if (dto.ApiVersion != 1)
            {
                return ProfileListResult.Failure($"Unsupported API version: {dto.ApiVersion} (expected 1)");
            }

            return ProfileListResult.Success(dto.Profiles, dto.Revision);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return ProfileListResult.Failure("Request canceled");
        }
        catch (Exception ex)
        {
            return ProfileListResult.Failure(ex.Message);
        }
    }

    public async Task<ProfileDetailResult> GetProfileAsync(string name, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return ProfileDetailResult.Failure("Profile name cannot be empty");
        }

        try
        {
            string url = $"http://127.0.0.1:19897/api/lighting/profiles/{Uri.EscapeDataString(name)}";
            using var req = new HttpRequestMessage(HttpMethod.Get, url);
            using var res = await _http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);

            var json = await res.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            if (!res.IsSuccessStatusCode)
            {
                return ProfileDetailResult.Failure($"HTTP {(int)res.StatusCode}: {res.ReasonPhrase}");
            }

            var dto = JsonSerializer.Deserialize<ProfileDetailResponseDto>(json, JsonOptions);
            if (dto == null || dto.Profile == null)
            {
                return ProfileDetailResult.Failure("Empty or invalid JSON payload");
            }

            if (!string.Equals(dto.Status, "ok", StringComparison.OrdinalIgnoreCase))
            {
                return ProfileDetailResult.Failure($"Daemon returned non-ok status: {dto.Status}");
            }

            if (dto.ApiVersion != 1)
            {
                return ProfileDetailResult.Failure($"Unsupported API version: {dto.ApiVersion} (expected 1)");
            }

            return ProfileDetailResult.Success(dto.Profile, dto.Revision);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return ProfileDetailResult.Failure("Request canceled");
        }
        catch (Exception ex)
        {
            return ProfileDetailResult.Failure(ex.Message);
        }
    }

    public async Task<UpdateProfileResult> UpdateProfileAsync(string name, ProfilePatchDto patch, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return UpdateProfileResult.Failure(UpdateProfileStatus.ValidationError, "Profile name cannot be empty");
        }

        try
        {
            string url = $"http://127.0.0.1:19897/api/lighting/profiles/{Uri.EscapeDataString(name)}";
            string patchJson = JsonSerializer.Serialize(patch, JsonOptions);

            using var req = new HttpRequestMessage(new HttpMethod("PATCH"), url)
            {
                Content = new StringContent(patchJson, System.Text.Encoding.UTF8, "application/json")
            };

            using var res = await _http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);
            var resJson = await res.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);

            if (res.StatusCode == System.Net.HttpStatusCode.OK)
            {
                var okDto = JsonSerializer.Deserialize<ProfilePatchResponseDto>(resJson, JsonOptions);
                if (okDto != null && okDto.ApiVersion == 1 && !string.IsNullOrEmpty(okDto.Revision) && string.Equals(okDto.Status, "ok", StringComparison.OrdinalIgnoreCase))
                {
                    return UpdateProfileResult.Success(okDto.Revision);
                }
                return UpdateProfileResult.Failure(UpdateProfileStatus.Failure, "Unexpected non-ok response from daemon");
            }

            ApiErrorResponseDto? errDto = null;
            try
            {
                errDto = JsonSerializer.Deserialize<ApiErrorResponseDto>(resJson, JsonOptions);
            }
            catch { }

            string msg = errDto?.Message ?? res.ReasonPhrase ?? $"HTTP {(int)res.StatusCode}";

            if (res.StatusCode == System.Net.HttpStatusCode.Conflict)
            {
                return UpdateProfileResult.Conflict(errDto?.CurrentRevision ?? "", msg);
            }

            if (res.StatusCode == System.Net.HttpStatusCode.BadRequest)
            {
                return UpdateProfileResult.Failure(UpdateProfileStatus.ValidationError, msg);
            }

            if (res.StatusCode == System.Net.HttpStatusCode.NotFound)
            {
                return UpdateProfileResult.Failure(UpdateProfileStatus.NotFound, msg);
            }

            return UpdateProfileResult.Failure(UpdateProfileStatus.Failure, msg);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return UpdateProfileResult.Failure(UpdateProfileStatus.Failure, "Request canceled");
        }
        catch (Exception ex)
        {
            return UpdateProfileResult.Failure(UpdateProfileStatus.Failure, ex.Message);
        }
    }

    public async Task<LightingPresetListResult> GetLightingPresetsAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            using var req = new HttpRequestMessage(HttpMethod.Get, "http://127.0.0.1:19897/api/lighting/presets");
            using var res = await _http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);

            var json = await res.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            if (!res.IsSuccessStatusCode)
            {
                return LightingPresetListResult.Failure($"HTTP {(int)res.StatusCode}: {res.ReasonPhrase}");
            }

            var dto = JsonSerializer.Deserialize<LightingPresetListResponseDto>(json, JsonOptions);
            if (dto == null)
            {
                return LightingPresetListResult.Failure("Empty or invalid JSON payload");
            }

            if (!string.Equals(dto.Status, "ok", StringComparison.OrdinalIgnoreCase))
            {
                return LightingPresetListResult.Failure($"Daemon returned non-ok status: {dto.Status}");
            }

            if (dto.ApiVersion != 1)
            {
                return LightingPresetListResult.Failure($"Unsupported API version: {dto.ApiVersion} (expected 1)");
            }

            return LightingPresetListResult.Success(dto.Presets);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return LightingPresetListResult.Failure("Request canceled");
        }
        catch (Exception ex)
        {
            return LightingPresetListResult.Failure(ex.Message);
        }
    }

    public async Task<BaseLightingResult> GetBaseLightingAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            using var req = new HttpRequestMessage(HttpMethod.Get, "http://127.0.0.1:19897/api/lighting/base");
            using var res = await _http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);

            var json = await res.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            if (!res.IsSuccessStatusCode)
            {
                return BaseLightingResult.Failure($"HTTP {(int)res.StatusCode}: {res.ReasonPhrase}");
            }

            var dto = JsonSerializer.Deserialize<BaseLightingResponseDto>(json, JsonOptions);
            if (dto == null || dto.Lighting == null)
            {
                return BaseLightingResult.Failure("Empty or invalid JSON payload");
            }

            if (!string.Equals(dto.Status, "ok", StringComparison.OrdinalIgnoreCase))
            {
                return BaseLightingResult.Failure($"Daemon returned non-ok status: {dto.Status}");
            }

            if (dto.ApiVersion != 1)
            {
                return BaseLightingResult.Failure($"Unsupported API version: {dto.ApiVersion} (expected 1)");
            }

            return BaseLightingResult.Success(dto.Lighting, dto.Revision);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return BaseLightingResult.Failure("Request canceled");
        }
        catch (Exception ex)
        {
            return BaseLightingResult.Failure(ex.Message);
        }
    }

    public async Task<UpdateBaseLightingResult> UpdateBaseLightingAsync(BaseLightingPatchDto patch, CancellationToken cancellationToken = default)
    {
        try
        {
            string url = "http://127.0.0.1:19897/api/lighting/base";
            string patchJson = JsonSerializer.Serialize(patch, JsonOptions);

            using var req = new HttpRequestMessage(new HttpMethod("PATCH"), url)
            {
                Content = new StringContent(patchJson, System.Text.Encoding.UTF8, "application/json")
            };

            using var res = await _http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);
            var resJson = await res.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);

            if (res.StatusCode == System.Net.HttpStatusCode.OK)
            {
                var okDto = JsonSerializer.Deserialize<BaseLightingPatchResponseDto>(resJson, JsonOptions);
                if (okDto != null && okDto.ApiVersion == 1 && !string.IsNullOrEmpty(okDto.Revision) && string.Equals(okDto.Status, "ok", StringComparison.OrdinalIgnoreCase))
                {
                    return UpdateBaseLightingResult.Success(okDto.Revision);
                }
                return UpdateBaseLightingResult.Failure(UpdateProfileStatus.Failure, "Unexpected non-ok response from daemon");
            }

            ApiErrorResponseDto? errDto = null;
            try
            {
                errDto = JsonSerializer.Deserialize<ApiErrorResponseDto>(resJson, JsonOptions);
            }
            catch { }

            string msg = errDto?.Message ?? res.ReasonPhrase ?? $"HTTP {(int)res.StatusCode}";

            if (res.StatusCode == System.Net.HttpStatusCode.Conflict)
            {
                return UpdateBaseLightingResult.Conflict(errDto?.CurrentRevision ?? "", msg);
            }

            if (res.StatusCode == System.Net.HttpStatusCode.BadRequest)
            {
                return UpdateBaseLightingResult.Failure(UpdateProfileStatus.ValidationError, msg);
            }

            if (res.StatusCode == System.Net.HttpStatusCode.NotFound)
            {
                return UpdateBaseLightingResult.Failure(UpdateProfileStatus.NotFound, msg);
            }

            return UpdateBaseLightingResult.Failure(UpdateProfileStatus.Failure, msg);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return UpdateBaseLightingResult.Failure(UpdateProfileStatus.Failure, "Request canceled");
        }
        catch (Exception ex)
        {
            return UpdateBaseLightingResult.Failure(UpdateProfileStatus.Failure, ex.Message);
        }
    }

}
