using System.ServiceProcess;
using AceHFX.AsusPlatform.Ipc;

namespace AceHFX.Service;

public sealed class ServiceHost : ServiceBase
{
    private readonly CancellationTokenSource _stop = new();
    private readonly StructuredLog _log = new(false);
    private Task? _listener;
    public ServiceHost()
    {
        ServiceName = Protocol.ServiceName;
        CanStop = true; CanShutdown = true; AutoLog = true;
    }
    protected override void OnStart(string[] args)
    {
        if (!NativeIdentity().IsSystem) throw new InvalidOperationException("LocalSystemRequired");
        _log.Write("ServiceStartup", new { version = Version(), identity = NativeIdentity() });
        _listener = CreateServer(Protocol.PipeName, _log).RunAsync(_stop.Token);
        _ = _listener.ContinueWith(task =>
        {
            _log.Write("ServiceListenerTerminated", new { faulted = task.IsFaulted }, true);
            if (!_stop.IsCancellationRequested) { ExitCode = 1; Stop(); }
        }, TaskScheduler.Default);
    }
    protected override void OnStop()
    {
        _stop.Cancel();
        if (_listener != null) { RequestAdditionalTime(10000); _listener.GetAwaiter().GetResult(); }
        _log.Write("ServiceShutdown");
    }
    protected override void OnShutdown() => OnStop();
    protected override void Dispose(bool disposing) { if (disposing) _stop.Dispose(); base.Dispose(disposing); }
    internal static AceHFX.AsusPlatform.SecurityContext NativeIdentity() => new AceHFX.AsusPlatform.Runtime.AsusRuntimeDetector().SecurityContext();
    internal static string Version() => typeof(ServiceHost).Assembly.GetCustomAttributes(typeof(System.Reflection.AssemblyInformationalVersionAttribute), false)
        .OfType<System.Reflection.AssemblyInformationalVersionAttribute>().Single().InformationalVersion;
    internal static SecurePipeServer CreateServer(string pipeName, StructuredLog log) =>
        new(pipeName, new(), new PlatformCapabilityProvider(log), new(Protocol.Version, Version(), NativeIdentity()), log,
            new CoolingReadOnlyProvider(log));
}
