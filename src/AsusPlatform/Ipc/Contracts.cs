using System.Text.Json;

namespace AceHFX.AsusPlatform.Ipc;

public enum Command { ClientHello = 1, Ping = 2, GetProtocolVersion = 3, GetServiceVersion = 4,
    GetServerIdentity = 5, GetAsusRuntimeCapabilities = 6, GetAsusServiceStatus = 7, GetAsusComponentVersions = 8,
    GetCoolingReadOnlySnapshot = 9 }
// No arbitrary method, arguments, path, polymorphic payload or vendor object on this surface.
public sealed record PlatformRequest(int ProtocolVersion, Guid RequestId, Command Command);
public sealed record ServerHello(int ProtocolVersion, string ServiceVersion, SecurityContext Identity);
public sealed record PlatformResponse(int ProtocolVersion, Guid RequestId, bool Success, PlatformError Error,
    string? Diagnostic = null, ServerHello? Hello = null, AsusPlatformCapabilities? Capabilities = null,
    IReadOnlyList<ComponentStatus>? Services = null, IReadOnlyList<ComponentStatus>? Components = null,
    Cooling.CoolingRuntimeSnapshot? Cooling = null);
public interface IAceHfxServiceClient
{
    Task<PlatformResponse> SendAsync(Command command, CancellationToken cancellationToken = default);
}
public static class Protocol
{
    public const int Version = 1;
    public const string PipeName = "AceHFXPlatform";
    public const string ServiceName = "AceHFXService";
    public const int MaximumMessageBytes = 128 * 1024;
    public const int MaximumRequestsPerSession = 128;
    public static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(5);
    public static readonly TimeSpan SessionTimeout = TimeSpan.FromSeconds(30);
    public static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web) { MaxDepth = 16 };
}
