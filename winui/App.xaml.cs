using System;
using System.Diagnostics;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.Windows.AppLifecycle;
using Aura_WinUI.Services;

namespace Aura_WinUI;

public partial class App : Application
{
    private static Window? _window;
    public static Window? MainWindowInstance => _window;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        // 1. 单实例注册与重定向
        var mainInstance = AppInstance.FindOrRegisterForKey("AceHFXAura_WinUI_App");
        if (!mainInstance.IsCurrent)
        {
            var activatedArgs = AppInstance.GetCurrent().GetActivatedEventArgs();
            _ = mainInstance.RedirectActivationToAsync(activatedArgs);
            Process.GetCurrentProcess().Kill();
            return;
        }

        mainInstance.Activated += (s, e) =>
        {
            _window?.DispatcherQueue.TryEnqueue(() =>
            {
                if (_window is MainWindow mw)
                {
                    mw.ShowAndBringToFront();
                }
            });
        };

        // 2. 启动后台守护进程探测与自动拉起 (异步非阻塞)
        _ = DaemonSupervisor.Instance.EnsureStartedAsync();

        // 3. 创建并激活主窗口
        _window = new MainWindow();
        _window.Activate();
    }

    public static void ExitApplication()
    {
        try
        {
            if (_window is MainWindow mw)
            {
                mw.DisposeTray();
            }

            // 停止后台守护进程监护
            _ = DaemonSupervisor.Instance.StopAsync();
        }
        catch
        {
            // ignore
        }

        Current.Exit();
    }
}
