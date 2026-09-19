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
    [JsonPropertyName("active")]
    public bool Active { get; set; }
}

public sealed class RuntimeStatusResponseDto
{
    [JsonPropertyName("status")]
    public string Status { get; set; } = "";

    [JsonPropertyName("api_version")]
    public int ApiVersion { get; set; } = 1;

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
            return Data.Gsi.Active ? "Active" : "Idle";
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
    public int ApiVersion { get; set; } = 1;

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
    public int ApiVersion { get; set; } = 1;

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
    public int ApiVersion { get; set; } = 1;

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
    Task<ProfileListResult> GetProfilesAsync(CancellationToken cancellationToken = default);
    Task<ProfileDetailResult> GetProfileAsync(string name, CancellationToken cancellationToken = default);
    Task<UpdateProfileResult> UpdateProfileAsync(string name, ProfilePatchDto patch, CancellationToken cancellationToken = default);
}

public sealed class AuraControlClient : IAuraControlClient
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
                if (okDto != null && string.Equals(okDto.Status, "ok", StringComparison.OrdinalIgnoreCase))
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
}
