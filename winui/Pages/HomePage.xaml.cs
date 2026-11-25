using Aura_WinUI.Common;
using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed partial class HomePage : Page
{
    private CancellationTokenSource? _pollCts;
    private long _pollingGeneration = 0;
    private readonly SemaphoreSlim _refreshGate = new(1, 1);
    private readonly ProfileControlClient _profileClient = new();
    private bool _switchingScene;

    public HomePage()
    {
        InitializeComponent();
        PageLayout.Attach(this, PageScroll, RootPanel, width => {
            var context = width >= 1300;
            var wide = width >= 720;
            CardGridCol1.Width = new GridLength(wide ? (context ? 0.42 : 1) : 0, GridUnitType.Star);
            CardGridCol0.Width = new GridLength(1, GridUnitType.Star);
            var cards = new FrameworkElement[] { DeviceCard, DaemonCard, ProfileCard, GsiCard };
            for (int i = 0; i < cards.Length; i++) { Grid.SetRow(cards[i], wide ? i / 2 : i); Grid.SetColumn(cards[i], wide ? i % 2 : 0); }
            if (context)
            {
                Grid.SetRow(DeviceCard, 0); Grid.SetColumn(DeviceCard, 0);
                Grid.SetRow(DaemonCard, 1); Grid.SetColumn(DaemonCard, 0);
                Grid.SetRow(SceneSwitcherCard, 2); Grid.SetColumn(SceneSwitcherCard, 0); Grid.SetColumnSpan(SceneSwitcherCard, 1);
                Grid.SetRow(QuickCard, 3); Grid.SetColumn(QuickCard, 0); Grid.SetColumnSpan(QuickCard, 1);
                Grid.SetRow(ProfileCard, 0); Grid.SetColumn(ProfileCard, 1);
                Grid.SetRow(GsiCard, 1); Grid.SetColumn(GsiCard, 1);
            }
            else
            {
                int sceneRow = wide ? 2 : 4;
                Grid.SetRow(SceneSwitcherCard, sceneRow); Grid.SetColumn(SceneSwitcherCard, 0); Grid.SetColumnSpan(SceneSwitcherCard, wide ? 2 : 1);
                Grid.SetRow(QuickCard, sceneRow + 1); Grid.SetColumn(QuickCard, 0); Grid.SetColumnSpan(QuickCard, wide ? 2 : 1);
            }
        });
        Loaded += HomePage_Loaded;
        Unloaded += HomePage_Unloaded;
    }

    private void HomePage_Loaded(object sender, RoutedEventArgs e)
    {
        StartPolling();
        _ = RefreshScenesAsync();
    }

    private void HomePage_Unloaded(object sender, RoutedEventArgs e)
    {
        StopPolling();
    }

    private void StartPolling()
    {
        StopPolling();
        long currentGen = Interlocked.Increment(ref _pollingGeneration);
        _pollCts = new CancellationTokenSource();
        var token = _pollCts.Token;
        _ = PollLoopAsync(currentGen, token);
    }

    private void StopPolling()
    {
        Interlocked.Increment(ref _pollingGeneration);
        if (_pollCts != null)
        {
            try
            {
                _pollCts.Cancel();
                _pollCts.Dispose();
            }
            catch { }
            finally
            {
                _pollCts = null;
            }
        }
    }

    private async Task PollLoopAsync(long generation, CancellationToken token)
    {
        await RefreshStatusSafeAsync(token);

        while (!token.IsCancellationRequested)
        {
            try { await Task.Delay(1000, token); }
            catch (OperationCanceledException) { break; }

            if (token.IsCancellationRequested || Interlocked.Read(ref _pollingGeneration) != generation)
            {
                break;
            }

            await RefreshStatusSafeAsync(token);
        }
    }

    private async void RefreshStatusBtn_Click(object sender, RoutedEventArgs e)
    {
        var token = _pollCts?.Token ?? CancellationToken.None;
        await RefreshStatusSafeAsync(token);
        await RefreshScenesAsync();
    }

    private async Task RefreshScenesAsync()
    {
        try
        {
            var resp = await _profileClient.ListAsync();
            if (!IsLoaded || App.IsShuttingDown) return;
            RenderScenes(resp);
        }
        catch
        {
            if (IsLoaded && !App.IsShuttingDown)
            {
                SceneButtonsHost.Children.Clear();
                SceneSwitchStatusText.Text = "无法读取磁轴方案列表，请确认核心服务在线";
                SceneSwitchStatusText.Visibility = Visibility.Visible;
            }
        }
    }

    private void RenderScenes(ProfileApiResponse resp)
    {
        SceneButtonsHost.Children.Clear();
        var profiles = resp.Profiles ?? [];
        if (profiles.Count == 0)
        {
            SceneSwitcherHintText.Text = "暂无可用场景方案，可在「磁轴与方案」中新建";
            return;
        }

        Guid currentId = resp.ActiveProfileId ?? resp.SelectedProfileId;
        foreach (var profile in profiles)
        {
            bool isActive = profile.Id == currentId;
            string tag = profile.ActivationBackend == "hardware_slot" && profile.HardwareSlot is int slot
                ? $" · 槽位{slot}"
                : (!string.IsNullOrWhiteSpace(profile.Lighting?.AuraEffectProfile)
                    ? $" · {profile.Lighting.AuraEffectProfile}"
                    : "");
            var btn = new Button
            {
                Content = profile.Name + tag,
                Style = isActive ? (Style)Application.Current.Resources["AccentButtonStyle"] : null,
                IsEnabled = !_switchingScene
            };
            var capturedProfile = profile;
            var revision = resp.DocumentRevision;
            btn.Click += async (_, _) => await ActivateSceneAsync(capturedProfile, revision);
            ToolTipService.SetToolTip(btn, string.IsNullOrWhiteSpace(profile.Lighting?.AuraEffectProfile)
                ? $"切换磁轴方案：{profile.Name}"
                : $"切换磁轴方案「{profile.Name}」并联动灯效「{profile.Lighting.AuraEffectProfile}」");
            SceneButtonsHost.Children.Add(btn);
        }
    }

    private async Task ActivateSceneAsync(DeviceProfile profile, long documentRevision)
    {
        if (_switchingScene) return;
        _switchingScene = true;
        SceneSwitchBusyRing.IsActive = true;
        SceneSwitchBusyRing.Visibility = Visibility.Visible;
        SceneSwitchStatusText.Visibility = Visibility.Visible;
        SceneSwitchStatusText.Text = $"正在切换至「{profile.Name}」…";
        foreach (var child in SceneButtonsHost.Children)
        {
            if (child is Control ctrl) ctrl.IsEnabled = false;
        }

        var sw = Stopwatch.StartNew();
        try
        {
            var resp = await _profileClient.ActivateAsync(profile.Id, ProfileActivationReason.Manual, documentRevision);
            string lightingNote = "";
            if (!string.IsNullOrWhiteSpace(profile.Lighting?.AuraEffectProfile) &&
                profile.EffectiveLightingOwner == "AuraDirectRgb")
            {
                var lRes = await AuraControlClient.Instance.SwitchDefaultLightingProfileAsync(profile.Lighting.AuraEffectProfile);
                lightingNote = lRes.IsSuccess
                    ? $" · 已联动灯效「{profile.Lighting.AuraEffectProfile}」"
                    : $" · 灯效联动失败：{lRes.ErrorMessage}";
            }
            sw.Stop();
            SceneSwitchStatusText.Text = resp.Outcome is "succeeded" or "no-op"
                ? $"已激活「{profile.Name}」({sw.ElapsedMilliseconds} ms){lightingNote}"
                : $"方案已选定（状态：{resp.Outcome ?? "unknown"}）{lightingNote}";
            RenderScenes(resp);
            await RefreshStatusSafeAsync(_pollCts?.Token ?? CancellationToken.None);
        }
        catch (Exception ex)
        {
            SceneSwitchStatusText.Text = $"切换失败：{ex.Message}";
            await RefreshScenesAsync();
        }
        finally
        {
            _switchingScene = false;
            SceneSwitchBusyRing.IsActive = false;
            SceneSwitchBusyRing.Visibility = Visibility.Collapsed;
        }
    }

    private async Task RefreshStatusSafeAsync(CancellationToken token)
    {
        bool acquired = false;
        try
        {
            acquired = await _refreshGate.WaitAsync(0, token).ConfigureAwait(false);
            if (!acquired) return;

            var status = await AuraControlClient.Instance
                .GetRuntimeStatusAsync(token)
                .ConfigureAwait(false);

            if (token.IsCancellationRequested) return;

            DispatcherQueue.TryEnqueue(() =>
            {
                if (!token.IsCancellationRequested && IsLoaded && !App.IsShuttingDown)
                { DaemonSupervisor.Instance.AcceptStatus(status); ApplyStatusToUi(status); }
            });
        }
        catch (OperationCanceledException) { }
        catch { }
        finally
        {
            if (acquired) _refreshGate.Release();
        }
    }

    private void ApplyStatusToUi(RuntimeStatus status)
    {
        DeviceStatusText.Text = status.DeviceStatusDisplayName;
        BackendStatusText.Text = $"后端协议: {status.BackendDisplayName}";

        if (status.IsDryRun)
            DeviceStatusText.Foreground = (Brush)Application.Current.Resources["SystemFillColorCautionBrush"];
        else if (status.Data?.Hardware.Connected == true)
            DeviceStatusText.Foreground = (Brush)Application.Current.Resources["SystemFillColorSuccessBrush"];
        else if (status.DeviceStatusDisplayName == "正在重连")
            DeviceStatusText.Foreground = (Brush)Application.Current.Resources["SystemFillColorAttentionBrush"];
        else
            DeviceStatusText.Foreground = (Brush)Application.Current.Resources["SystemFillColorCriticalBrush"];

        string devDetail = "";
        if (!string.IsNullOrEmpty(status.Data?.Hardware.DevicePath))
            devDetail += $"设备路径: {status.Data.Hardware.DevicePath}\n";
        if (!string.IsNullOrEmpty(status.Data?.Hardware.LastError))
            devDetail += $"最近错误: {status.Data.Hardware.LastError}\n";
        ToolTipService.SetToolTip(DeviceCard, string.IsNullOrEmpty(devDetail) ? "硬件连接正常" : devDetail.TrimEnd());

        DaemonStatusText.Text = status.CoreStatusDisplayName;
        if (status.IsOnline)
        {
            DaemonStatusText.Foreground = (Brush)Application.Current.Resources["SystemFillColorSuccessBrush"];
            string proc = string.IsNullOrEmpty(status.Data?.Runtime.ForegroundProcess) ? "桌面/系统" : status.Data.Runtime.ForegroundProcess;
            DaemonDetailText.Text = $"前台窗口感应: {proc} (API v{status.Data?.ApiVersion ?? 1})";
        }
        else
        {
            DaemonStatusText.Foreground = (Brush)Application.Current.Resources["SystemFillColorCriticalBrush"];
            DaemonDetailText.Text = string.IsNullOrEmpty(status.ErrorMessage) ? "无法连接后台守护进程 (127.0.0.1:19897)" : status.ErrorMessage;
        }

        ActiveProfileText.Text = status.ActiveProfileDisplayName;
        FpsText.Text = $"渲染帧率: {status.FpsDisplayName}";

        GsiStatusText.Text = status.GsiStatusDisplayName;
        GsiStatusText.Foreground = (status.Data?.Gsi.Active == true)
            ? (Brush)Application.Current.Resources["SystemFillColorSuccessBrush"]
            : (Brush)Application.Current.Resources["TextFillColorSecondaryBrush"];
    }

    private void OpenStudioBtn_Click(object sender, RoutedEventArgs e)
    {
        MainWindow.CurrentInstance?.NavigateTo(typeof(StudioPage));
    }

    private void OpenMagneticBtn_Click(object sender, RoutedEventArgs e)
    {
        MainWindow.CurrentInstance?.NavigateTo(typeof(MagneticSwitchPage));
    }

    private void OpenLightingBtn_Click(object sender, RoutedEventArgs e)
    {
        MainWindow.CurrentInstance?.NavigateTo(typeof(LightingPage));
    }

    private void OpenPluginsBtn_Click(object sender, RoutedEventArgs e)
    {
        MainWindow.CurrentInstance?.NavigateTo(typeof(PluginsPage));
    }
}
