using System;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using WinRT.Interop;
using Aura_WinUI.Common;
using Aura_WinUI.Pages;
using Aura_WinUI.Services;

namespace Aura_WinUI;

public sealed partial class MainWindow : Window
{
    public static MainWindow? CurrentInstance { get; private set; }
    public static Frame? CurrentNavFrame { get; private set; }
    public static NavigationView? CurrentNavView { get; private set; }

    private readonly TrayIconManager _trayIcon;

    public MainWindow()
    {
        CurrentInstance = this;
        InitializeComponent();

        CurrentNavFrame = NavFrame;
        CurrentNavView = NavView;

        // 1. 初始化窗口 DPI 缩放与 MinWidth/MinHeight 物理硬性约束 (600x500)
        WindowHelper.InitializeWindowConstraints(this, preferredWidthDip: 1060, preferredHeightDip: 720, minWidthDip: 600, minHeightDip: 500);

        // 2. 自定义系统原生 TitleBar 扩展与控件绑定
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);

        if (AppWindowTitleBar.IsCustomizationSupported())
        {
            AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Tall;
        }

        try
        {
            AppWindow.SetIcon("Assets/AppIcon.ico");
        }
        catch
        {
            // 忽略图标设置异常
        }

        // 3. 拦截窗口右上角关闭按钮，默认最小化至系统托盘保活
        AppWindow.Closing += MainWindow_Closing;

        // 4. 初始化系统托盘管理
        _trayIcon = new TrayIconManager(this, ShowAndBringToFront, App.ExitApplication);

        // 5. 监听后台守护进程状态变化并同步托盘提示
        DaemonSupervisor.Instance.StatusChanged += (status) =>
        {
            DispatcherQueue.TryEnqueue(() =>
            {
                _trayIcon.UpdateStatus(status);
            });
        };

        // 6. 默认进入首页
        NavFrame.Navigate(typeof(HomePage));
    }

    private void MainWindow_Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (TrayIconManager.MinimizeToTrayEnabled)
        {
            args.Cancel = true;
            AppWindow.Hide();
        }
        else
        {
            App.ExitApplication();
        }
    }

    public void ShowAndBringToFront()
    {
        AppWindow.Show();
        IntPtr hWnd = WindowNative.GetWindowHandle(this);
        WindowHelper.ShowWindow(hWnd, WindowHelper.SW_RESTORE);
        WindowHelper.SetForegroundWindow(hWnd);
    }

    private void TitleBar_PaneToggleRequested(TitleBar sender, object args)
    {
        NavView.IsPaneOpen = !NavView.IsPaneOpen;
    }

    private void TitleBar_BackRequested(TitleBar sender, object args)
    {
        if (NavFrame.CanGoBack)
        {
            NavFrame.GoBack();
        }
    }

    private void NavView_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.IsSettingsSelected)
        {
            NavFrame.Navigate(typeof(SettingsPage));
            return;
        }

        if (args.SelectedItem is NavigationViewItem item)
        {
            switch (item.Tag?.ToString())
            {
                case "home":
                    NavFrame.Navigate(typeof(HomePage));
                    break;
                case "lighting":
                    NavFrame.Navigate(typeof(LightingPage));
                    break;
                case "automation":
                    NavFrame.Navigate(typeof(AutomationPage));
                    break;
                case "gsi":
                    NavFrame.Navigate(typeof(GameIntegrationPage));
                    break;
                case "studio":
                    NavFrame.Navigate(typeof(StudioPage));
                    break;
                default:
                    NavFrame.Navigate(typeof(HomePage));
                    break;
            }
        }
    }

    public void DisposeTray()
    {
        _trayIcon.Dispose();
    }
}
