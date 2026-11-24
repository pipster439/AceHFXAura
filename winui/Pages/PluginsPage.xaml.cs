using Aura_WinUI.Common;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using System.Diagnostics;
using Windows.ApplicationModel.DataTransfer;

namespace Aura_WinUI.Pages;

public sealed partial class PluginsPage : Page
{
    private readonly PluginOverviewClient _pluginClient = new();
    private readonly MagneticHallClient _hallClient = new();
    private PluginOverviewResponse? _lastOverview;

    public PluginsPage()
    {
        InitializeComponent();
        NavigationCacheMode = Microsoft.UI.Xaml.Navigation.NavigationCacheMode.Required;
        PageLayout.Attach(this, PageScroll, PageContent, maxWidth: 1200);
        PageLayout.Notification(this, PluginsNoticeBar);
        Loaded += async (_, _) => await RefreshAsync();
    }

    private async Task RefreshAsync()
    {
        RescanPluginsBtn.IsEnabled = false;
        try
        {
            _lastOverview = await _pluginClient.GetOverviewAsync();
            RenderOverview();
        }
        finally
        {
            RescanPluginsBtn.IsEnabled = true;
        }
    }

    private void RenderOverview()
    {
        var overview = _lastOverview;
        if (overview == null || overview.Status != "ok")
        {
            WootingBuiltText.Text = "服务未连接";
            WootingInstalledText.Text = "—";
            WootingShmText.Text = "—";
            NativePluginCountText.Text = "无法连接核心服务";
            return;
        }

        var bridge = overview.Bridges.FirstOrDefault(b => b.Id == "wooting-analog-m605")
            ?? overview.Bridges.FirstOrDefault(b => b.Id != "aura-mcp-server");
        if (bridge != null)
        {
            WootingTitleText.Text = bridge.Name;
            WootingDescText.Text = bridge.Description;
            WootingBuiltText.Text = bridge.Built ? "编译产物：已就绪" : "编译产物：未编译";
            WootingInstalledText.Text = bridge.Installed ? "系统安装：已安装" : "系统安装：未安装";
            WootingShmText.Text = bridge.HallActive
                ? $"共享内存：运行中 ({bridge.HallRateHz:F0} Hz)"
                : "共享内存：未开启";
            WootingHallToggle.IsChecked = bridge.HallActive;
            WootingHallToggle.Content = bridge.HallActive ? "停止共享内存广播" : "开启共享内存广播";
            InstallWootingBtn.IsEnabled = bridge.Built && File.Exists(bridge.InstallScriptPath);
        }

        var mcpBridge = overview.Bridges.FirstOrDefault(b => b.Id == "aura-mcp-server");
        if (mcpBridge != null)
        {
            McpTitleText.Text = mcpBridge.Name;
            McpDescText.Text = mcpBridge.Description;
            McpReadyText.Text = mcpBridge.Built ? "服务脚本：已就绪" : "服务脚本：缺失";
            McpTransportText.Text = $"传输协议：{mcpBridge.SharedMemoryName}";
            CopyMcpConfigBtn.IsEnabled = mcpBridge.Built;
        }

        NativePluginsList.Children.Clear();
        NativePluginCountText.Text = $"{overview.NativePlugins.Count} 个已加载";

        if (overview.NativePlugins.Count == 0)
        {
            NativePluginsList.Children.Add(new Border
            {
                Style = (Style)Resources["PluginCardStyle"],
                Child = new TextBlock
                {
                    Text = "plugins 目录暂无已加载插件。可将编译好的 effect_*.dll 放入 plugins/ 后点击右上角“重新扫描”。",
                    Style = (Style)Application.Current.Resources["CaptionTextBlockStyle"],
                    Foreground = (Brush)Application.Current.Resources["TextFillColorSecondaryBrush"],
                    TextWrapping = TextWrapping.Wrap
                }
            });
            return;
        }

        foreach (var item in overview.NativePlugins)
        {
            var caps = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
            void AddPill(string text, bool accent = false)
            {
                caps.Children.Add(new Border
                {
                    Background = (Brush)Application.Current.Resources[
                        accent ? "AccentFillColorSecondaryBrush" : "SubtleFillColorSecondaryBrush"],
                    CornerRadius = new CornerRadius(4),
                    Padding = new Thickness(7, 2, 7, 2),
                    Child = new TextBlock
                    {
                        Text = text,
                        Style = (Style)Application.Current.Resources["CaptionTextBlockStyle"],
                        Foreground = (Brush)Application.Current.Resources[
                            accent ? "TextOnAccentFillColorPrimaryBrush" : "TextFillColorSecondaryBrush"]
                    }
                });
            }

            AddPill(item.Kind == "program" ? "Studio 积木" : "原生 DLL");
            if (item.HasHallAnalog) AddPill("霍尔深度", accent: true);
            if (item.HasLogicalKeyboard) AddPill("逻辑按键");
            if (item.HasLifecycle) AddPill("生命周期");

            var row = new Grid { ColumnSpacing = 12 };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var left = new StackPanel { Spacing = 2 };
            left.Children.Add(new TextBlock
            {
                Text = item.Name,
                Style = (Style)Application.Current.Resources["BodyStrongTextBlockStyle"]
            });
            left.Children.Add(new TextBlock
            {
                Text = item.Path,
                Style = (Style)Application.Current.Resources["CaptionTextBlockStyle"],
                Foreground = (Brush)Application.Current.Resources["TextFillColorTertiaryBrush"],
                TextWrapping = TextWrapping.Wrap
            });
            row.Children.Add(left);

            Grid.SetColumn(caps, 1);
            caps.VerticalAlignment = VerticalAlignment.Center;
            row.Children.Add(caps);

            NativePluginsList.Children.Add(new Border
            {
                Style = (Style)Resources["PluginCardStyle"],
                Child = row
            });
        }
    }

