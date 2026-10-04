namespace AceHFX.AsusPlatform.Ipc;

public sealed class RequestDispatcher(IAsusCapabilityProvider provider, ServerHello identity,
    Cooling.ICoolingReadOnlyProvider? cooling = null)
{
    private readonly HashSet<Guid> _requests = [];
    private bool _hello;
    public async Task<PlatformResponse> DispatchAsync(PlatformRequest request, CancellationToken token)
    {
        // Reuse envelope/hello/duplicate validation before any worker can be launched.
        var response = DispatchCore(request, true);
        if (!response.Success || request.Command != Command.GetCoolingReadOnlySnapshot) return response;
        try
        {
            return response with { Cooling = await cooling!.ReadAsync(token) };
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception) { return response with { Success = false, Error = PlatformError.InternalError, Diagnostic = "CoolingReadFailed" }; }
    }
    public PlatformResponse Dispatch(PlatformRequest request) => DispatchCore(request, false);
    private PlatformResponse DispatchCore(PlatformRequest request, bool allowCooling)
    {
        PlatformResponse Fail(PlatformError error, string reason) => new(Protocol.Version, request.RequestId, false, error, reason);
        if (request.ProtocolVersion != Protocol.Version) return Fail(PlatformError.ProtocolMismatch, "ProtocolVersionMismatch");
        if (request.RequestId == Guid.Empty) return Fail(PlatformError.MalformedRequest, "RequestIdRequired");
        if (!Enum.IsDefined(request.Command)) return Fail(PlatformError.UnsupportedCapability, "UnknownCommand");
        if (_requests.Count >= Protocol.MaximumRequestsPerSession) return Fail(PlatformError.RequestTooLarge, "SessionRequestLimit");
        if (!_requests.Add(request.RequestId)) return Fail(PlatformError.DuplicateRequestId, "DuplicateRequestId");
        if (!_hello && request.Command != Command.ClientHello) return Fail(PlatformError.MalformedRequest, "ClientHelloRequired");
        if (request.Command == Command.ClientHello) _hello = true;
        try
        {
            return request.Command switch
            {
                Command.ClientHello or Command.GetProtocolVersion or Command.GetServiceVersion or Command.GetServerIdentity =>
                    new(Protocol.Version, request.RequestId, true, PlatformError.None, Hello: identity),
                Command.Ping => new(Protocol.Version, request.RequestId, true, PlatformError.None),
                Command.GetCoolingReadOnlySnapshot => cooling == null || !allowCooling ? Fail(PlatformError.UnsupportedCapability, "CoolingReadNotSupported") :
                    new(Protocol.Version, request.RequestId, true, PlatformError.None),
                Command.GetAsusRuntimeCapabilities => new(Protocol.Version, request.RequestId, true, PlatformError.None, Capabilities: provider.Discover()),
                Command.GetAsusServiceStatus => new(Protocol.Version, request.RequestId, true, PlatformError.None, Services: provider.Discover().Services),
                Command.GetAsusComponentVersions => new(Protocol.Version, request.RequestId, true, PlatformError.None, Components: provider.Discover().Components),
                _ => Fail(PlatformError.UnsupportedCapability, "UnknownCommand")
            };
        }
        catch (UnauthorizedAccessException) { return Fail(PlatformError.AccessDenied, "DiscoveryAccessDenied"); }
        catch (Exception) { return Fail(PlatformError.InternalError, "DiscoveryFailed"); }
    }
}
