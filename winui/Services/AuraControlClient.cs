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

public interface IAuraControlClient
{
    Task<RuntimeStatus> GetRuntimeStatusAsync(CancellationToken cancellationToken = default);
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
        _http = client ?? new HttpClient { Timeout = TimeSpan.FromMilliseconds(1200) };
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
}
