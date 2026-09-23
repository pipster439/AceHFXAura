using System.Runtime.InteropServices;

namespace Aura_WinUI.Services;

public sealed class DaemonSupervisor : IDaemonSupervisor
{
    private readonly IAuraControlClient _client;
    private readonly AuraWebClient _web;
    private readonly Func<RuntimeLayout> _prepare;
    private readonly Func<RuntimeLayout, IOwnedDaemonProcess> _start;
    private readonly Func<bool> _mutexExists;
    private readonly SemaphoreSlim _lifecycle = new(1, 1);
    private readonly CancellationTokenSource _shutdown = new();
    private IOwnedDaemonProcess? _child;
    private static readonly Lazy<DaemonSupervisor> Singleton = new(() => new());
    public static DaemonSupervisor Instance => Singleton.Value;

    public DaemonSupervisor(IAuraControlClient? client = null, AuraWebClient? web = null,
        Func<RuntimeLayout>? prepare = null, Func<RuntimeLayout, IOwnedDaemonProcess>? start = null, Func<bool>? mutexExists = null)
    {
        _client = client ?? AuraControlClient.Instance; _web = web ?? new();
        _prepare = prepare ?? (() => { var layout = RuntimeLayoutResolver.Resolve(); RuntimePreparer.Prepare(layout); return layout; });
        _start = start ?? (layout => new OwnedDaemonProcess(layout)); _mutexExists = mutexExists ?? CheckMutexExists;
    }
    public bool IsDaemonRunning { get; private set; }
    public bool CoreReady { get; private set; }
    public bool IsWebServerReady { get; private set; }
    public bool StudioWebReady => IsWebServerReady;
    public bool WebSuppressed { get; private set; }
    public string StatusDescription { get; private set; } = "未初始化";
    public string? OwnedRuntimeDirectory { get; private set; }
    public RuntimeIdentityDto? Identity { get; private set; }
    public DaemonOwnership Ownership { get; private set; }
    public event Action<string>? StatusChanged;

    public async Task RefreshAsync(CancellationToken token = default)
    {
        var status = await _client.GetRuntimeStatusAsync(token);
        if (token.IsCancellationRequested || _shutdown.IsCancellationRequested) return;
        AcceptStatus(status);
    }

    internal void AcceptStatus(RuntimeStatus status)
    {
        if (_shutdown.IsCancellationRequested) return;
        var previousInstance = Identity?.InstanceId;
        Identity = status.Data?.Identity;
        IsDaemonRunning = status.IsOnline;
        CoreReady = status.IsOnline && Identity is { Service: "aura_daemon", ProcessId: > 0 } && !string.IsNullOrEmpty(Identity.InstanceId);
        WebSuppressed = status.Data?.StudioWeb.Suppressed == true;
        if (!CoreReady || WebSuppressed || previousInstance != Identity?.InstanceId) IsWebServerReady = false;
        if (_child is { HasExited: false } && CoreReady && Identity!.InstanceId == _child.InstanceId && Identity.ProcessId == _child.Id)
            Ownership = DaemonOwnership.SpawnedByWinUI;
        else if (status.IsOnline) Ownership = DaemonOwnership.AttachedPreExisting;
        else if (_child is null || _child.HasExited) Ownership = DaemonOwnership.None;
        UpdateStatus(CoreReady ? $"核心已就绪 · {Ownership} · {Identity!.ProductVersion}" :
            status.IsOnline ? "外部核心版本不兼容，仅可查看；请在外部升级后重新连接" : status.ErrorMessage);
    }

    public async Task<bool> ProbeWebServerAsync()
    {
        var result = await _web.GetStatusAsync(_shutdown.Token);
        IsWebServerReady = CoreReady && !WebSuppressed && result.IsSuccess && result.Value is { Service: "aura_web_ui", WebApiVersion: 2 } value &&
            Identity != null && value.DaemonInstanceId == Identity.InstanceId;
        return IsWebServerReady;
    }

    public async Task EnsureStartedAsync()
    {
        if (_shutdown.IsCancellationRequested) return;
        try
        {
            await _lifecycle.WaitAsync(_shutdown.Token);
            try
            {
                await RefreshAsync(_shutdown.Token);
                if (IsDaemonRunning) return; // Never acquire ownership by attaching.
                if (_child?.HasExited == true) { _child.Dispose(); _child = null; }
                if (_child == null && _mutexExists())
                { UpdateStatus("检测到外部核心，但 Control API 尚未就绪；不会启动或停止它"); return; }
                if (_child == null)
                {
                    UpdateStatus("正在准备并启动后台核心...");
                    var layout = await Task.Run(_prepare, _shutdown.Token);
                    _shutdown.Token.ThrowIfCancellationRequested();
                    _child = _start(layout);
                    OwnedRuntimeDirectory = layout.RuntimeDirectory;
                }
                using var deadline = CancellationTokenSource.CreateLinkedTokenSource(_shutdown.Token);
                deadline.CancelAfter(TimeSpan.FromSeconds(10));
                while (!deadline.IsCancellationRequested)
                {
                    await RefreshAsync(deadline.Token);
                    if (CoreReady) return;
                    if (_child.HasExited) { UpdateStatus("后台核心已退出，请查看数据目录中的 aura_daemon.log"); return; }
                    await Task.Delay(200, deadline.Token);
                }
            }
            finally { _lifecycle.Release(); }
        }
        catch (OperationCanceledException) { if (!_shutdown.IsCancellationRequested) UpdateStatus("核心启动等待超时，可重新连接；不会重复启动进程"); }
        catch (Exception ex) { ClientSettings.Log(ex); UpdateStatus("核心启动失败: " + ex.Message); }
    }

    public async Task StopAsync()
    {
        _shutdown.Cancel();
        await _lifecycle.WaitAsync();
        try
        {
            // This retained object can only represent our own spawned child, including a failed startup.
            // An attached service never receives an event or a termination request.
            if (_child != null)
            {
                try { await _child.StopAsync(); }
                finally { _child.Dispose(); _child = null; }
            }
            var attached = Ownership == DaemonOwnership.AttachedPreExisting;
            CoreReady = false; IsWebServerReady = false; Ownership = DaemonOwnership.None;
            IsDaemonRunning = attached;
            UpdateStatus(attached ? "客户端已断开，外部核心保持运行" : "后台核心已停止");
        }
        finally { _lifecycle.Release(); }
    }
    private void UpdateStatus(string status) { StatusDescription = status; StatusChanged?.Invoke(status); }
    public static bool CheckMutexExists()
    {
        var handle = OpenMutex(0x00100000, false, @"Local\RogFalchionAceHfxDaemonMutex");
        if (handle == IntPtr.Zero) return false;
        CloseHandle(handle); return true;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr OpenMutex(uint access, bool inherit, string name);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
}
