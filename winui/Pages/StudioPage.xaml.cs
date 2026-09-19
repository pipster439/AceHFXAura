using System;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Web.WebView2.Core;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed partial class StudioPage : Page
{
    private const string STUDIO_URL = "http://127.0.0.1:19898/";
    private bool _isInitialized = false;

    public StudioPage()
    {
        InitializeComponent();
        Loaded += StudioPage_Loaded;
    }

    private async void StudioPage_Loaded(object sender, RoutedEventArgs e)
    {
        if (!_isInitialized)
        {
            await InitializeStudioAsync();
        }
    }

    private async void RetryBtn_Click(object sender, RoutedEventArgs e)
    {
        await InitializeStudioAsync();
    }

    private async Task InitializeStudioAsync()
    {
        LoadingPanel.Visibility = Visibility.Visible;
        LoadingRing.IsActive = true;
        RetryBtn.Visibility = Visibility.Collapsed;
        LoadingStatusText.Text = "正在验证 Aura Web 服务就绪状态...";
        StudioInfoBar.IsOpen = false;

        // 1. 确保后台守护进程与 Web 服务正在运行
        await DaemonSupervisor.Instance.EnsureStartedAsync();

        bool isReady = await DaemonSupervisor.Instance.ProbeWebServerAsync();
        if (!isReady)
        {
            LoadingStatusText.Text = "无法连接至本地 Aura Web 服务 (127.0.0.1:19898)";
            LoadingRing.IsActive = false;
            RetryBtn.Visibility = Visibility.Visible;
            StudioInfoBar.Title = "服务未就绪";
            StudioInfoBar.Message = "Aura 核心或 Web 服务未正常启动，或当前正处于游戏节能静默状态。";
            StudioInfoBar.Severity = InfoBarSeverity.Warning;
            StudioInfoBar.IsOpen = true;
            return;
        }

        LoadingStatusText.Text = "正在初始化 WebView2 渲染内核...";
        try
        {
            await StudioWebView.EnsureCoreWebView2Async();

            // 配置 DevTools 策略: Debug 构建允许调试，Release 默认关闭
#if DEBUG
            StudioWebView.CoreWebView2.Settings.AreDevToolsEnabled = true;
#else
            StudioWebView.CoreWebView2.Settings.AreDevToolsEnabled = false;
#endif
            StudioWebView.CoreWebView2.Settings.IsStatusBarEnabled = false;
            StudioWebView.CoreWebView2.Settings.AreDefaultContextMenusEnabled = true;

            StudioWebView.NavigationCompleted -= StudioWebView_NavigationCompleted;
            StudioWebView.NavigationCompleted += StudioWebView_NavigationCompleted;

            LoadingStatusText.Text = "正在载入 Blockly Studio...";
            StudioWebView.Source = new Uri(STUDIO_URL);
            _isInitialized = true;
        }
        catch (Exception ex)
        {
            LoadingRing.IsActive = false;
            LoadingStatusText.Text = $"WebView2 初始化失败: {ex.Message}";
            RetryBtn.Visibility = Visibility.Visible;
            StudioInfoBar.Title = "WebView2 异常";
            StudioInfoBar.Message = ex.Message;
            StudioInfoBar.Severity = InfoBarSeverity.Error;
            StudioInfoBar.IsOpen = true;
        }
    }

    private void StudioWebView_NavigationCompleted(WebView2 sender, CoreWebView2NavigationCompletedEventArgs args)
    {
        if (args.IsSuccess)
        {
            LoadingPanel.Visibility = Visibility.Collapsed;
            StudioWebView.Visibility = Visibility.Visible;
        }
        else
        {
            LoadingRing.IsActive = false;
            LoadingStatusText.Text = $"网页加载失败 (代码: {args.WebErrorStatus})";
            RetryBtn.Visibility = Visibility.Visible;
            StudioInfoBar.Title = "Studio 加载失败";
            StudioInfoBar.Message = $"无法加载 {STUDIO_URL}，请检查端口 19898 是否被占用。";
            StudioInfoBar.Severity = InfoBarSeverity.Error;
            StudioInfoBar.IsOpen = true;
        }
    }
}
