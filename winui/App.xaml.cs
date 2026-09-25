using System;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.Windows.AppLifecycle;
using Aura_WinUI.Services;
using Aura_WinUI.Validation;

namespace Aura_WinUI;

public partial class App : Application
{
    private static Window? _window;
    public static Window? MainWindowInstance => _window;

    private static readonly object _activationLock = new();
    private static bool _hasPendingActivation = false;

    public App()
    {
        Environment.SetEnvironmentVariable("WEBVIEW2_USER_DATA_FOLDER", System.IO.Path.Combine(RuntimeLayoutResolver.DataRoot, "WebView2"));
        InitializeComponent();
        UnhandledException += (sender, e) =>
        {
            Console.Error.WriteLine($"[FATAL] Xaml UnhandledException: {e.Exception}");
            ClientSettings.Log(e.Exception);
        };
    }

    public static void HandleSecondaryActivation(AppActivationArguments args)
    {
        lock (_activationLock)
        {
            if (IsShuttingDown) return;
            if (_window is MainWindow mw && mw.DispatcherQueue != null)
            {
                mw.DispatcherQueue.TryEnqueue(() =>
                {
                    if (!IsShuttingDown) mw.ShowAndBringToFront();
                });
            }
            else
            {
                // MainWindow 尚未构建完毕，记录未决激活，待窗口就绪后恢复
                _hasPendingActivation = true;
            }
        }
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        if (IsShuttingDown) return;
        if (_window is MainWindow existing) { existing.ShowAndBringToFront(); return; }
        // 1. 启动后台守护进程探测与自动拉起 (异步非阻塞，内部通过共享 Task 实现串行化)
        if (!MagneticLayoutValidation.Requested &&
            Environment.GetEnvironmentVariable("AURA_MAGNETIC_VALIDATION_OFFLINE") != "1")
            _ = DaemonSupervisor.Instance.EnsureStartedAsync();

        // 2. 创建并激活主窗口
        var mainWindow = new MainWindow();
        _window = mainWindow;
        mainWindow.Activate();
        if (Validation.LayoutValidation.Requested) _ = Validation.LayoutValidation.RunAsync(mainWindow);
        else if (Validation.StudioValidation.Requested) _ = Validation.StudioValidation.RunAsync(mainWindow);
        else if (MagneticLayoutValidation.Requested) _ = MagneticLayoutValidation.RunAsync(mainWindow);

        // 3. 检查并处理窗口创建前可能已到达的激活事件
        lock (_activationLock)
        {
            if (_hasPendingActivation)
            {
                _hasPendingActivation = false;
                mainWindow.ShowAndBringToFront();
            }
        }
    }

    public static bool IsShuttingDown { get; private set; }

    private static readonly object _shutdownLock = new();
    private static Task? _shutdownTask;

    public static Task RequestExit()
    {
        lock (_shutdownLock)
        {
            if (_shutdownTask != null)
            {
                // 已有 shutdown 正在进行中，直接返回该 Task，防止多次点击导致并发重入
                return _shutdownTask;
            }

            IsShuttingDown = true;
            _shutdownTask = PerformShutdownAsync();
            return _shutdownTask;
        }
    }

    private static async Task PerformShutdownAsync()
    {
        try
        {
            // 1. 发送优雅停机请求并等待守护进程完成清理与退出
            await DaemonSupervisor.Instance.StopAsync();
        }
        catch
        {
            // 忽略停机异常，保障后续清理与应用退出执行
        }

        try
        {
            // 2. 守护进程处理完毕后再注销托盘图标与窗口子类化钩子
            if (_window is MainWindow mw)
            {
                mw.DisposeTray();
            }
        }
        catch
        {
            // 忽略托盘注销异常
        }

        // 3. 最终退出 XAML 应用程序
        Current.Exit();
    }
}
