using AceHFX.AsusPlatform.Ipc;
using AceHFX.AsusPlatform.Runtime;

namespace AceHFX.AsusPlatform;

public sealed class AsusPlatform(IAceHfxServiceClient client) : IAsusPlatform
{
    public async Task<Cooling.CoolingRuntimeSnapshot> GetCoolingAsync(CancellationToken cancellationToken = default)
    {
        var response = await client.SendAsync(Command.GetCoolingReadOnlySnapshot, cancellationToken);
        return response.Success && response.Cooling != null ? response.Cooling :
            Cooling.CoolingRuntimeSnapshot.Unobserved(response.Success ? PlatformError.MalformedRequest : response.Error,
                response.Diagnostic ?? "CoolingSnapshotMissing");
    }
    public async Task<AsusPlatformStatus> GetStatusAsync(CancellationToken cancellationToken = default)
    {
        var broker = await Task.Run(() => new AsusServiceDetector(new()).Detect(Protocol.ServiceName), cancellationToken);
        if (broker.Running.Value != true) return new(broker, false, PlatformError.ServiceUnavailable, "BrokerNotRunning",
            await Task.Run(() => new AsusCapabilityDetector(new AsusRuntimeDetector()).Discover(), cancellationToken));
        var response = await client.SendAsync(Command.GetAsusRuntimeCapabilities, cancellationToken);
        if (response.Success && response.Capabilities == null)
            return new(broker, false, PlatformError.MalformedRequest, "CapabilitiesMissing", null);
        return new(broker, response.Success, response.Error, response.Diagnostic, response.Capabilities);
    }
}
