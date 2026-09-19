using System;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.Windows.AppLifecycle;
using Aura_WinUI.Services;

namespace Aura_WinUI;

public partial class App : Application
{
    private static Window? _window;
    public static Window? MainWindowInstance => _window;

    private static readonly object _activationLock = new();
    private static bool _hasPendingActivation = false;

    public App()
    {
        InitializeComponent();
    }

    public static void HandleSecondaryActivation(AppActivationArguments args)
    {
        lock (_activationLock)
        {
            if (_window is MainWindow mw && mw.DispatcherQueue != null)
            {
                mw.DispatcherQueue.TryEnqueue(() =>
                {
                    mw.ShowAndBringToFront();
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
        // 1. 启动后台守护进程探测与自动拉起 (异步非阻塞，内部通过共享 Task 实现串行化)
        _ = DaemonSupervisor.Instance.EnsureStartedAsync();

        // 2. 创建并激活主窗口
        var mainWindow = new MainWindow();
        _window = mainWindow;
        mainWindow.Activate();

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

    public static async void ExitApplication()
    {
        try
        {
            if (_window is MainWindow mw)
            {
                mw.DisposeTray();
            }

            // 发送优雅停机请求并等待守护进程退出
            await DaemonSupervisor.Instance.StopAsync();
        }
        catch
        {
            // ignore
        }

        Current.Exit();
    }
}
