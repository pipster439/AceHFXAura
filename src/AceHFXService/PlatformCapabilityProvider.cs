using AceHFX.AsusPlatform;
using AceHFX.AsusPlatform.Runtime;

namespace AceHFX.Service;

public sealed class PlatformCapabilityProvider(StructuredLog log) : IAsusCapabilityProvider
{
    private readonly IAsusCapabilityProvider _detector = new AsusCapabilityDetector(new AsusRuntimeDetector());
    private AsusPlatformCapabilities? _cached;
    private string? _summary;
    public AsusPlatformCapabilities Discover()
    {
        if (_cached != null && DateTimeOffset.UtcNow - _cached.ObservedAt < TimeSpan.FromSeconds(30)) return _cached;
        _cached = _detector.Discover();
        var summary = string.Join("|", _cached.Components.Select(c => $"{c.Name}:{c.Installed.State}:{c.Registered.State}:{c.Running.State}:{c.Version.Value}"));
        if (summary != _summary)
        {
            log.Write("CapabilityDiscovery", new { _cached.ObservedAt, _cached.SecurityContext,
                components = _cached.Components.Select(c => new { c.Name, c.Installed, c.Registered, c.Running, c.Version }) });
            _summary = summary;
        }
        return _cached;
    }
}
