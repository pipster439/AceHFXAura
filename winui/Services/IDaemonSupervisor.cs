using System;
using System.Threading.Tasks;

namespace Aura_WinUI.Services;

public enum DaemonOwnership
{
    None,
    SpawnedByWinUI,
    AttachedPreExisting
}

public interface IDaemonSupervisor
{
    bool IsDaemonRunning { get; }
    bool CoreReady { get; }
    bool StudioWebReady { get; }
    bool IsWebServerReady { get; }
    string StatusDescription { get; }
    DaemonOwnership Ownership { get; }

    event Action<string>? StatusChanged;

    Task EnsureStartedAsync();
    Task StopAsync();
    Task<bool> ProbeWebServerAsync();
}
