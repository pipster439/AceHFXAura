using System;
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

    public HomePage()
    {
        InitializeComponent();
        Loaded += HomePage_Loaded;
        Unloaded += HomePage_Unloaded;
    }

    private void HomePage_Loaded(object sender, RoutedEventArgs e)
    {
        StartPolling();
    }

    private void HomePage_Unloaded(object sender, RoutedEventArgs e)
    {
        StopPolling();
    }

    private void StartPolling()
    {
        // 1. 先取消并清理前一轮轮询 (处理快速切换页面的竞态)
        StopPolling();

        // 2. 递增代数 (Generation) 确保失效旧任务
        long currentGen = Interlocked.Increment(ref _pollingGeneration);
        _pollCts = new CancellationTokenSource();
        var token = _pollCts.Token;

        // 3. 启动受控的轮询循环
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
            catch
            {
                // 忽略释放竞态
            }
            finally
            {
                _pollCts = null;
            }
        }
    }

    private async Task PollLoopAsync(long generation, CancellationToken token)
    {
        // 首次加载立即请求一次
        await RefreshStatusSafeAsync(token);

        while (!token.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(1000, token);
            }
            catch (OperationCanceledException)
            {
                break;
            }

            // 检查 generation 避免快速重入时旧循环继续执行
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
    }

    private async Task RefreshStatusSafeAsync(CancellationToken token)
    {
        // 核心纪律：禁止每次 status poll 调用 DaemonSupervisor.EnsureStartedAsync()
        // 状态读取与生命周期启动职责严格分离

        // 单飞行闸门 (Single-flight gate)：避免 polling 与手动刷新产生并发 HTTP 请求
        if (!await _refreshGate.WaitAsync(0, token).ConfigureAwait(false))
        {
            return;
        }

        try
        {
            var status = await AuraControlClient.Instance.GetRuntimeStatusAsync(token).ConfigureAwait(false);

            if (token.IsCancellationRequested)
            {
                return;
            }

            DispatcherQueue.TryEnqueue(() =>
            {
                ApplyStatusToUi(status);
            });
        }
        catch (OperationCanceledException)
        {
            // 正常取消
        }
        catch
        {
            // 兜底异常防崩
        }
        finally
        {
            _refreshGate.Release();
        }
    }

    private void ApplyStatusToUi(RuntimeStatus status)
    {
        // 1. 设备卡片
        DeviceStatusText.Text = status.DeviceStatusDisplayName;
        BackendStatusText.Text = $"后端协议: {status.BackendDisplayName}";

        if (status.IsDryRun)
        {
            DeviceStatusText.Foreground = (Brush)Application.Current.Resources["SystemFillColorCautionBrush"];
        }
        else if (status.Data?.Hardware.Connected == true)
        {
            DeviceStatusText.Foreground = (Brush)Application.Current.Resources["SystemFillColorSuccessBrush"];
        }
        else if (status.DeviceStatusDisplayName == "Reconnecting")
        {
            DeviceStatusText.Foreground = (Brush)Application.Current.Resources["SystemFillColorAttentionBrush"];
        }
        else
        {
            DeviceStatusText.Foreground = (Brush)Application.Current.Resources["SystemFillColorCriticalBrush"];
        }

        // 设备路径与最近错误放入 Tooltip / 辅助提示
        string devDetail = "";
        if (!string.IsNullOrEmpty(status.Data?.Hardware.DevicePath))
        {
            devDetail += $"设备路径: {status.Data.Hardware.DevicePath}\n";
        }
        if (!string.IsNullOrEmpty(status.Data?.Hardware.LastError))
        {
            devDetail += $"最近错误: {status.Data.Hardware.LastError}\n";
        }
        ToolTipService.SetToolTip(DeviceCard, string.IsNullOrEmpty(devDetail) ? "硬件连接正常" : devDetail.TrimEnd());

        // 2. 核心服务卡片
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

        // 3. 方案与渲染卡片
        ActiveProfileText.Text = status.ActiveProfileDisplayName;
        FpsText.Text = $"渲染帧率: {status.FpsDisplayName}";

        // 4. 游戏联动卡片
        GsiStatusText.Text = status.GsiStatusDisplayName;
        GsiStatusText.Foreground = (status.Data?.Gsi.Active == true)
            ? (Brush)Application.Current.Resources["SystemFillColorSuccessBrush"]
            : (Brush)Application.Current.Resources["TextFillColorSecondaryBrush"];
    }

    private void OpenStudioBtn_Click(object sender, RoutedEventArgs e)
    {
        MainWindow.CurrentInstance?.NavigateTo(typeof(StudioPage));
    }
}
