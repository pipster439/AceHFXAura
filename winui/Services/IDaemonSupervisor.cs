using System;
using System.Threading.Tasks;

namespace Aura_WinUI.Services;

public interface IDaemonSupervisor
{
    bool IsDaemonRunning { get; }
    bool IsWebServerReady { get; }
    string StatusDescription { get; }

    event Action<string>? StatusChanged;

    Task EnsureStartedAsync();
    Task StopAsync();
    Task<bool> ProbeWebServerAsync();
}
