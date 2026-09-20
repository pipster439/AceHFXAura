using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed partial class LightingPage : Page
{
    private readonly IAuraControlClient _client;

    // 预设列表与服务端干净状态
    private readonly List<LightingPresetItemDto> _presets = new();
    private BaseLightingDto? _cleanLighting;
    private string _currentRevision = "";

    // UI 事件抑制标记 (生命周期防御) - 构造期与异步数据灌入期间置 true
    private bool _isApplyingModel = true;

    // 本地草稿与脏状态追踪
    private LightingPresetItemDto? _draftPreset;
    private double _draftBrightnessRatio = 1.0;
    private int? _draftPeriodMs;
    private readonly Dictionary<string, object> _draftParameters = new();
    private bool _isDirty = false;

    public LightingPage()
    {
        _isApplyingModel = true;

        InitializeComponent();

        _isApplyingModel = false;

        _client = AuraControlClient.Instance;

        Loaded += LightingPage_Loaded;
    }

    private async void LightingPage_Loaded(object sender, RoutedEventArgs e)
    {
        await LoadLightingPageDataAsync();
        await RefreshRuntimeStatusAsync();
    }

    private async void RefreshStatusBtn_Click(object sender, RoutedEventArgs e)
    {
        // 约束 4: 刷新状态按钮只刷新运行时状态，严禁覆盖或丢弃未保存的本地草稿
        await RefreshRuntimeStatusAsync();
    }

    private void OpenStudioBtn_Click(object sender, RoutedEventArgs e)
    {
        // 约束 3: 提供直接跳转至 Studio 页面的入口
        if (MainWindow.CurrentNavView != null)
        {
            var studioItem = MainWindow.CurrentNavView.MenuItems
                .OfType<NavigationViewItem>()
                .FirstOrDefault(i => string.Equals((string?)i.Tag, "studio", StringComparison.OrdinalIgnoreCase));
            if (studioItem != null)
            {
                MainWindow.CurrentNavView.SelectedItem = studioItem;
            }
        }
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
                string activeProf = status.Data.Runtime.ActiveProfile;
                string baseProf = _cleanLighting?.ProfileName ?? "";

                ActiveFpsText.Text = status.FpsDisplayName;
                ActiveBackendText.Text = status.BackendDisplayName;

                // 约束 2: 中性事实文案，严禁无依据声称“一定由自动化规则控制”
                if (!string.IsNullOrEmpty(activeProf) && !string.IsNullOrEmpty(baseProf) &&
                    !string.Equals(activeProf, baseProf, StringComparison.OrdinalIgnoreCase))
                {
                    ActiveProfileText.Text = activeProf;
                    if (StatusMismatchInfoBar != null)
                    {
                        StatusMismatchInfoBar.IsOpen = true;
                        StatusMismatchInfoBar.Title = "当前运行方案与默认灯效不同";
                        StatusMismatchInfoBar.Message = $"当前运行方案为 [{activeProf}]，可能由应用规则、游戏联动或编排配置选择。您在此页面修改的是默认灯效（{baseProf}）；当无特定规则覆盖时生效。";
                    }
                }
                else
                {
                    ActiveProfileText.Text = string.IsNullOrEmpty(activeProf) ? "--" : $"{activeProf} (默认)";
                    if (StatusMismatchInfoBar != null)
                    {
                        StatusMismatchInfoBar.IsOpen = false;
                    }
                }
            }
            else
            {
                ActiveProfileText.Text = "守护进程未连接";
                ActiveFpsText.Text = "--";
                ActiveBackendText.Text = "Offline";
                if (StatusMismatchInfoBar != null)
                {
                    StatusMismatchInfoBar.IsOpen = false;
                }
            }
        }
        catch (Exception ex)
        {
            if (ActiveProfileText != null) ActiveProfileText.Text = "获取失败";
            if (ActiveFpsText != null) ActiveFpsText.Text = "--";
            if (ActiveBackendText != null) ActiveBackendText.Text = ex.Message;
        }
    }

    private async Task LoadLightingPageDataAsync()
    {
        try
        {
            _isApplyingModel = true;

            // 1. 加载预设 Catalog (单一事实来源)
            var pRes = await _client.GetLightingPresetsAsync();
            if (!pRes.IsSuccess)
            {
                _isApplyingModel = false;
                ShowNotification(InfoBarSeverity.Error, "加载预设目录失败", pRes.ErrorMessage);
                return;
            }

            _presets.Clear();
            _presets.AddRange(pRes.Presets);

            if (PresetsGridView != null)
            {
                PresetsGridView.ItemsSource = null;
                PresetsGridView.ItemsSource = _presets;
            }

            // 2. 加载 Base Lighting
            var bRes = await _client.GetBaseLightingAsync();
            if (!bRes.IsSuccess || bRes.Lighting == null)
            {
                _isApplyingModel = false;
                ShowNotification(InfoBarSeverity.Error, "读取默认灯效失败", bRes.ErrorMessage);
                return;
            }

            _cleanLighting = bRes.Lighting;
            _currentRevision = bRes.Revision;

            // 3. 将 cleanLighting 回填至 UI 与本地草稿
            ApplyCleanLightingToUi();

            _isApplyingModel = false;
            SetDirty(false);
        }
        catch (Exception ex)
        {
            _isApplyingModel = false;
            ShowNotification(InfoBarSeverity.Error, "初始化异常", ex.Message);
        }
    }

    private void ApplyCleanLightingToUi()
    {
        if (_cleanLighting == null) return;

        // 亮度 0.0–1.0 -> 0–100%
        int sliderVal = (int)Math.Round(_cleanLighting.Brightness * 100.0);
        if (BrightnessSlider != null)
        {
            BrightnessSlider.Value = sliderVal;
        }
        if (BrightnessValueText != null)
        {
            BrightnessValueText.Text = $"{sliderVal}%";
        }
        _draftBrightnessRatio = _cleanLighting.Brightness;

        // 约束 3: 支持高级方案 (custom_keymap / plugin)
        if (!_cleanLighting.IsBuiltinPreset)
        {
            _draftPreset = null;
            if (PresetsGridView != null)
            {
                PresetsGridView.SelectedItem = null;
            }
            if (DefaultEffectText != null)
            {
                DefaultEffectText.Text = $"{_cleanLighting.Effect} (高级方案)";
            }
            if (SelectedPresetText != null)
            {
                SelectedPresetText.Text = $"{_cleanLighting.Effect} (由 Studio 管理的高级效果)";
            }
            if (AdvancedEffectBanner != null)
            {
                AdvancedEffectBanner.IsOpen = true;
                AdvancedEffectBanner.Message = $"当前默认方案 [{_cleanLighting.Effect}] 包含逐键自定义或插件逻辑，由 Studio 管理。您可在此选择下方任一内建预设替换默认灯效，或打开 Studio 进行逐键编排。";
            }
            if (PeriodPanel != null)
            {
                PeriodPanel.Visibility = Visibility.Collapsed;
            }
            _draftPeriodMs = null;
            _draftParameters.Clear();
            RebuildDynamicParametersUi();
            return;
        }

        // 内置预设正常匹配
        if (AdvancedEffectBanner != null)
        {
            AdvancedEffectBanner.IsOpen = false;
        }

        _draftPreset = _presets.Find(p =>
            string.Equals(p.Id, _cleanLighting.PresetId, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(p.Effect, _cleanLighting.Effect, StringComparison.OrdinalIgnoreCase));

        if (PresetsGridView != null)
        {
            PresetsGridView.SelectedItem = _draftPreset;
        }

        string displayName = _draftPreset?.DisplayName ?? _cleanLighting.Effect;
        if (DefaultEffectText != null)
        {
            DefaultEffectText.Text = displayName;
        }
        if (SelectedPresetText != null)
        {
            SelectedPresetText.Text = _draftPreset != null
                ? $"{_draftPreset.DisplayName} ({_draftPreset.Effect})"
                : displayName;
        }

        if (PeriodPanel != null)
        {
            if (_cleanLighting.SupportsPeriod)
            {
                PeriodPanel.Visibility = Visibility.Visible;
                int periodVal = _cleanLighting.PeriodMs ?? _draftPreset?.DefaultPeriodMs ?? 3000;
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

        // 灌入初始参数草稿 (严格以 _cleanLighting.Parameters 为准)
        _draftParameters.Clear();
        if (_draftPreset != null && _draftPreset.ParameterSchema != null)
        {
            foreach (var schema in _draftPreset.ParameterSchema)
            {
                object? rawVal = null;
                if (_cleanLighting.Parameters != null && _cleanLighting.Parameters.TryGetValue(schema.Key, out var je))
                {
                    rawVal = je;
                }
                else
                {
                    rawVal = schema.DefaultValue;
                }

                switch (schema.Type)
                {
                    case EffectParamType.Color:
                        var rgb = ExtractRgb(rawVal) ?? (255, 255, 255);
                        _draftParameters[schema.Key] = new int[] { rgb.r, rgb.g, rgb.b };
                        break;
                    case EffectParamType.Boolean:
                        _draftParameters[schema.Key] = ExtractBool(rawVal) ?? false;
                        break;
                    case EffectParamType.Enum:
                        _draftParameters[schema.Key] = ExtractString(rawVal) ?? "";
                        break;
                    case EffectParamType.Number:
                        _draftParameters[schema.Key] = ExtractDouble(rawVal) ?? 1.0;
                        break;
                }
            }
        }

        RebuildDynamicParametersUi();
    }

    private void PresetsGridView_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        // 约束 6: 抑制程序化或构造期触发
        if (_isApplyingModel) return;

        var selected = PresetsGridView?.SelectedItem as LightingPresetItemDto;
        if (selected == null) return;

        _draftPreset = selected;

        if (SelectedPresetText != null)
        {
            SelectedPresetText.Text = $"{selected.DisplayName} ({selected.Effect})";
        }

        // 约束 5: 切换预设时动画周期联动
        if (selected.SupportsPeriod)
        {
            if (PeriodPanel != null)
            {
                PeriodPanel.Visibility = Visibility.Visible;
            }
            int defPeriod = selected.DefaultPeriodMs ?? 2500;
            _isApplyingModel = true;
            if (PeriodNumberBox != null)
            {
                PeriodNumberBox.Value = defPeriod;
            }
            _isApplyingModel = false;
            _draftPeriodMs = defPeriod;
        }
        else
        {
            if (PeriodPanel != null)
            {
                PeriodPanel.Visibility = Visibility.Collapsed;
            }
            _draftPeriodMs = null;
        }

        // 切换新预设时，参数草稿采用目标预设的 Canonical Defaults
        _draftParameters.Clear();
        if (selected.ParameterSchema != null)
        {
            foreach (var schema in selected.ParameterSchema)
            {
                switch (schema.Type)
                {
                    case EffectParamType.Color:
                        var rgb = ExtractRgb(schema.DefaultValue) ?? (255, 255, 255);
                        _draftParameters[schema.Key] = new int[] { rgb.r, rgb.g, rgb.b };
                        break;
                    case EffectParamType.Boolean:
                        _draftParameters[schema.Key] = ExtractBool(schema.DefaultValue) ?? false;
                        break;
                    case EffectParamType.Enum:
                        _draftParameters[schema.Key] = ExtractString(schema.DefaultValue) ?? "";
                        break;
                    case EffectParamType.Number:
                        _draftParameters[schema.Key] = ExtractDouble(schema.DefaultValue) ?? 1.0;
                        break;
                }
            }
        }

        _isApplyingModel = true;
        RebuildDynamicParametersUi();
        _isApplyingModel = false;

        EvaluateDirty();
    }

    private void RebuildDynamicParametersUi()
    {
        if (DynamicParametersPanel == null) return;

        DynamicParametersPanel.Children.Clear();

        if (_draftPreset == null || _draftPreset.ParameterSchema == null || _draftPreset.ParameterSchema.Count == 0)
        {
            DynamicParametersPanel.Visibility = Visibility.Collapsed;
            return;
        }

        DynamicParametersPanel.Visibility = Visibility.Visible;

        foreach (var schema in _draftPreset.ParameterSchema)
        {
            switch (schema.Type)
            {
                case EffectParamType.Color:
                    BuildColorControl(schema);
                    break;
                case EffectParamType.Boolean:
                    BuildBooleanControl(schema);
                    break;
                case EffectParamType.Enum:
                    BuildEnumControl(schema);
                    break;
                case EffectParamType.Number:
                    BuildNumberControl(schema);
                    break;
            }
        }
    }

    private void BuildColorControl(EffectParamSchemaDto schema)
    {
        var container = new StackPanel
        {
            Spacing = 6,
            MaxWidth = 540,
            HorizontalAlignment = HorizontalAlignment.Left
        };

        var header = new TextBlock
        {
            Text = schema.DisplayName,
            Style = (Style)Application.Current.Resources["BodyStrongTextBlockStyle"]
        };
        container.Children.Add(header);

        var swatchBorder = new Border
        {
            Width = 24,
            Height = 24,
            CornerRadius = new CornerRadius(4),
            BorderThickness = new Thickness(1),
            BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"]
        };

        var colorText = new TextBlock
        {
            VerticalAlignment = VerticalAlignment.Center,
            Style = (Style)Application.Current.Resources["BodyTextBlockStyle"]
        };

        var btnStack = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 10,
            VerticalAlignment = VerticalAlignment.Center
        };
        btnStack.Children.Add(swatchBorder);
        btnStack.Children.Add(colorText);

        var dropDownBtn = new DropDownButton
        {
            Content = btnStack,
            MinWidth = 240
        };

        var picker = new ColorPicker
        {
            IsAlphaEnabled = false,
            IsHexInputVisible = true
        };

        var rgb = ExtractRgb(_draftParameters.GetValueOrDefault(schema.Key)) ?? (255, 255, 255);
        var winColor = Windows.UI.Color.FromArgb(255, (byte)rgb.r, (byte)rgb.g, (byte)rgb.b);
        swatchBorder.Background = new SolidColorBrush(winColor);
        colorText.Text = $"#{rgb.r:X2}{rgb.g:X2}{rgb.b:X2}  ({rgb.r}, {rgb.g}, {rgb.b})";
        picker.Color = winColor;

        picker.ColorChanged += (s, args) =>
        {
            if (_isApplyingModel) return;
            var c = args.NewColor;
            _draftParameters[schema.Key] = new int[] { c.R, c.G, c.B };
            swatchBorder.Background = new SolidColorBrush(c);
            colorText.Text = $"#{c.R:X2}{c.G:X2}{c.B:X2}  ({c.R}, {c.G}, {c.B})";
            EvaluateDirty();
        };

        var flyout = new Flyout
        {
            Content = picker
        };
        dropDownBtn.Flyout = flyout;

        container.Children.Add(dropDownBtn);
        DynamicParametersPanel.Children.Add(container);
    }

    private void BuildBooleanControl(EffectParamSchemaDto schema)
    {
        bool curVal = ExtractBool(_draftParameters.GetValueOrDefault(schema.Key)) ?? false;

        var toggle = new ToggleSwitch
        {
            Header = schema.DisplayName,
            IsOn = curVal,
            MaxWidth = 540,
            HorizontalAlignment = HorizontalAlignment.Left
        };

        toggle.Toggled += (s, args) =>
        {
            if (_isApplyingModel) return;
            _draftParameters[schema.Key] = toggle.IsOn;
            EvaluateDirty();
        };

        DynamicParametersPanel.Children.Add(toggle);
    }

    private void BuildEnumControl(EffectParamSchemaDto schema)
    {
        var container = new StackPanel
        {
            Spacing = 6,
            MaxWidth = 540,
            HorizontalAlignment = HorizontalAlignment.Left
        };

        var header = new TextBlock
        {
            Text = schema.DisplayName,
            Style = (Style)Application.Current.Resources["BodyStrongTextBlockStyle"]
        };
        container.Children.Add(header);

        var combo = new ComboBox
        {
            MinWidth = 240,
            DisplayMemberPath = "Label"
        };

        string curVal = ExtractString(_draftParameters.GetValueOrDefault(schema.Key)) ?? "";
        EffectParamOptionDto? selectedOpt = null;

        if (schema.Options != null)
        {
            foreach (var opt in schema.Options)
            {
                combo.Items.Add(opt);
                if (string.Equals(opt.Value, curVal, StringComparison.OrdinalIgnoreCase))
                {
                    selectedOpt = opt;
                }
            }
        }

        combo.SelectedItem = selectedOpt ?? schema.Options?.FirstOrDefault();

        combo.SelectionChanged += (s, args) =>
        {
            if (_isApplyingModel) return;
            if (combo.SelectedItem is EffectParamOptionDto opt)
            {
                _draftParameters[schema.Key] = opt.Value;
                EvaluateDirty();
            }
        };

        container.Children.Add(combo);
        DynamicParametersPanel.Children.Add(container);
    }

    private void BuildNumberControl(EffectParamSchemaDto schema)
    {
        var container = new StackPanel
        {
            Spacing = 6,
            MaxWidth = 540,
            HorizontalAlignment = HorizontalAlignment.Stretch
        };

        var grid = new Grid
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            ColumnSpacing = 12
        };

        grid.ColumnDefinitions.Add(
            new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        grid.ColumnDefinitions.Add(
            new ColumnDefinition { Width = GridLength.Auto });

        var header = new TextBlock
        {
            Text = schema.DisplayName,
            Style = (Style)Application.Current.Resources["BodyStrongTextBlockStyle"],
            TextWrapping = TextWrapping.Wrap,
            VerticalAlignment = VerticalAlignment.Center
        };

        var valText = new TextBlock
        {
            Style = (Style)Application.Current.Resources["BodyStrongTextBlockStyle"],
            TextWrapping = TextWrapping.NoWrap,
            VerticalAlignment = VerticalAlignment.Center
        };

        Grid.SetColumn(header, 0);
        Grid.SetColumn(valText, 1);

        grid.Children.Add(header);
        grid.Children.Add(valText);
        container.Children.Add(grid);

        double curVal = ExtractDouble(_draftParameters.GetValueOrDefault(schema.Key)) ?? 1.0;
        valText.Text = curVal.ToString("0.0");

        var slider = new Slider
        {
            Minimum = schema.Min ?? 0.1,
            Maximum = schema.Max ?? 5.0,
            StepFrequency = schema.Step ?? 0.1,
            Value = curVal,
            HorizontalAlignment = HorizontalAlignment.Stretch
        };

        slider.ValueChanged += (s, args) =>
        {
            if (_isApplyingModel) return;
            double rounded = Math.Round(args.NewValue, 2);
            valText.Text = rounded.ToString("0.0");
            _draftParameters[schema.Key] = rounded;
            EvaluateDirty();
        };

        container.Children.Add(slider);
        DynamicParametersPanel.Children.Add(container);
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

    private void EvaluateDirty()
    {
        if (_cleanLighting == null)
        {
            SetDirty(false);
            return;
        }

        // 预设变更判定
        bool presetChanged = false;
        if (!_cleanLighting.IsBuiltinPreset)
        {
            // 如果原先是高级方案，用户选中了任何一个内置预设即为 dirty
            presetChanged = (_draftPreset != null);
        }
        else
        {
            presetChanged = (_draftPreset == null ||
                             !string.Equals(_draftPreset.Id, _cleanLighting.PresetId, StringComparison.OrdinalIgnoreCase));
        }

        // 亮度变更判定
        bool bDiff = Math.Abs(_draftBrightnessRatio - _cleanLighting.Brightness) > 0.005;

        // 周期变更判定 (仅当当前预设支持周期时计算)
        bool perDiff = false;
        if (_draftPreset != null && _draftPreset.SupportsPeriod)
        {
            perDiff = (_draftPeriodMs != _cleanLighting.PeriodMs);
        }

        // 动态参数变更判定
        bool paramsDiff = false;
        if (presetChanged)
        {
            paramsDiff = true;
        }
        else if (_draftPreset != null && _draftPreset.ParameterSchema != null)
        {
            foreach (var schema in _draftPreset.ParameterSchema)
            {
                var draftVal = _draftParameters.GetValueOrDefault(schema.Key);
                object? cleanVal = null;
                if (_cleanLighting.Parameters != null && _cleanLighting.Parameters.TryGetValue(schema.Key, out var cleanJe))
                {
                    cleanVal = cleanJe;
                }
                if (!ParameterValuesEqual(schema.Type, draftVal, cleanVal))
                {
                    paramsDiff = true;
                    break;
                }
            }
        }

        SetDirty(presetChanged || bDiff || perDiff || paramsDiff);
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

    private static bool ParameterValuesEqual(EffectParamType type, object? a, object? b)
    {
        return EffectParamValueComparer.AreValuesEqual(type, a, b);
    }

    private static (int r, int g, int b)? ExtractRgb(object? val) => EffectParamValueComparer.ExtractRgb(val);
    private static bool? ExtractBool(object? val) => EffectParamValueComparer.ExtractBool(val);
    private static string? ExtractString(object? val) => EffectParamValueComparer.ExtractString(val);
    private static double? ExtractDouble(object? val) => EffectParamValueComparer.ExtractDouble(val);

    private async void SaveBtn_Click(object sender, RoutedEventArgs e)
    {
        if (!_isDirty || _cleanLighting == null)
        {
            return;
        }

        SaveBtn.IsEnabled = false;

        // 构建真正 Sparse Patch
        var patch = new BaseLightingPatchDto
        {
            ExpectedRevision = _currentRevision
        };

        bool presetChanged = false;
        if (!_cleanLighting.IsBuiltinPreset)
        {
            if (_draftPreset != null)
            {
                patch.Preset = _draftPreset.Id;
                presetChanged = true;
            }
        }
        else if (_draftPreset != null && !string.Equals(_draftPreset.Id, _cleanLighting.PresetId, StringComparison.OrdinalIgnoreCase))
        {
            patch.Preset = _draftPreset.Id;
            presetChanged = true;
        }

        if (Math.Abs(_draftBrightnessRatio - _cleanLighting.Brightness) > 0.005)
        {
            patch.Brightness = Math.Round(_draftBrightnessRatio, 4);
        }

        if (_draftPreset != null && _draftPreset.SupportsPeriod)
        {
            if (presetChanged || (_draftPeriodMs.HasValue && _draftPeriodMs != _cleanLighting.PeriodMs))
            {
                patch.PeriodMs = _draftPeriodMs ?? _draftPreset.DefaultPeriodMs ?? 3000;
            }
        }

        // Parameters 调控：
        // 预设改变时，下发目标预设的全部草稿参数以覆盖其默认值
        // 预设未变时，仅下发相比 cleanLighting 发生实质变化的参数 (true sparse patch)
        if (_draftPreset != null && _draftPreset.ParameterSchema != null && _draftPreset.ParameterSchema.Count > 0)
        {
            var patchParams = new Dictionary<string, object>();
            if (presetChanged)
            {
                foreach (var schema in _draftPreset.ParameterSchema)
                {
                    if (_draftParameters.TryGetValue(schema.Key, out var val))
                    {
                        patchParams[schema.Key] = val;
                    }
                }
            }
            else
            {
                foreach (var schema in _draftPreset.ParameterSchema)
                {
                    var draftVal = _draftParameters.GetValueOrDefault(schema.Key);
                    object? cleanVal = null;
                    if (_cleanLighting.Parameters != null && _cleanLighting.Parameters.TryGetValue(schema.Key, out var cleanJe))
                    {
                        cleanVal = cleanJe;
                    }
                    if (!ParameterValuesEqual(schema.Type, draftVal, cleanVal) && draftVal != null)
                    {
                        patchParams[schema.Key] = draftVal;
                    }
                }
            }

            if (patchParams.Count > 0)
            {
                patch.Parameters = patchParams;
            }
        }

        var res = await _client.UpdateBaseLightingAsync(patch);

        if (res.IsSuccess)
        {
            ShowNotification(InfoBarSeverity.Success, "配置已保存", "系统默认灯效已成功更新并通过热重载生效。");
            await LoadLightingPageDataAsync();
            await RefreshRuntimeStatusAsync();
        }
        else if (res.IsConflict)
        {
            // 约束 4: 409 冲突处理，明确提示后重新拉取并丢弃 stale draft
            ShowNotification(InfoBarSeverity.Warning, "配置已被外部修改", "配置文件已被其他编辑器修改，已自动为您重新载入最新配置，本地草稿已丢弃。");
            await LoadLightingPageDataAsync();
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
