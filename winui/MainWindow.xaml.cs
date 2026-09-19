using System;
using System.Linq;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
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
        _trayIcon = new TrayIconManager(this, ShowAndBringToFront, () => { _ = App.RequestExit(); });

        // 5. 监听后台守护进程状态变化并同步托盘提示
        DaemonSupervisor.Instance.StatusChanged += (status) =>
        {
            DispatcherQueue.TryEnqueue(() =>
            {
                _trayIcon.UpdateStatus(status);
            });
        };

        // 6. 监听导航完成事件，同步更新 TitleBar 返回按钮可见性与 NavigationView 选中项
        NavFrame.Navigated += NavFrame_Navigated;

        // 7. 默认进入首页
        NavigateTo(typeof(HomePage));

        // 8. 自动化导航压力验证钩子 (支持环境变量或 G:\Aura\nav_stress_trigger.txt 触发)
        int stressCycles = 0;
        string triggerPath = @"G:\Aura\nav_stress_trigger.txt";
        if (System.IO.File.Exists(triggerPath))
        {
            try
            {
                string content = System.IO.File.ReadAllText(triggerPath).Trim();
                int.TryParse(content, out stressCycles);
            }
            catch { }
        }
        if (stressCycles <= 0)
        {
            string? stressCyclesEnv = Environment.GetEnvironmentVariable("AURA_TEST_NAV_STRESS");
            int.TryParse(stressCyclesEnv, out stressCycles);
        }
        if (stressCycles > 0)
        {
            _ = RunNavStressTestAsync(stressCycles);
        }
    }

    private void MainWindow_Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (App.IsShuttingDown)
        {
            // 正在执行程序化停机中：不再 Cancel 关闭流程，允许系统顺利关闭窗口
            return;
        }

        if (TrayIconManager.MinimizeToTrayEnabled)
        {
            args.Cancel = true;
            AppWindow.Hide();
        }
        else
        {
            // 非最小化到托盘场景：必须先 Cancel 阻止操作系统内核直接销毁视窗，
            // 隐藏窗口后启动受控的非重入异步停机流程
            args.Cancel = true;
            AppWindow.Hide();
            _ = App.RequestExit();
        }
    }

    public void ShowAndBringToFront()
    {
        AppWindow.Show();
        IntPtr hWnd = WindowNative.GetWindowHandle(this);
        WindowHelper.ShowWindow(hWnd, WindowHelper.SW_RESTORE);
        WindowHelper.SetForegroundWindow(hWnd);
        Activate();
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

    private void NavFrame_Navigated(object sender, NavigationEventArgs e)
    {
        // 1. 同步 TitleBar 返回按钮状态 (替代单向 x:Bind)
        AppTitleBar.IsBackButtonVisible = NavFrame.CanGoBack;

        // 2. 同步 NavigationView 选中状态
        if (e.SourcePageType == typeof(SettingsPage))
        {
            NavView.SelectedItem = NavView.SettingsItem;
        }
        else
        {
            var matchedItem = NavView.MenuItems
                .OfType<NavigationViewItem>()
                .FirstOrDefault(item => GetPageTypeForTag(item.Tag?.ToString()) == e.SourcePageType);

            if (matchedItem != null && !Equals(NavView.SelectedItem, matchedItem))
            {
                NavView.SelectedItem = matchedItem;
            }
        }
    }

    public void NavigateTo(Type targetType)
    {
        // 避免对当前已呈现的 PageType 重复 Navigate
        if (NavFrame.CurrentSourcePageType == targetType)
        {
            return;
        }

        // 清理 BackStack 中该类型的历史实例，防止在顶层菜单间来回点击导致历史堆栈和 WebView2 页面无限堆积
        for (int i = NavFrame.BackStack.Count - 1; i >= 0; i--)
        {
            if (NavFrame.BackStack[i].SourcePageType == targetType)
            {
                NavFrame.BackStack.RemoveAt(i);
            }
        }

        NavFrame.Navigate(targetType);
    }

    private void NavView_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.IsSettingsSelected)
        {
            NavigateTo(typeof(SettingsPage));
            return;
        }

        if (args.SelectedItem is NavigationViewItem item)
        {
            Type? pageType = GetPageTypeForTag(item.Tag?.ToString());
            if (pageType != null)
            {
                NavigateTo(pageType);
            }
        }
    }

    private static Type? GetPageTypeForTag(string? tag)
    {
        return tag switch
        {
            "home" => typeof(HomePage),
            "lighting" => typeof(LightingPage),
            "automation" => typeof(AutomationPage),
            "gsi" => typeof(GameIntegrationPage),
            "studio" => typeof(StudioPage),
            _ => null
        };
    }

    public void DisposeTray()
    {
        _trayIcon.Dispose();
    }

    private async Task RunNavStressTestAsync(int cycles)
    {
        string resultFile = @"G:\Aura\nav_stress_result.txt";
        Console.WriteLine($"[NAV_STRESS] Starting {cycles} navigation cycles (Home <-> Lighting)...");
        try
        {
            System.IO.File.WriteAllText(resultFile, $"STARTING {cycles} cycles at {DateTime.Now}\n");
            await Task.Delay(800);
            for (int i = 1; i <= cycles; i++)
            {
                DispatcherQueue?.TryEnqueue(() => NavigateTo(typeof(LightingPage)));
                await Task.Delay(300);
                DispatcherQueue?.TryEnqueue(() => NavigateTo(typeof(HomePage)));
                await Task.Delay(250);
                Console.WriteLine($"[NAV_STRESS] Cycle {i}/{cycles} passed.");
                System.IO.File.AppendAllText(resultFile, $"Cycle {i}/{cycles} passed at {DateTime.Now}\n");
            }
            Console.WriteLine($"[NAV_STRESS] All {cycles} navigation cycles completed successfully!");
            System.IO.File.AppendAllText(resultFile, $"SUCCESS {cycles} cycles completed at {DateTime.Now}\n");
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[NAV_STRESS_ERROR] {ex}");
            System.IO.File.AppendAllText(resultFile, $"FAILED: {ex}\n");
        }
        finally
        {
            bool autoExit = Environment.GetEnvironmentVariable("AURA_TEST_NAV_STRESS_AUTOEXIT") == "1"
                || System.IO.File.Exists(@"G:\Aura\nav_stress_trigger.txt");
            if (autoExit)
            {
                await Task.Delay(500);
                DispatcherQueue?.TryEnqueue(() =>
                {
                    Application.Current.Exit();
                });
            }
        }
    }
}
