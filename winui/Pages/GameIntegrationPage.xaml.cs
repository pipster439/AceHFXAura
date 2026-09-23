using Aura_WinUI.Common;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Aura_WinUI.Pages;

public sealed partial class GameIntegrationPage : Page
{
    private readonly AuraControlClient _core = AuraControlClient.Instance;
    private readonly AuraWebClient _web = new();
    private CancellationTokenSource? _lifetime;
    private SimulationState? _simulation;
    private bool _busy, _detecting, _installing;
    private long _generation;

    public GameIntegrationPage()
    {
        InitializeComponent();
        PageLayout.Attach(this, PageScroll, PageContent, width => {
            PageLayout.Columns(StatusCards, width >= 1000 ? 4 : width >= 480 ? 2 : 1);
            PageLayout.Columns(TelemetryGrid, width >= 900 ? 5 : width >= 600 ? 3 : 2);
            PageLayout.Columns(SimulationFields, width >= 720 ? 2 : 1);
            PageLayout.Columns(CfgActions, width >= 720 ? 2 : 1);
            PageLayout.Columns(SimulationActions, width >= 720 ? 3 : 1);
        });
        PageLayout.Notification(this, ResultBar);
        BombBox.ItemsSource = new[] { "carried", "dropped", "planting", "planted", "defusing", "defused", "exploded" };
        PhaseBox.ItemsSource = new[] { "freezetime", "live", "over" };
        BombBox.SelectedIndex = 0; PhaseBox.SelectedIndex = 1;
        Loaded += (_, _) =>
        {
            _lifetime?.Cancel(); _lifetime?.Dispose();
            _lifetime = new(); var generation = ++_generation;
            ResultBar.IsOpen = false;
            _ = PollAsync(generation, _lifetime.Token);
            _ = DetectAsync(_lifetime.Token);
        };
        Unloaded += (_, _) => { ++_generation; _lifetime?.Cancel(); _simulation = null; };
    }
    private CancellationToken Token => _lifetime?.Token ?? new CancellationToken(true);
    private async Task PollAsync(long generation, CancellationToken token)
    {
        try
        {
            while (!token.IsCancellationRequested)
            {
                await DaemonSupervisor.Instance.RefreshAsync(token);
                var data = await _core.GetGsiAsync(token);
                var simulation = await _core.GetSimulationAsync(token);
                if (token.IsCancellationRequested || generation != _generation) return;
                _simulation = simulation.IsSuccess ? simulation.Value : null;
                if (data.IsSuccess && _simulation?.InstanceId == data.Value!.InstanceId && _simulation.Source == data.Value.Source)
                {
                    var value = data.Value;
                    SourceText.Text = value.Source == "simulation" ? "SIMULATION" : "REAL GSI";
                    ConnectionText.Text = value.Connected ? "已连接" : "等待游戏数据";
                    LastUpdateText.Text = value.LastUpdatedSec < 0 ? "尚无数据" : value.LastUpdatedSec.ToString("0.0") + " 秒前";
                    ForegroundStatusText.Text = value.IsCs2Foreground ? "CS2 在前台" : "CS2 不在前台";
                    EmptyStateText.Text = value.LastUpdatedSec < 0 ? WaitingMessage(value.Source) :
                        value.Freshness?.Fresh == false ? "游戏数据已过期；正在等待新的状态更新。" : "以下为游戏数据源最近报告的状态。";
                    FreshnessText.Text = value.Freshness is { } freshness ?
                        $"Automation {(freshness.Fresh ? "Fresh" : "Stale")} · 年龄 {freshness.AgeMs?.ToString() ?? "—"} ms / 阈值 {freshness.ThresholdMs} ms（核心最近评估）" : "等待 Automation 评估";
                    ForegroundText.Text = "前台进程：" + value.ForegroundProcess;
                    HealthText.Text = value.Field("player.state.health"); ArmorText.Text = value.Field("player.state.armor");
                    KillsText.Text = value.Field("player.state.round_kills");
                    BombText.Text = GsiPresentation.Label(value.Field("bomb.state", "round.bomb"));
                    RoundText.Text = GsiPresentation.Label(value.Field("round.phase"));
                }
                else
                {
                    SourceText.Text = data.IsSuccess ? "数据源切换中，等待一致快照" : "核心数据不可用";
                    ConnectionText.Text = "核心数据不可用"; EmptyStateText.Text = data.IsSuccess ? "等待数据源一致快照。" : "无法读取核心数据，请在设置中检查连接。";
                    FreshnessText.Text = data.Error; ForegroundText.Text = "";
                    LastUpdateText.Text = ForegroundStatusText.Text = HealthText.Text = ArmorText.Text = KillsText.Text = BombText.Text = RoundText.Text = "—";
                }
                UpdateButtons();
                await Task.Delay(1000, token);
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { if (generation == _generation) Show(ex.Message, true); }
    }
    private string WaitingMessage(string source)
    {
        if (source == "simulation") return "SIMULATION 已启用；在高级调试中发送模拟数据。";
        return (CfgPathsBox.SelectedItem as GsiCfgPath)?.TemplateMatch switch
        {
            "matching" => "连接配置已安装，等待 CS2 数据。请启动或重启游戏并进入对局。",
            "missing" => "等待 CS2 数据。请先在下方安装连接配置，再启动游戏。",
            "different" => "等待 CS2 数据。已有连接配置与当前模板不同，可在下方更新。",
            _ => "等待 CS2 数据。请在下方检测 CS2 安装位置和连接配置。"
        };
    }
    private void UpdateButtons()
    {
        SimulationBanner.IsOpen = _simulation?.Enabled == true;
        QueueText.Text = _simulation == null ? "模拟状态不可用" : $"已应用序号：{_simulation.AppliedSequence} · 排队：{_simulation.Pending} · 心跳：{_simulation.HeartbeatMs} ms";
        SourceButton.IsEnabled = !_busy && _simulation != null && DaemonSupervisor.Instance.CoreReady;
        SourceButton.Content = _simulation?.Enabled == true ? "返回 REAL GSI" : "启用 SIMULATION";
        SimulationControls.IsEnabled = SourceButton.IsEnabled && _simulation?.Enabled == true;
        HeartbeatButton.Content = _simulation?.Heartbeat == true ? "暂停自动心跳" : "恢复自动心跳";
    }
    private async Task DetectAsync(CancellationToken token)
    {
        if (_detecting) return;
        _detecting = true; DetectButton.IsEnabled = false; InstallButton.IsEnabled = false;
        try
        {
            var result = await _web.GetCfgAsync(token);
            if (token.IsCancellationRequested) return;
            var previous = (CfgPathsBox.SelectedItem as GsiCfgPath)?.Path;
            CfgPathsBox.ItemsSource = result.Value?.Paths;
            CfgPathsBox.Visibility = result.Value?.Paths.Count > 1 ? Visibility.Visible : Visibility.Collapsed;
            CfgPathsBox.SelectedItem = result.Value?.Paths.FirstOrDefault(x => x.Path == previous) ?? result.Value?.Paths.FirstOrDefault();
            UpdateCfgDetails();
            CfgStatusText.Text = !result.IsSuccess ? "配置管理服务不可用；不影响核心 GSI/simulation。 " + result.Error :
                result.Value!.Paths.Count == 0 ? "未检测到 CS2 cfg 目录。请检查 Steam/CS2 安装后重新检测。" : Describe(CfgPathsBox.SelectedItem as GsiCfgPath);
        }
        catch (OperationCanceledException) { }
        finally { _detecting = false; if (IsLoaded) { DetectButton.IsEnabled = true; UpdateInstallButton(); } }
    }
    private static string Describe(GsiCfgPath? path) => path?.TemplateMatch switch
    {
        "matching" => "已安装 CS2 连接配置。启动游戏后即可查看实时数据。",
        "different" => "已找到连接配置，可更新为当前版本。更新会替换现有文件。",
        "missing" => "已检测到 CS2，尚未安装连接配置。",
        _ => "无法读取配置文件，请检查权限后重新检测。"
    };
    private void UpdateCfgDetails()
    {
        var path = CfgPathsBox.SelectedItem as GsiCfgPath;
        CfgPathText.Text = path?.Path ?? "未检测到安装位置";
        InstallButton.Content = path?.TemplateMatch switch { "matching" => "重新安装…", "different" => "更新连接配置…", _ => "安装连接配置…" };
    }
    private void UpdateInstallButton() => InstallButton.IsEnabled = !_installing && !_detecting && CfgPathsBox.SelectedItem is GsiCfgPath { Revision.Length: > 0 };
    private void CfgPaths_SelectionChanged(object sender, SelectionChangedEventArgs e) { CfgStatusText.Text = Describe(CfgPathsBox.SelectedItem as GsiCfgPath); UpdateCfgDetails(); UpdateInstallButton(); }
    private async void Detect_Click(object sender, RoutedEventArgs e) => await DetectAsync(Token);
    private async void Install_Click(object sender, RoutedEventArgs e)
    {
        if (_installing || CfgPathsBox.SelectedItem is not GsiCfgPath path) return;
        var token = Token;
        _installing = true; UpdateInstallButton();
        try
        {
            var dialog = new ContentDialog { XamlRoot = XamlRoot, Title = "安装 CS2 GSI 配置？",
                Content = path.Path + "\n\n将写入 gamestate_integration_aura.cfg。已有文件会被当前模板替换。",
                PrimaryButtonText = "确认安装", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Close };
            if (await dialog.ShowAsync() != ContentDialogResult.Primary || token.IsCancellationRequested) return;
            var result = await _web.InstallCfgAsync(path, token);
            if (token.IsCancellationRequested) return;
            Show(result.IsSuccess ? "配置已安装：" + result.Value!.Path + "。启动或重启 CS2 后检查实时数据。" : result.Error, !result.IsSuccess);
            await DetectAsync(token);
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { if (!token.IsCancellationRequested) Show(ex.Message, true); }
        finally { _installing = false; if (IsLoaded) UpdateInstallButton(); }
    }
    private async Task SendAsync(SimulationPatch patch)
    {
        if (_busy || !DaemonSupervisor.Instance.CoreReady) return;
        var token = Token; _busy = true; UpdateButtons();
        try
        {
            var result = await _core.UpdateSimulationAsync(patch, token);
            if (token.IsCancellationRequested) return;
            if (result.IsSuccess) _simulation = result.Value;
            Show(result.IsSuccess ? "核心已确认应用；当前状态以实时数据为准。" : result.Error, !result.IsSuccess);
        }
        catch (OperationCanceledException) { }
        finally { _busy = false; if (IsLoaded) UpdateButtons(); }
    }
    private async void ReturnReal_Click(object sender, RoutedEventArgs e) => await SendAsync(new() { Enabled = false });
    private async void Source_Click(object sender, RoutedEventArgs e) => await SendAsync(new() { Enabled = _simulation?.Enabled != true });
    private async void Kill_Click(object sender, RoutedEventArgs e) => await SendAsync(new() { IncrementKill = true });
    private async void Heartbeat_Click(object sender, RoutedEventArgs e) => await SendAsync(new() { Heartbeat = _simulation?.Heartbeat != true });
    private async void ApplySimulation_Click(object sender, RoutedEventArgs e)
    {
        if (new[] { HealthBox.Value, ArmorBox.Value, KillsBox.Value }.Any(x => !double.IsFinite(x) || x != Math.Truncate(x)))
        { Show("请输入有效整数。", true); return; }
        await SendAsync(new() { Health = (int)HealthBox.Value, Armor = (int)ArmorBox.Value, RoundKills = (int)KillsBox.Value,
            ForegroundProcess = ProcessBox.Text, Bomb = BombBox.SelectedItem as string, RoundPhase = PhaseBox.SelectedItem as string });
    }
    private void OpenStudio_Click(object sender, RoutedEventArgs e) => MainWindow.CurrentInstance?.NavigateTo(typeof(StudioPage));
    private void Show(string message, bool error)
    { if (!IsLoaded) return; ResultBar.IsOpen = false; ResultBar.Message = message; ResultBar.Severity = error ? InfoBarSeverity.Warning : InfoBarSeverity.Success; ResultBar.IsOpen = true; }
}
