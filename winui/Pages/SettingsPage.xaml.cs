using System;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Aura_WinUI.Common;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed partial class SettingsPage : Page
{
    private bool _loading = true;
    private CancellationTokenSource? _lifetime;
    public SettingsPage()
    {
        InitializeComponent();
        PageLayout.Attach(this, PageScroll, PageContent, width => {
            foreach (var control in new[] { ThemeControlContainer, TrayControlContainer, DaemonControlContainer, BrowserControlContainer }) {
                bool narrow = width < 720;
                Grid.SetRow(control, narrow ? 1 : 0); Grid.SetColumn(control, narrow ? 0 : 1);
                Grid.SetColumnSpan(control, narrow ? 2 : 1);
                control.HorizontalAlignment = HorizontalAlignment.Left;
                if (control.Parent is Grid row && row.Children[0] is FrameworkElement label) Grid.SetColumnSpan(label, narrow ? 2 : 1);
            }
        });
        Loaded += SettingsPage_Loaded;
        Unloaded += (_, _) => { _lifetime?.Cancel(); DaemonSupervisor.Instance.StatusChanged -= OnStatus; };
    }

    private void SettingsPage_Loaded(object sender, RoutedEventArgs e)
    {
        _loading = true;
        _lifetime?.Cancel(); _lifetime?.Dispose(); _lifetime = new();
        _ = PollAsync(_lifetime.Token);
        DaemonSupervisor.Instance.StatusChanged -= OnStatus;
        DaemonSupervisor.Instance.StatusChanged += OnStatus;
        VersionText.Text = "版本: v" + ClientSettings.Version;
        MinimizeToTrayToggle.IsOn = TrayIconManager.MinimizeToTrayEnabled;
        UpdateDaemonDetail();

        // 绑定主题选择状态
        if (MainWindow.CurrentInstance?.Content is FrameworkElement root)
        {
            ThemeComboBox.SelectedIndex = root.RequestedTheme switch
            {
                ElementTheme.Light => 1,
                ElementTheme.Dark => 2,
                _ => 0
            };
        }
        _loading = false;
    }

    private async Task PollAsync(CancellationToken token)
    {
        try
        {
            while (!token.IsCancellationRequested)
            {
                await DaemonSupervisor.Instance.RefreshAsync(token);
                await DaemonSupervisor.Instance.ProbeWebServerAsync();
                if (token.IsCancellationRequested) return;
                UpdateDaemonDetail();
                await Task.Delay(2000, token);
            }
        }
        catch (OperationCanceledException) { }
    }

    private void ThemeComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ThemeComboBox.SelectedItem is ComboBoxItem item &&
            MainWindow.CurrentInstance?.Content is FrameworkElement root)
        {
            root.RequestedTheme = (string)item.Tag switch
            {
                "Light" => ElementTheme.Light,
                "Dark" => ElementTheme.Dark,
                _ => ElementTheme.Default
            };
            if (!_loading) { ClientSettings.Current.Theme = (string)item.Tag; ClientSettings.Current.Save(); }
        }
    }

    private void MinimizeToTrayToggle_Toggled(object sender, RoutedEventArgs e)
    {
        TrayIconManager.MinimizeToTrayEnabled = MinimizeToTrayToggle.IsOn;
        if (!_loading) { ClientSettings.Current.MinimizeToTray = MinimizeToTrayToggle.IsOn; ClientSettings.Current.Save(); }
    }

    private async void RestartDaemonBtn_Click(object sender, RoutedEventArgs e)
    {
        DaemonStatusDetailText.Text = "正在重新连接...";
        await DaemonSupervisor.Instance.EnsureStartedAsync();
        UpdateDaemonDetail();
    }

    private void OpenBrowserBtn_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            Process.Start(new ProcessStartInfo("http://127.0.0.1:19898/") { UseShellExecute = true });
        }
        catch
        {
            // ignore
        }
    }

    private void UpdateDaemonDetail()
    {
        var supervisor = DaemonSupervisor.Instance;
        DaemonStatusDetailText.Text = supervisor.CoreReady ? "核心已连接" : supervisor.StatusDescription;
        StudioStatusText.Text = supervisor.WebSuppressed ? "Studio：免打扰规则已暂停网页服务" : supervisor.StudioWebReady ? "Studio：可用" : "Studio：暂不可用";
        var id = supervisor.Identity;
        RuntimeDetailsText.Text = id == null ? "尚未取得核心信息" :
            $"核心版本：{id.ProductVersion}\nPID：{id.ProcessId}\n实例：{id.InstanceId}\n连接方式：{supervisor.Ownership}\n配置：{id.ConfigPath}";
        if (supervisor.Ownership == DaemonOwnership.SpawnedByWinUI && supervisor.OwnedRuntimeDirectory != null)
            RuntimeDetailsText.Text += "\nRuntime：" + supervisor.OwnedRuntimeDirectory;
        else if (id != null) RuntimeDetailsText.Text += "\nRuntime：外部核心未提供此路径";
    }
    private void OnStatus(string status) => DispatcherQueue.TryEnqueue(() => { if (IsLoaded && !App.IsShuttingDown) UpdateDaemonDetail(); });
}
