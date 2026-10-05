using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Microsoft.Web.WebView2.Core;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed partial class StudioPage : Page
{
    private static StudioPage? _host;
    private static bool _openAutomation;
    private bool _initialized, _initializing, _closed, _active, _checking;
    private string? _lastTheme;
    private readonly StudioShellModel _shell = new();
    private bool _settingProjects;
    private readonly DispatcherTimer _retry = new() { Interval = TimeSpan.FromSeconds(3) };
    public StudioPage()
    {
        InitializeComponent();
        NavigationCacheMode = NavigationCacheMode.Required; // One retained editor, preserving unsaved Blockly work.
        _host = this;
        Loaded += (_, _) => { _active = true; _retry.Start(); _ = InitializeStudioAsync(); SyncTheme(); };
        Unloaded += (_, _) => { _active = false; _retry.Stop(); _aiCancel?.Cancel(); };
        ActualThemeChanged += ThemeChanged;
        _retry.Tick += (_, _) => { if (!_initialized) _ = InitializeStudioAsync(); else _ = RefreshAvailabilityAsync(); };
    }
    public static void OpenAutomation()
    {
        _openAutomation = true;
        if (_host?._initialized == true && _host.StudioWebView.CoreWebView2 != null &&
            _host.LoadingPanel.Visibility == Visibility.Collapsed)
        {
            _host.AiPanel.Visibility = Visibility.Collapsed; _host.AdaptAssistant();
            _host.StudioWebView.CoreWebView2.PostWebMessageAsJson("{\"type\":\"open_automation\"}");
            _openAutomation = false;
        }
    }
    private async Task RefreshAvailabilityAsync()
    {
        if (_checking || !_active || _closed) return;
        _checking = true;
        try
        {
            await DaemonSupervisor.Instance.RefreshAsync();
            var ready = await DaemonSupervisor.Instance.ProbeWebServerAsync();
            if (!_active || _closed) return;
            StudioInfoBar.Title = DaemonSupervisor.Instance.WebSuppressed ? "工作室网页服务已被免打扰规则暂停" : "工作室网页服务暂不可用";
            StudioInfoBar.Message = "编辑器草稿保留；服务恢复后可继续保存。核心与 GSI 状态请查看原生页面。";
            StudioInfoBar.IsOpen = !ready;
            RuntimeSummary.Text = $"核心：{(DaemonSupervisor.Instance.CoreReady ? "已连接" : "未连接")} · 工作室：{(ready ? "已连接" : "不可用")}";
        }
        catch (OperationCanceledException) { }
        finally { _checking = false; }
    }
    private async void RetryBtn_Click(object sender, RoutedEventArgs e) => await InitializeStudioAsync(true);
    private string CurrentTheme => MainWindow.CurrentInstance?.Content is FrameworkElement root && root.ActualTheme == ElementTheme.Light ? "light" : "dark";
    private void ThemeChanged(FrameworkElement sender, object args) => SyncTheme();
    public static void HostThemeChanged() => _host?.SyncTheme();
    private void SyncTheme(bool force = false)
    {
        if (_closed || !_initialized || StudioWebView.CoreWebView2 == null) return;
        var theme = CurrentTheme;
        if (!force && _lastTheme == theme) return;
        StudioWebView.CoreWebView2.PostWebMessageAsJson(EmbeddedStudioNavigation.ThemeMessage(theme));
        _lastTheme = theme;
    }
    private async Task InitializeStudioAsync(bool force = false)
    {
        if (Aura_WinUI.Validation.StudioAiValidation.SettingsOnly || _closed || !_active || _initializing || (_initialized && !force)) return;
        _initializing = true;
        try
        {
            LoadingPanel.Visibility = Visibility.Visible; LoadingRing.IsActive = true;
            RetryBtn.Visibility = Visibility.Collapsed; StudioInfoBar.IsOpen = false;
            await DaemonSupervisor.Instance.EnsureStartedAsync();
            var ready = await DaemonSupervisor.Instance.ProbeWebServerAsync();
            if (_closed || !_active) return;
            if (!ready)
            {
                ShowError(DaemonSupervisor.Instance.WebSuppressed ? "工作室网页服务已被免打扰规则暂停；核心与 GSI 可继续运行。" :
                    "工作室网页服务尚未就绪。请检查核心状态；页面会自动重试。");
                return;
            }
            await StudioWebView.EnsureCoreWebView2Async();
            if (!_initialized) await StudioWebView.CoreWebView2.AddScriptToExecuteOnDocumentCreatedAsync("sessionStorage.removeItem('aura-effect-draft');");
            if (_closed || !_active) return;
#if DEBUG
            StudioWebView.CoreWebView2.Settings.AreDevToolsEnabled = true;
#else
            StudioWebView.CoreWebView2.Settings.AreDevToolsEnabled = false;
#endif
            StudioWebView.CoreWebView2.Settings.IsStatusBarEnabled = false;
            StudioWebView.NavigationCompleted -= NavigationCompleted;
            StudioWebView.NavigationCompleted += NavigationCompleted;
            StudioWebView.CoreWebView2.NavigationStarting -= NavigationStarting;
            StudioWebView.CoreWebView2.NavigationStarting += NavigationStarting;
            StudioWebView.CoreWebView2.WebMessageReceived -= StudioMessageReceived;
            StudioWebView.CoreWebView2.WebMessageReceived += StudioMessageReceived;
            StudioWebView.Source = EmbeddedStudioNavigation.InitialUrl(_openAutomation ? "automation" : "studio", CurrentTheme);
            _openAutomation = false;
            _lastTheme = null;
            _initialized = true;
        }
        catch (Exception ex) { if (!_closed && _active) ShowError("工作室初始化失败：" + ex.Message + "。请确认 Microsoft WebView2 运行时已安装。"); }
        finally { _initializing = false; }
    }
    private void PostAssistant(object message) {
        if (!_closed && StudioWebView.CoreWebView2 != null) StudioWebView.CoreWebView2.PostWebMessageAsJson(JsonSerializer.Serialize(message));
    }
    private async void StudioMessageReceived(CoreWebView2 sender, CoreWebView2WebMessageReceivedEventArgs args)
    {
        if (_closed || !StudioShellModel.TrustedSource(args.Source)) return;
        if (await ReceivePersistenceAsync(args.WebMessageAsJson)) return;
        if (ReceiveAssistantMessage(args.WebMessageAsJson)) return;
        if (!_shell.Receive(args.WebMessageAsJson)) return;
        ProjectStatus.Text = $"{_shell.Name} · {_shell.Lifecycle}";
        ValidationSummary.Text = $"{_shell.Validation} · 构建：{_shell.Build} · 插件：{_shell.Plugin}";
        DiagnosticsText.Text = _shell.Diagnostics;
        AskAiError.Visibility = _shell.Validation != "验证通过" || _shell.Build.Contains("失败") ? Visibility.Visible : Visibility.Collapsed;
        PreviewCommand.Label = _shell.Playing ? "暂停预览" : "预览";
        NewCommand.IsEnabled = OpenCommand.IsEnabled = RecentProjects.IsEnabled = !_shell.Busy;
        var enabled = _shell.WorkType == "effect" && !_shell.Busy;
        SaveCommand.IsEnabled = PreviewCommand.IsEnabled = BuildCommand.IsEnabled = PublishCommand.IsEnabled = enabled;
        _settingProjects = true;
        try {
            var projects = PersistentProjects();
            if (RecentProjects.ItemsSource is not IReadOnlyList<string> current || !current.SequenceEqual(projects)) RecentProjects.ItemsSource = projects;
            if (RecentProjects.SelectedItem as string != _shell.Name) RecentProjects.SelectedItem = _shell.Name;
        }
        finally { _settingProjects = false; }
    }
    private StudioRecentStore? _recentStore;
    private string? _lastRecentProject;
    private IReadOnlyList<string> PersistentProjects() {
        // Do not prune the saved list from an unloaded catalog or Automation page.
        if (_shell.WorkType != "effect" || _shell.Projects.Count == 0) return _shell.Projects;
        try {
            _recentStore ??= new(RuntimeLayoutResolver.DataRoot);
            var opened = _shell.WorkType == "effect" && _shell.Projects.Contains(_shell.Name) && _lastRecentProject != _shell.Name ? _shell.Name : null;
            _lastRecentProject = _shell.Name;
            return _recentStore.Order(_shell.Projects, opened);
        } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or StudioPersistenceException) {
            PostStorage(new { type = "studio_storage_status", name = _shell.Name, text = "最近工程记录不可用；现有工程仍可打开。" });
            return _shell.Projects;
        }
    }
    private void ClearRecent_Click(object sender, RoutedEventArgs args) {
        try {
            (_recentStore ??= new(RuntimeLayoutResolver.DataRoot)).Clear();
            _settingProjects = true;
            try { RecentProjects.ItemsSource = _shell.Projects; RecentProjects.SelectedItem = _shell.Name; }
            finally { _settingProjects = false; }
            PostStorage(new { type = "studio_storage_status", name = _shell.Name, text = "最近工程记录已清除。" });
        } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or StudioPersistenceException) {
            PostStorage(new { type = "studio_storage_status", name = _shell.Name, text = "最近工程记录未能清除。" });
        }
    }
    private void StudioCommand_Click(object sender, RoutedEventArgs args)
    {
        if (sender is AppBarButton { Tag: string command } && !_closed && _initialized)
        {
            if (command == "bench") { AiPanel.Visibility = Visibility.Collapsed; AdaptAssistant(); }
            StudioWebView.CoreWebView2.PostWebMessageAsJson(StudioShellModel.Command(command));
        }
    }
    private void ProjectSelection_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (!_settingProjects && RecentProjects.SelectedItem is string name && _initialized && !_shell.Busy)
            StudioWebView.CoreWebView2.PostWebMessageAsJson(StudioShellModel.Command("select", name));
    }
    private void Diagnostics_Click(object sender, RoutedEventArgs args) =>
        DiagnosticsText.Visibility = DiagnosticsText.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;

    private void NavigationStarting(CoreWebView2 sender, CoreWebView2NavigationStartingEventArgs args)
    {
        if (!Uri.TryCreate(args.Uri, UriKind.Absolute, out var uri) || uri.Scheme != "http" || uri.Host != "127.0.0.1" || uri.Port != 19898)
            args.Cancel = true;
    }
    private void NavigationCompleted(WebView2 sender, CoreWebView2NavigationCompletedEventArgs args)
    {
        if (_closed) return; // A retained page can finish navigation while another native page is visible.
        if (args.IsSuccess)
        {
            LoadingPanel.Visibility = Visibility.Collapsed; StudioWebView.Visibility = Visibility.Visible; AdaptAssistant(); SyncTheme(true);
            if (_openAutomation) OpenAutomation();
        }
        else { _initialized = false; ShowError("工作室加载失败：" + args.WebErrorStatus); }
    }
    private void ShowError(string message)
    {
        LoadingRing.IsActive = false; LoadingStatusText.Text = message; RetryBtn.Visibility = Visibility.Visible;
    }
    public static void CloseHost()
    {
        if (_host is not { } host || host._closed) return;
        host._closed = true; host._retry.Stop(); host._aiCancel?.Cancel(); host._llmHttp.Dispose();
        host.ActualThemeChanged -= host.ThemeChanged;
        host.StudioWebView.NavigationCompleted -= host.NavigationCompleted;
        if (host.StudioWebView.CoreWebView2 != null) {
            host.StudioWebView.CoreWebView2.NavigationStarting -= host.NavigationStarting;
            host.StudioWebView.CoreWebView2.WebMessageReceived -= host.StudioMessageReceived;
        }
        host.StudioWebView.Close(); _host = null;
    }
}