    private async void RescanPluginsBtn_Click(object sender, RoutedEventArgs e)
    {
        RescanPluginsBtn.IsEnabled = false;
        try
        {
            _lastOverview = await _pluginClient.RescanAsync();
            RenderOverview();
            PluginsNoticeBar.Severity = InfoBarSeverity.Success;
            PluginsNoticeBar.Title = "插件目录已刷新";
            PluginsNoticeBar.Message = $"当前共加载 {_lastOverview.NativePlugins.Count} 个原生插件。";
            PluginsNoticeBar.IsOpen = true;
        }
        finally
        {
            RescanPluginsBtn.IsEnabled = true;
        }
    }

    private async void WootingHallToggle_Click(object sender, RoutedEventArgs e)
    {
        bool wantActive = WootingHallToggle.IsChecked == true;
        WootingHallToggle.IsEnabled = false;
        try
        {
            var snap = wantActive
                ? await _hallClient.StartAsync(WootingUnlockFaCheckBox.IsChecked == true)
                : await _hallClient.StopAsync();
            if (!wantActive || snap.Active)
            {
                PluginsNoticeBar.Severity = InfoBarSeverity.Success;
                PluginsNoticeBar.Title = snap.Active ? "共享内存已开启" : "共享内存已停止";
                PluginsNoticeBar.Message = snap.Active
                    ? "Local\\AuraM605HallAnalogV1 正在广播 68 键模拟深度。"
                    : "已安全发送 04 04 01 停止霍尔流。";
            }
            else
            {
                PluginsNoticeBar.Severity = InfoBarSeverity.Error;
                PluginsNoticeBar.Title = "启动失败";
                PluginsNoticeBar.Message = snap.LastError;
            }
            PluginsNoticeBar.IsOpen = true;
            await RefreshAsync();
        }
        finally
        {
            WootingHallToggle.IsEnabled = true;
        }
    }

    private async void InstallWootingBtn_Click(object sender, RoutedEventArgs e)
    {
        var bridge = _lastOverview?.Bridges.FirstOrDefault(b => b.Id == "wooting-analog-m605");
        var script = bridge?.InstallScriptPath;
        if (string.IsNullOrWhiteSpace(script) || !File.Exists(script)) return;

        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = "powershell.exe",
                Arguments = $"-NoProfile -ExecutionPolicy Bypass -File \"{script}\"",
                UseShellExecute = true,
                Verb = "runas"
            };
            using var proc = Process.Start(psi);
            if (proc != null)
            {
                await proc.WaitForExitAsync();
            }
            await RefreshAsync();
            bool ok = _lastOverview?.Bridges.FirstOrDefault(b => b.Id == "wooting-analog-m605")?.Installed == true;
            PluginsNoticeBar.Severity = ok ? InfoBarSeverity.Success : InfoBarSeverity.Warning;
            PluginsNoticeBar.Title = ok ? "Wooting 插件已安装" : "安装未完成";
            PluginsNoticeBar.Message = ok
                ? "已复制到 C:\\Program Files\\WootingAnalogPlugins\\aura-m605-plugin\\。"
                : "未能确认系统目录安装结果，请检查是否已授权管理员权限。";
            PluginsNoticeBar.IsOpen = true;
        }
        catch (Exception ex)
        {
            PluginsNoticeBar.Severity = InfoBarSeverity.Warning;
            PluginsNoticeBar.Title = "需要管理员权限";
            PluginsNoticeBar.Message = ex.Message;
            PluginsNoticeBar.IsOpen = true;
        }
    }

    private void OpenPluginsFolderBtn_Click(object sender, RoutedEventArgs e)
    {
        OpenFolder(_lastOverview?.PluginsDir);
    }

    private void OpenWootingSourceBtn_Click(object sender, RoutedEventArgs e)
    {
        OpenFolder(_lastOverview?.Bridges.FirstOrDefault(b => b.Id == "wooting-analog-m605")?.SourceDir);
    }

    private void CopyMcpConfigBtn_Click(object sender, RoutedEventArgs e)
    {
        var mcpBridge = _lastOverview?.Bridges.FirstOrDefault(b => b.Id == "aura-mcp-server");
        var scriptPath = AuraMcpConfig.ResolveScriptPath(mcpBridge?.DllPath);
        var json = AuraMcpConfig.BuildClientConfigJson(scriptPath);
        var data = new DataPackage();
        data.SetText(json);
        Clipboard.SetContent(data);

        PluginsNoticeBar.Severity = InfoBarSeverity.Success;
        PluginsNoticeBar.Title = "已复制 MCP 客户端配置";
        PluginsNoticeBar.Message = $"已将 {scriptPath} 的 mcpServers JSON 配置复制到剪贴板，可直接粘贴到外部 AI 客户端。";
        PluginsNoticeBar.IsOpen = true;
    }

    private void OpenMcpFolderBtn_Click(object sender, RoutedEventArgs e)
    {
        var dir = _lastOverview?.Bridges.FirstOrDefault(b => b.Id == "aura-mcp-server")?.SourceDir;
        if (string.IsNullOrWhiteSpace(dir))
        {
            dir = Path.GetDirectoryName(AuraMcpConfig.ResolveScriptPath());
        }
        OpenFolder(dir);
    }

    private static void OpenFolder(string? path)
    {
        if (!string.IsNullOrWhiteSpace(path) && Directory.Exists(path))
        {
            Process.Start(new ProcessStartInfo { FileName = "explorer.exe", Arguments = $"\"{path}\"", UseShellExecute = true });
        }
    }
}
