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
    private string? _ownedConfigPath;
    private string? _startupLogPath;
    private long _startupLogOffset;
    private string? _startupFailureDescription;
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
    public string ConfigStatusDescription { get; private set; } = "尚未取得配置状态";
    public string? OwnedRuntimeDirectory { get; private set; }
    public RuntimeIdentityDto? Identity { get; private set; }
    public DaemonOwnership Ownership { get; private set; }
    public string OwnershipDescription => Ownership switch {
        DaemonOwnership.SpawnedByWinUI => "由 Aura 启动",
        DaemonOwnership.AttachedPreExisting => "连接到已有服务",
        _ => "未连接"
    };
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
        ConfigStatusDescription = status.Data == null ? "核心数据不可用" :
            status.Data.Config.Healthy ? "配置正常" : "配置热重载失败：" + status.Data.Config.LastError;
        IsDaemonRunning = status.IsOnline;
        CoreReady = status.IsOnline && Identity is { Service: "aura_daemon", ProcessId: > 0 } && !string.IsNullOrEmpty(Identity.InstanceId);
        if (CoreReady) _startupFailureDescription = null;
        WebSuppressed = status.Data?.StudioWeb.Suppressed == true;
        if (!CoreReady || WebSuppressed || previousInstance != Identity?.InstanceId) IsWebServerReady = false;
        if (_child is { HasExited: false } && CoreReady && Identity!.InstanceId == _child.InstanceId && Identity.ProcessId == _child.Id)
            Ownership = DaemonOwnership.SpawnedByWinUI;
        else if (status.IsOnline) Ownership = DaemonOwnership.AttachedPreExisting;
        else if (_child is null || _child.HasExited) Ownership = DaemonOwnership.None;
        UpdateStatus(CoreReady ? status.Data!.Config.Healthy
                ? $"核心已就绪 · {OwnershipDescription} · {Identity!.ProductVersion}"
                : $"配置热重载失败，仍使用先前有效配置：{status.Data.Config.LastError}" :
            status.IsOnline ? "外部核心版本不兼容，仅可查看；请在外部升级后重新连接" : _startupFailureDescription ?? status.ErrorMessage);
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
                    _startupFailureDescription = null;
                    UpdateStatus("正在准备并启动后台核心...");
                    var layout = await Task.Run(_prepare, _shutdown.Token);
                    _shutdown.Token.ThrowIfCancellationRequested();
                    _ownedConfigPath = layout.ConfigPath;
                    _startupLogPath = Path.Combine(layout.WorkingDirectory, "aura_daemon.log");
                    try { _startupLogOffset = File.Exists(_startupLogPath) ? new FileInfo(_startupLogPath).Length : 0; }
                    catch (IOException) { _startupLogOffset = 0; }
                    catch (UnauthorizedAccessException) { _startupLogOffset = 0; }
                    _child = _start(layout);
                    OwnedRuntimeDirectory = layout.RuntimeDirectory;
                }
                using var deadline = CancellationTokenSource.CreateLinkedTokenSource(_shutdown.Token);
                deadline.CancelAfter(TimeSpan.FromSeconds(10));
                while (!deadline.IsCancellationRequested)
                {
                    await RefreshAsync(deadline.Token);
                    if (CoreReady) return;
                    if (_child.HasExited) { _startupFailureDescription = DescribeStartupExit(); UpdateStatus(_startupFailureDescription); return; }
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

    private string DescribeStartupExit()
    {
        var fallback = $"后台核心已退出；配置文件：{_ownedConfigPath}；请查看数据目录中的 aura_daemon.log";
        try
        {
            if (_startupLogPath is null || !File.Exists(_startupLogPath)) return fallback;
            using var log = new FileStream(_startupLogPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            if (log.Length < _startupLogOffset || log.Length - _startupLogOffset > 65536) return fallback;
            log.Position = _startupLogOffset;
            using var reader = new StreamReader(log);
            var appended = reader.ReadToEnd();
            if (!appended.Contains("FATAL: 配置文件加载或校验失败", StringComparison.Ordinal)) return fallback;
            var reason = appended.Split('\n').Select(line => line.Trim())
                .LastOrDefault(line => line.Contains("ERROR", StringComparison.Ordinal) &&
                    !line.Contains("FATAL:", StringComparison.Ordinal));
            if (reason is null) return fallback;
            return $"配置加载失败：{reason[..Math.Min(reason.Length, 300)]}；配置文件：{_ownedConfigPath}";
        }
        catch (IOException) { return fallback; }
        catch (UnauthorizedAccessException) { return fallback; }
    }
    public static bool CheckMutexExists()
    {
        var handle = OpenMutex(0x00100000, false, @"Local\RogFalchionAceHfxDaemonMutex");
        if (handle == IntPtr.Zero) return false;
        CloseHandle(handle); return true;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr OpenMutex(uint access, bool inherit, string name);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
}
