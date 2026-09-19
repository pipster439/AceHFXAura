using System;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Aura_WinUI.Services;

public sealed class DaemonSupervisor : IDaemonSupervisor
{
    private const uint SYNCHRONIZE = 0x00100000;
    private const string DAEMON_MUTEX_NAME = @"Local\RogFalchionAceHfxDaemonMutex";

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr OpenMutex(uint dwDesiredAccess, bool bInheritHandle, string lpName);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);

    private readonly HttpClient _http = new() { Timeout = TimeSpan.FromMilliseconds(1500) };
    private Process? _spawnedProcess;
    private string _status = "未初始化";

    public bool IsDaemonRunning { get; private set; }
    public bool IsWebServerReady { get; private set; }
    public string StatusDescription => _status;

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

    public async Task EnsureStartedAsync()
    {
        UpdateStatus("正在探测后台核心状态...");

        // 1. 检查守护进程 Mutex
        if (CheckMutexExists())
        {
            IsDaemonRunning = true;
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

        // 2. 未运行，寻找可执行文件并启动
        string? exePath = FindDaemonExecutable();
        if (string.IsNullOrEmpty(exePath))
        {
            UpdateStatus("未找到 aura_daemon.exe 可执行文件");
            IsDaemonRunning = false;
            return;
        }

        UpdateStatus("正在启动后台守护进程...");
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = exePath,
                WorkingDirectory = Path.GetDirectoryName(exePath) ?? string.Empty,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            _spawnedProcess = Process.Start(psi);
            IsDaemonRunning = true;

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
        }
    }

    public Task StopAsync()
    {
        if (_spawnedProcess != null && !_spawnedProcess.HasExited)
        {
            try
            {
                _spawnedProcess.Kill(true);
                _spawnedProcess.WaitForExit(2000);
            }
            catch
            {
                // 忽略退出异常
            }
            _spawnedProcess = null;
        }

        IsDaemonRunning = false;
        IsWebServerReady = false;
        UpdateStatus("守护进程已停止");
        return Task.CompletedTask;
    }

    private void UpdateStatus(string desc)
    {
        _status = desc;
        StatusChanged?.Invoke(desc);
    }

    private static string? FindDaemonExecutable()
    {
        // 1. 沿当前执行目录逐级向上查找 (支持 bin/x64/Debug 任意深层路径定位仓库根目录)
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null && dir.Exists)
        {
            string rootCandidate = Path.Combine(dir.FullName, "aura_daemon.exe");
            if (File.Exists(rootCandidate))
            {
                return rootCandidate;
            }
            string buildCandidate = Path.Combine(dir.FullName, "build", "Release", "aura_daemon.exe");
            if (File.Exists(buildCandidate))
            {
                return buildCandidate;
            }
            dir = dir.Parent;
        }

        // 2. 检查单文件运行时缓存目录 (%LOCALAPPDATA%\Aura\runtime\aura_daemon.exe)
        string localRuntime = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Aura", "runtime", "aura_daemon.exe");
        if (File.Exists(localRuntime))
        {
            return localRuntime;
        }

        return null;
    }
}
