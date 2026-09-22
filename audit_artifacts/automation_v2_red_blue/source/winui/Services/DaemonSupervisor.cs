using System;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace Aura_WinUI.Services;

public sealed class DaemonSupervisor : IDaemonSupervisor
{
    private const uint SYNCHRONIZE = 0x00100000;
    private const string DAEMON_MUTEX_NAME = @"Local\RogFalchionAceHfxDaemonMutex";
    private const string DAEMON_SHUTDOWN_EVENT_NAME = @"Local\RogFalchionAceHfxDaemonShutdownEvent";

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr OpenMutex(uint dwDesiredAccess, bool bInheritHandle, string lpName);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);

    private readonly HttpClient _http = new() { Timeout = TimeSpan.FromMilliseconds(1500) };
    private readonly object _taskLock = new();
    private Task? _inFlightStartTask;

    private Process? _spawnedProcess;
    private string _status = "未初始化";

    public bool IsDaemonRunning { get; private set; }
    public bool IsWebServerReady { get; private set; }
    public string StatusDescription => _status;
    public DaemonOwnership Ownership { get; private set; } = DaemonOwnership.None;

    public event Action<string>? StatusChanged;

    private static DaemonSupervisor? _instance;
    public static DaemonSupervisor Instance => _instance ??= new DaemonSupervisor();

    public static bool CheckMutexExists()
    {
        IntPtr hMutex = OpenMutex(SYNCHRONIZE, false, DAEMON_MUTEX_NAME);
        if (hMutex != IntPtr.Zero)
        {
            CloseHandle(hMutex);
            return true;
        }
        return false;
    }

    public async Task<bool> ProbeWebServerAsync()
    {
        try
        {
            var res = await _http.GetAsync("http://127.0.0.1:19898/api/status");
            if (res.IsSuccessStatusCode)
            {
                IsWebServerReady = true;
                return true;
            }
        }
        catch
        {
            // Web 接口未就绪
        }

        IsWebServerReady = false;
        return false;
    }

    public Task EnsureStartedAsync()
    {
        // 串行化并发调用 (App.OnLaunched、HomePage.Loaded、StudioPage.Loaded 等共享同一启动 Task)
        lock (_taskLock)
        {
            if (_inFlightStartTask != null && !_inFlightStartTask.IsCompleted)
            {
                return _inFlightStartTask;
            }

            _inFlightStartTask = EnsureStartedCoreAsync();
            return _inFlightStartTask;
        }
    }

    private async Task EnsureStartedCoreAsync()
    {
        UpdateStatus("正在探测后台核心状态...");

        // 1. 检查守护进程 Mutex
        if (CheckMutexExists())
        {
            IsDaemonRunning = true;
            if (_spawnedProcess == null)
            {
                Ownership = DaemonOwnership.AttachedPreExisting;
            }
            UpdateStatus("守护进程已在运行中");

            // 检查 Web 服务是否可通
            if (await ProbeWebServerAsync())
            {
                UpdateStatus("核心与 Web 服务已就绪");
            }
            else
            {
                UpdateStatus("守护进程运行中 (Web 服务就绪中...)");
            }
            return;
        }

        // 2. 互斥体不存在，使用 RuntimeLayoutResolver 解析规范布局并启动
        var layout = RuntimeLayoutResolver.Resolve();
        if (layout == null)
        {
            UpdateStatus("未找到 aura_daemon.exe 可执行文件");
            IsDaemonRunning = false;
            Ownership = DaemonOwnership.None;
            return;
        }

        UpdateStatus("正在启动后台守护进程...");
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = layout.DaemonExecutablePath,
                WorkingDirectory = layout.WorkingDirectory,
                Arguments = $"--config \"{layout.ConfigPath}\" --keymap \"{layout.KeymapPath}\"",
                UseShellExecute = false,
                CreateNoWindow = true
            };

            var proc = Process.Start(psi);
            if (proc != null)
            {
                _spawnedProcess = proc;
                Ownership = DaemonOwnership.SpawnedByWinUI;
                IsDaemonRunning = true;
            }
            else
            {
                UpdateStatus("守护进程启动失败 (未能创建进程句柄)");
                IsDaemonRunning = false;
                Ownership = DaemonOwnership.None;
                return;
            }

            // 等待 Web 端口开放 (最多 5 秒)
            for (int i = 0; i < 25; i++)
            {
                await Task.Delay(200);
                if (await ProbeWebServerAsync())
                {
                    UpdateStatus("守护进程与 Web 服务已就绪");
                    return;
                }
            }

            UpdateStatus("守护进程已启动，等待 Web 服务响应...");
        }
        catch (Exception ex)
        {
            UpdateStatus($"启动失败: {ex.Message}");
            IsDaemonRunning = false;
            Ownership = DaemonOwnership.None;
        }
    }

    public async Task StopAsync()
    {
        UpdateStatus("正在请求守护进程退出...");

        // 1. 发送优雅停机通知 (Named Event: RogFalchionAceHfxDaemonShutdownEvent)
        try
        {
            if (EventWaitHandle.TryOpenExisting(DAEMON_SHUTDOWN_EVENT_NAME, out var shutdownEvent))
            {
                using (shutdownEvent)
                {
                    shutdownEvent.Set();
                }
            }
        }
        catch
        {
            // 忽略事件打开异常
        }

        // 2. 根据所有权执行退出等待与兜底策略
        if (Ownership == DaemonOwnership.SpawnedByWinUI && _spawnedProcess != null && !_spawnedProcess.HasExited)
        {
            try
            {
                // 等待进程优雅退出 (最长等待 3.5 秒)
                using var cts = new CancellationTokenSource(TimeSpan.FromMilliseconds(3500));
                await _spawnedProcess.WaitForExitAsync(cts.Token);
            }
            catch (OperationCanceledException)
            {
                // 优雅退出超时，仅对 WinUI 自己拉起的进程执行最后兜底强杀
                try
                {
                    if (!_spawnedProcess.HasExited)
                    {
                        _spawnedProcess.Kill(true);
                    }
                }
                catch
                {
                    // 忽略强杀异常
                }
            }
            catch
            {
                // 忽略等待异常
            }
            _spawnedProcess = null;
        }
        else if (Ownership == DaemonOwnership.AttachedPreExisting)
        {
            // 外部预先启动的守护进程：WinUI 发送停机通知并等待其释放 Mutex，超时绝不越权强杀
            bool mutexReleased = false;
            for (int i = 0; i < 35; i++)
            {
                await Task.Delay(100);
                if (!CheckMutexExists())
                {
                    mutexReleased = true;
                    break;
                }
            }

            if (!mutexReleased)
            {
                // 超时后互斥体依然存在：不强杀，保持运行状态，不伪装成已停止
                IsDaemonRunning = true;
                UpdateStatus("守护进程未在超时时间内退出，已保留其运行");
                return;
            }
        }

        Ownership = DaemonOwnership.None;
        IsDaemonRunning = false;
        IsWebServerReady = false;
        UpdateStatus("守护进程已停止");
    }

    private void UpdateStatus(string desc)
    {
        _status = desc;
        StatusChanged?.Invoke(desc);
    }
}
