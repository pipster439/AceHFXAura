using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed partial class LightingPage : Page
{
    private readonly IAuraControlClient _client;

    private readonly List<ProfileListItemDto> _profiles = new();
    private ProfileDetailDto? _currentDetail;
    private string _currentRevision = "";
    private ProfileListItemDto? _lastLoadedItem;

    // UI 事件抑制标记 (Constraint 5) - 构造期间默认启用抑制
    private bool _isApplyingModel = true;
    private bool _suppressSelectionChange = true;

    // 本地草稿与脏状态追踪
    private double _draftBrightnessRatio = 1.0;
    private int? _draftPeriodMs;
    private int _draftFps = 25;
    private bool _isDirty = false;

    public LightingPage()
    {
        _isApplyingModel = true;
        _suppressSelectionChange = true;

        InitializeComponent();

        _isApplyingModel = false;
        _suppressSelectionChange = false;

        _client = AuraControlClient.Instance;

        Loaded += LightingPage_Loaded;
    }

    private async void LightingPage_Loaded(object sender, RoutedEventArgs e)
    {
        // 页面 Loaded 时仅拉取一次，杜绝无限 polling 循环
        await RefreshRuntimeStatusAsync();
        await LoadProfileListAsync();
    }

    private async void RefreshStatusBtn_Click(object sender, RoutedEventArgs e)
    {
        await RefreshRuntimeStatusAsync();
    }

    private async Task RefreshRuntimeStatusAsync()
    {
        try
        {
            var status = await _client.GetRuntimeStatusAsync();
            if (ActiveProfileText == null || ActiveFpsText == null || ActiveBackendText == null)
            {
                return;
            }
            if (status.IsOnline && status.Data != null)
            {
                ActiveProfileText.Text = status.ActiveProfileDisplayName;
                ActiveFpsText.Text = status.FpsDisplayName;
                ActiveBackendText.Text = status.BackendDisplayName;
            }
            else
            {
                ActiveProfileText.Text = "守护进程未连接";
                ActiveFpsText.Text = "--";
                ActiveBackendText.Text = "Offline";
            }
        }
        catch (Exception ex)
        {
            if (ActiveProfileText != null) ActiveProfileText.Text = "获取失败";
            if (ActiveFpsText != null) ActiveFpsText.Text = "--";
            if (ActiveBackendText != null) ActiveBackendText.Text = ex.Message;
        }
    }

    private async Task LoadProfileListAsync(string? preferredSelection = null)
    {
        if (ProfileComboBox == null) return;

        try
        {
            var res = await _client.GetProfilesAsync();
            if (!res.IsSuccess)
            {
                ShowNotification(InfoBarSeverity.Error, "加载方案列表失败", res.ErrorMessage);
                return;
            }

            _suppressSelectionChange = true;
            _profiles.Clear();
            _profiles.AddRange(res.Profiles);

            ProfileComboBox.ItemsSource = null;
            ProfileComboBox.ItemsSource = _profiles;
            ProfileComboBox.DisplayMemberPath = "Name";

            ProfileListItemDto? target = null;
            if (!string.IsNullOrEmpty(preferredSelection))
            {
                target = _profiles.Find(p => string.Equals(p.Name, preferredSelection, StringComparison.OrdinalIgnoreCase));
            }
            target ??= _profiles.Count > 0 ? _profiles[0] : null;

            ProfileComboBox.SelectedItem = target;
            _lastLoadedItem = target;
            _suppressSelectionChange = false;

            if (target != null)
            {
                await LoadProfileDetailAsync(target.Name);
            }
        }
        catch (Exception ex)
        {
            ShowNotification(InfoBarSeverity.Error, "初始化异常", ex.Message);
        }
    }

    private async Task LoadProfileDetailAsync(string profileName)
    {
        try
        {
            var res = await _client.GetProfileAsync(profileName);
            if (!res.IsSuccess || res.Profile == null)
            {
                ShowNotification(InfoBarSeverity.Error, "读取方案失败", res.ErrorMessage);
                return;
            }

            _currentDetail = res.Profile;
            _currentRevision = res.Revision;

            // 进入程序化数据绑定阶段，彻底抑制脏检测事件
            _isApplyingModel = true;

            if (EffectTypeText != null)
            {
                EffectTypeText.Text = _currentDetail.Type;
            }

            // 亮度 0.0–1.0 -> 0–100%
            int sliderVal = (int)Math.Round(_currentDetail.Brightness * 100.0);
            if (BrightnessSlider != null)
            {
                BrightnessSlider.Value = sliderVal;
            }
            if (BrightnessValueText != null)
            {
                BrightnessValueText.Text = $"{sliderVal}%";
            }
            _draftBrightnessRatio = _currentDetail.Brightness;

            // 动画周期 (只有 supports_period == true 时显示)
            if (PeriodPanel != null)
            {
                if (_currentDetail.SupportsPeriod)
                {
                    PeriodPanel.Visibility = Visibility.Visible;
                    int periodVal = _currentDetail.PeriodMs ?? 3000;
                    if (PeriodNumberBox != null)
                    {
                        PeriodNumberBox.Value = periodVal;
                    }
                    _draftPeriodMs = periodVal;
                }
                else
                {
                    PeriodPanel.Visibility = Visibility.Collapsed;
                    _draftPeriodMs = null;
                }
            }

            // 方案独占帧率
            if (FpsNumberBox != null)
            {
                FpsNumberBox.Value = _currentDetail.Fps;
            }
            _draftFps = _currentDetail.Fps;

            if (FpsHelperText != null)
            {
                FpsHelperText.Text = _currentDetail.FpsInherited
                    ? $"当前继承全局默认 ({_currentDetail.Fps} FPS)"
                    : $"当前已显式指定独立帧率 ({_currentDetail.Fps} FPS)";
            }

            _isApplyingModel = false;
            SetDirty(false);
        }
        catch (Exception ex)
        {
            _isApplyingModel = false;
            ShowNotification(InfoBarSeverity.Error, "解析方案失败", ex.Message);
        }
    }

    private async void ProfileComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_suppressSelectionChange || _isApplyingModel)
        {
            return;
        }

        var selected = ProfileComboBox?.SelectedItem as ProfileListItemDto;
        if (selected == null || selected == _lastLoadedItem)
        {
            return;
        }

        if (_isDirty)
        {
            // 拦截未保存切换并弹窗提示
            var dialog = new ContentDialog
            {
                Title = "未保存的修改",
                Content = $"方案 [{_lastLoadedItem?.Name}] 存在尚未保存的配置修改，是否放弃修改并切换方案？",
                PrimaryButtonText = "放弃修改",
                CloseButtonText = "取消",
                XamlRoot = this.XamlRoot
            };

            var diagRes = await dialog.ShowAsync();
            if (diagRes == ContentDialogResult.Primary)
            {
                // 用户明确放弃本地草稿
                _lastLoadedItem = selected;
                await LoadProfileDetailAsync(selected.Name);
            }
            else
            {
                // 取消切换：恢复原选择，抑制递归弹窗 (Constraint 5)
                _suppressSelectionChange = true;
                if (ProfileComboBox != null)
                {
                    ProfileComboBox.SelectedItem = _lastLoadedItem;
                }
                _suppressSelectionChange = false;
            }
        }
        else
        {
            _lastLoadedItem = selected;
            await LoadProfileDetailAsync(selected.Name);
        }
    }

    private void BrightnessSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_isApplyingModel) return;

        _draftBrightnessRatio = Math.Clamp(e.NewValue / 100.0, 0.0, 1.0);
        if (BrightnessValueText != null)
        {
            BrightnessValueText.Text = $"{(int)e.NewValue}%";
        }
        EvaluateDirty();
    }

    private void PeriodNumberBox_ValueChanged(NumberBox sender, NumberBoxValueChangedEventArgs args)
    {
        if (_isApplyingModel) return;
        if (double.IsNaN(args.NewValue)) return;

        _draftPeriodMs = (int)Math.Max(33, args.NewValue);
        EvaluateDirty();
    }

    private void FpsNumberBox_ValueChanged(NumberBox sender, NumberBoxValueChangedEventArgs args)
    {
        if (_isApplyingModel) return;
        if (double.IsNaN(args.NewValue)) return;

        _draftFps = (int)Math.Clamp(args.NewValue, 10, 100);
        EvaluateDirty();
    }

    private void EvaluateDirty()
    {
        if (_currentDetail == null)
        {
            SetDirty(false);
            return;
        }

        bool bDiff = Math.Abs(_draftBrightnessRatio - _currentDetail.Brightness) > 0.005;
        bool pDiff = _currentDetail.SupportsPeriod && (_draftPeriodMs != _currentDetail.PeriodMs);
        bool fDiff = _draftFps != _currentDetail.Fps;

        SetDirty(bDiff || pDiff || fDiff);
    }

    private void SetDirty(bool dirty)
    {
        _isDirty = dirty;
        if (SaveBtn != null)
        {
            SaveBtn.IsEnabled = dirty;
        }
        if (DirtyHintText != null)
        {
            DirtyHintText.Visibility = dirty ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    private async void SaveBtn_Click(object sender, RoutedEventArgs e)
    {
        if (!_isDirty || _currentDetail == null)
        {
            return;
        }

        SaveBtn.IsEnabled = false;

        // 构建真正 Sparse Patch (Constraint 2)：仅序列化用户真正修改的字段
        var patch = new ProfilePatchDto
        {
            ExpectedRevision = _currentRevision
        };

        if (Math.Abs(_draftBrightnessRatio - _currentDetail.Brightness) > 0.005)
        {
            patch.Brightness = Math.Round(_draftBrightnessRatio, 4);
        }

        if (_currentDetail.SupportsPeriod && _draftPeriodMs.HasValue && _draftPeriodMs != _currentDetail.PeriodMs)
        {
            patch.PeriodMs = _draftPeriodMs.Value;
        }

        if (_draftFps != _currentDetail.Fps)
        {
            patch.Fps = _draftFps;
        }

        var res = await _client.UpdateProfileAsync(_currentDetail.Name, patch);

        if (res.IsSuccess)
        {
            ShowNotification(InfoBarSeverity.Success, "配置已保存", $"方案 [{_currentDetail.Name}] 参数已更新并通过热重载生效。");
            await LoadProfileDetailAsync(_currentDetail.Name);
            await RefreshRuntimeStatusAsync();
        }
        else if (res.IsConflict)
        {
            // 409 冲突处理：提示外部修改并重新加载最新状态
            ShowNotification(InfoBarSeverity.Warning, "配置已被外部修改", "配置文件已被其他编辑器修改，已自动为您重新载入最新配置，本地草稿已丢弃。");
            await LoadProfileDetailAsync(_currentDetail.Name);
            await RefreshRuntimeStatusAsync();
        }
        else
        {
            ShowNotification(InfoBarSeverity.Error, "保存失败", res.ErrorMessage);
            SaveBtn.IsEnabled = true;
        }
    }

    private void ShowNotification(InfoBarSeverity severity, string title, string message)
    {
        if (StatusInfoBar == null) return;
        StatusInfoBar.Severity = severity;
        StatusInfoBar.Title = title;
        StatusInfoBar.Message = message;
        StatusInfoBar.IsOpen = true;
    }
}
