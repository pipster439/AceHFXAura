using Aura_WinUI.Common;
using Aura_WinUI.Services;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using System.Globalization;
using System.Runtime.InteropServices;

namespace Aura_WinUI.Pages;

public sealed partial class MagneticSwitchPage : Page
{
    [DllImport("user32.dll")]
    private static extern short GetKeyState(int virtualKey);
    private MagneticSettingsModel _model;
    private readonly Dictionary<ushort, Button> _keyButtons = [];
    private readonly Dictionary<ushort, (TextBlock Legend, TextBlock Actuation, TextBlock RtPress,
        TextBlock RtRelease)> _keycapParts = [];
    private readonly Dictionary<ushort, Border> _keyDepthBars = [];
    private readonly MagneticHallClient _hallClient = new();
    private readonly Microsoft.UI.Dispatching.DispatcherQueueTimer? _hallTimer;
    private bool _hallPolling;
    private bool _hallActive;
    private bool _verifyRunning;
    private double _lastKeyboardKeyHeight = 38.0;
    private double _liveSelectedDepthMm;
    private ushort _liveSelectedRaw;
    private bool _liveSelectedDigitalDown;
    private readonly List<Canvas> _keyboardCanvases = [];
    private readonly List<(ComboBox Target, ComboBox DownStart, ComboBox DownEnd,
        ComboBox UpStart, ComboBox UpEnd)> _dksControls = [];
    private readonly Button _quarantineRecoveryButton = new()
    {
        Content = "我已完成外部重新同步…",
        Visibility = Visibility.Collapsed
    };
    private bool _rendering;

    public MagneticSettingsModel Model => _model;

    public MagneticSwitchPage() : this(new MagneticSettingsModel(new MagneticControlClient(),
        profileClient: new ProfileControlClient()))
    {
    }

    public MagneticSwitchPage(MagneticSettingsModel model)
    {
        _model = model;
        InitializeComponent();
        // Set fractional ranges in code: XAML numeric conversion can follow the host locale.
        _rendering = true;
        ConfigureSlider(ActuationSlider, 0.1, 4.0);
        ConfigureSlider(PressSlider, 0.1, 2.5);
        ConfigureSlider(ReleaseSlider, 0.1, 2.5);
        ConfigureSlider(TopSlider, 0, 0.5);
        ConfigureSlider(BottomSlider, 0, 0.5);
        ConfigureSlider(DksStartSlider, 0.1, 4.0);
        ConfigureSlider(DksEndSlider, 0.1, 4.0);
        _rendering = false;
        AutomationProperties.SetAutomationId(_quarantineRecoveryButton, "MagneticQuarantineRecoveryButton");
        _quarantineRecoveryButton.Click += QuarantineRecoveryButton_Click;
        HealthBar.ActionButton = _quarantineRecoveryButton;
        NavigationCacheMode = Microsoft.UI.Xaml.Navigation.NavigationCacheMode.Required;
        BuildKeyboard();
        BuildDksEditor();
        FillBatchPicker(BatchActuationPicker, 1, 40);
        FillBatchPicker(BatchRtPressPicker, 1, 25);
        FillBatchPicker(BatchRtReleasePicker, 1, 25);
        FillBatchPicker(BatchTopPicker, 0, 5);
        FillBatchPicker(BatchBottomPicker, 0, 5);
        foreach (var key in MagneticKeyLayout.Keys)
        {
            SpeedTapKey1.Items.Add(new ComboBoxItem { Content = key.FullName, Tag = key.LogicalId });
            SpeedTapKey2.Items.Add(new ComboBoxItem { Content = key.FullName, Tag = key.LogicalId });
        }
        PageLayout.Attach(this, PageScroll, PageContent, width =>
        {
            double unit = Math.Max(34, ((int)Math.Clamp((width - 120) / 16.75, 34.0, 52.0)) & ~1);
            double keyH = width >= 900 ? 46.0 : 40.0;
            _lastKeyboardKeyHeight = keyH;

            bool sideBySideViz = width >= 760;
            BasicWorkbenchGrid.ColumnDefinitions[0].Width = sideBySideViz ? new GridLength(244) : new GridLength(1, GridUnitType.Star);
            BasicWorkbenchGrid.ColumnDefinitions[1].Width = sideBySideViz ? new GridLength(1, GridUnitType.Star) : new GridLength(0);
            Grid.SetColumn(CommonLowerGrid, sideBySideViz ? 1 : 0);
            Grid.SetRow(CommonLowerGrid, sideBySideViz ? 0 : 1);

            double rightWidth = sideBySideViz ? Math.Max(0, width - 260) : width;
            bool twoCommonColumns = rightWidth >= 680;
            CommonLowerGrid.ColumnDefinitions[1].Width = twoCommonColumns ? new GridLength(1, GridUnitType.Star) : new GridLength(0);
            Grid.SetColumn(RapidTriggerCard, twoCommonColumns ? 1 : 0);
            Grid.SetRow(RapidTriggerCard, twoCommonColumns ? 0 : 1);
            Grid.SetRow(DeadzoneCard, twoCommonColumns ? 1 : 2);
            Grid.SetColumnSpan(DeadzoneCard, twoCommonColumns ? 2 : 1);

            bool twoDzColumns = rightWidth >= 500;
            DeadzoneSlidersGrid.ColumnDefinitions[1].Width = twoDzColumns ? new GridLength(1, GridUnitType.Star) : new GridLength(0);
            Grid.SetColumn(BottomDzCol, twoDzColumns ? 1 : 0);
            Grid.SetRow(BottomDzCol, twoDzColumns ? 0 : 1);

            bool twoHallColumns = width >= 900;
            HallSectionGrid.ColumnDefinitions[1].Width = twoHallColumns ? new GridLength(1, GridUnitType.Star) : new GridLength(0);
            Grid.SetColumn(WootingBridgeCard, twoHallColumns ? 1 : 0);
            Grid.SetRow(WootingBridgeCard, twoHallColumns ? 0 : 1);

            PageLayout.Columns(DksSlotsHost, width >= 760 ? 2 : 1);

            foreach (var key in MagneticKeyLayout.Keys)
            {
                var button = _keyButtons[key.LogicalId];
                button.Width = unit * key.Units;
                button.Height = keyH;
                Canvas.SetLeft(button, MagneticKeyLayout.LeftOf(key, unit));
            }
            foreach (var canvas in _keyboardCanvases)
            {
                canvas.Width = MagneticKeyLayout.RightmostColumn(unit) + 1.3 * unit;
                canvas.Height = keyH;
            }
        }, maxWidth: 1280);
        PageScroll.SizeChanged += (_, _) => PageHost.MinHeight = PageScroll.ViewportHeight;
        Loaded += async (_, _) =>
        {
            await _model.RefreshAsync();
            Render();
            if (_hallActive || _verifyRunning) _hallTimer?.Start();
        };
        Unloaded += (_, _) => _hallTimer?.Stop();
        _hallTimer = DispatcherQueue.CreateTimer();
        _hallTimer.Interval = TimeSpan.FromMilliseconds(40);
        _hallTimer.Tick += HallTimer_Tick;
        ActualThemeChanged += (_, _) => Render();
        Render();
    }

    private static void ConfigureSlider(Slider slider, double minimum, double maximum)
    {
        slider.Maximum = maximum;
        slider.Minimum = minimum;
        slider.StepFrequency = 0.1;
        slider.Value = minimum;
    }

    private static void FillBatchPicker(ComboBox picker, int minRaw, int maxRaw)
    {
        for (int raw = minRaw; raw <= maxRaw; ++raw)
            picker.Items.Add(new ComboBoxItem { Content = $"{raw / 10.0:F1} mm", Tag = raw / 10.0 });
    }

    private static void ShowBatchChoice(ComboBox picker, double? value, int minRaw)
    {
        picker.SelectedIndex = value is double mm ? (int)Math.Round(mm * 10) - minRaw : -1;
    }

    internal void SetModelForValidation(MagneticSettingsModel model)
    {
        _model = model;
        Render();
    }

    internal void ForceRender() => Render();

    private void BuildDksEditor()
    {
        string[] labels = ["无操作", "单次触发", "释放", "保持"];
        string[] states = ["Inactive", "Tap", "Release", "Hold"];
        DksSlotsHost.Children.Clear();
        _dksControls.Clear();

        for (int index = 0; index < 4; index++)
        {
            var slotBorder = new Border
            {
                Background = (Brush)Application.Current.Resources["CardBackgroundFillColorSecondaryBrush"],
                BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"],
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(10)
            };
            var panel = new StackPanel { Spacing = 6 };
            var slotTitle = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
            slotTitle.Children.Add(new TextBlock
            {
                Text = $"动作 {index + 1}",
                Style = (Style)Application.Current.Resources["CaptionTextBlockStyle"],
                FontWeight = FontWeights.SemiBold
            });
            panel.Children.Add(slotTitle);

            var target = new ComboBox { Header = "目标按键", HorizontalAlignment = HorizontalAlignment.Stretch };
            target.Items.Add(new ComboBoxItem { Content = "标准 / 无附加键", Tag = null });
            foreach (var key in MagneticKeyLayout.Keys.Where(k => MagneticKeyLayout.IsValidDksActionTarget(k.LogicalId)))
                target.Items.Add(new ComboBoxItem { Content = key.FullName, Tag = key.LogicalId });
            target.SelectionChanged += DksSlot_SelectionChanged;
            AutomationProperties.SetAutomationId(target, $"MagneticDksTarget{index + 1}");
            AutomationProperties.SetName(target, $"动作 {index + 1} 目标按键");
            panel.Children.Add(target);

            ComboBox MakeState(string header, string automationPrefix)
            {
                var box = new ComboBox { Header = header, HorizontalAlignment = HorizontalAlignment.Stretch };
                for (int state = 0; state < 4; ++state)
                    box.Items.Add(new ComboBoxItem { Content = labels[state], Tag = states[state] });
                box.SelectionChanged += DksSlot_SelectionChanged;
                AutomationProperties.SetAutomationId(box, $"MagneticDks{automationPrefix}{index + 1}");
                AutomationProperties.SetName(box, $"动作 {index + 1} {header}");
                return box;
            }

            var downStart = MakeState("按下起点", "按下起点");
            var downEnd = MakeState("按下终点", "按下终点");
            var upStart = MakeState("抬起起点", "抬起起点");
            var upEnd = MakeState("抬起终点", "抬起终点");

            var downRow = new Grid { ColumnSpacing = 8 };
            downRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            downRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            Grid.SetColumn(downStart, 0); Grid.SetColumn(downEnd, 1);
            downRow.Children.Add(downStart); downRow.Children.Add(downEnd);

            var upRow = new Grid { ColumnSpacing = 8 };
            upRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            upRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            Grid.SetColumn(upStart, 0); Grid.SetColumn(upEnd, 1);
            upRow.Children.Add(upStart); upRow.Children.Add(upEnd);

            panel.Children.Add(downRow);
            panel.Children.Add(upRow);
            slotBorder.Child = panel;
            DksSlotsHost.Children.Add(slotBorder);
            _dksControls.Add((target, downStart, downEnd, upStart, upEnd));
        }
    }

    private void BuildKeyboard()
    {
        for (int row = 0; row < 5; row++)
        {
            var panel = new Canvas { Height = 38 };
            foreach (var key in MagneticKeyLayout.Keys.Where(key => key.Row == row))
            {
                var keycap = new Grid { IsHitTestVisible = false, MinWidth = 0, MinHeight = 0 };
                TextBlock Corner(string name, HorizontalAlignment horizontal, VerticalAlignment vertical)
                {
                    return new TextBlock {
                        Name = name, Text = "—", FontSize = 7.5, Opacity = 0.22,
                        TextTrimming = TextTrimming.CharacterEllipsis, MaxLines = 1,
                        HorizontalAlignment = horizontal, VerticalAlignment = vertical,
                        Margin = new Thickness(3, 2, 3, 2)
                    };
                }
                var legend = new TextBlock {
                    Name = $"MagneticKeyLegend{key.LogicalId:X4}", Text = key.Label,
                    FontSize = 12, FontWeight = FontWeights.SemiBold,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center,
                    IsHitTestVisible = false
                };
                var actuation = Corner($"MagneticKeyActuation{key.LogicalId:X4}",
                    HorizontalAlignment.Left, VerticalAlignment.Top);
                var rtPress = Corner($"MagneticKeyRtPress{key.LogicalId:X4}",
                    HorizontalAlignment.Right, VerticalAlignment.Top);
                var rtRelease = Corner($"MagneticKeyRtRelease{key.LogicalId:X4}",
                    HorizontalAlignment.Right, VerticalAlignment.Bottom);
                var depthBar = new Border
                {
                    HorizontalAlignment = HorizontalAlignment.Stretch,
                    VerticalAlignment = VerticalAlignment.Bottom,
                    Height = 0,
                    CornerRadius = new CornerRadius(4),
                    Background = (Brush)Application.Current.Resources["AccentFillColorSecondaryBrush"],
                    Opacity = 0.45,
                    IsHitTestVisible = false
                };
                keycap.Children.Add(depthBar);
                _keyDepthBars.Add(key.LogicalId, depthBar);
                keycap.Children.Add(actuation);
                keycap.Children.Add(rtPress);
                keycap.Children.Add(rtRelease);
                keycap.Children.Add(legend);
                var button = new Button
                {
                    Content = keycap,
                    Width = 38 * key.Units,
                    Height = 38,
                    MinWidth = 0,
                    Padding = new Thickness(1),
                    HorizontalContentAlignment = HorizontalAlignment.Stretch,
                    VerticalContentAlignment = VerticalAlignment.Stretch,
                    CornerRadius = new CornerRadius(5),
                    FontSize = 12,
                    FontWeight = FontWeights.SemiBold,
                    Tag = key.LogicalId
                };
                Canvas.SetLeft(button, MagneticKeyLayout.LeftOf(key, 38));
                AutomationProperties.SetName(button, $"选择 {key.FullName}");
                AutomationProperties.SetAutomationId(button, $"MagneticKey{key.LogicalId:X4}");
                button.Click += Key_Click;
                panel.Children.Add(button);
                _keyButtons.Add(key.LogicalId, button);
                _keycapParts.Add(key.LogicalId, (legend, actuation, rtPress, rtRelease));
            }
            panel.Width = MagneticKeyLayout.RightmostColumn(38) + 1.3 * 38;
            _keyboardCanvases.Add(panel);
            KeyboardRows.Children.Add(panel);
        }
    }

    private void Key_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: ushort logicalId }) return;
        bool ctrl = (GetKeyState(0x11) & 0x8000) != 0;
        if ((ctrl || _model.IsMultiMode) ? _model.ToggleMulti(logicalId) : _model.Select(logicalId)) Render();
    }

    private void Render()
    {
        _rendering = true;
        try
        {
            var selected = _model.SelectedKey;
            var draft = _model.Draft;
            var status = _model.Status;
            var key = _model.SelectedLogicalId;
            var actuation = status?.Actuation.FirstOrDefault(value => value.LogicalId == key);
            var rt = status?.RapidTrigger.FirstOrDefault(value => value.LogicalId == key);
            var deadzone = status?.Deadzone.FirstOrDefault(value => value.LogicalId == key);
            var dks = status?.Dks.FirstOrDefault(value => value.LogicalId == key && value.Source == "SessionApplied");

            GlobalSelectButton.Style = _model.IsGlobalMode ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
            MultiSelectButton.Style = _model.IsMultiMode ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
            ClearSelectionButton.Visibility = _model.IsMultiMode ? Visibility.Visible : Visibility.Collapsed;
            ClearSelectionButton.IsEnabled = _model.IsMultiMode && _model.SelectedCount > 0 && !_model.Busy;
            GlobalContextNotice.IsOpen = _model.IsGlobalMode;
            MultiContextNotice.IsOpen = _model.IsMultiMode;

            // 1. Selected Key / Global Hero Presentation
            if (_model.IsGlobalMode)
            {
                KeyCapVisualText.Text = "全";
                KeyCapVisualBorder.Background = (Brush)Application.Current.Resources["AccentFillColorDefaultBrush"];
                KeyCapVisualText.Foreground = (Brush)Application.Current.Resources["TextOnAccentFillColorPrimaryBrush"];
                SelectedKeyHeroTitle.Text = "全部按键";
                SelectedKeyHeroDesc.Text = "全局磁轴设置";
                SelectedKeyTagText.Text = "全局基础值";
                SelectedKeyDebugInfo.Text = "";
            }
            else if (selected != null)
            {
                KeyCapVisualText.Text = selected.Label;
                KeyCapVisualBorder.Background = (Brush)Application.Current.Resources["AccentFillColorDefaultBrush"];
                KeyCapVisualText.Foreground = (Brush)Application.Current.Resources["TextOnAccentFillColorPrimaryBrush"];
                SelectedKeyHeroTitle.Text = selected.FullName;
                SelectedKeyHeroDesc.Text = $"Row {selected.Row + 1} · 实体按键";
                SelectedKeyTagText.Text = "选中按键";
                SelectedKeyDebugInfo.Text = $"0x{selected.LogicalId:X4}";
            }
            else if (_model.IsMultiMode)
            {
                KeyCapVisualText.Text = _model.SelectedCount.ToString();
                KeyCapVisualBorder.Background = (Brush)Application.Current.Resources["AccentFillColorDefaultBrush"];
                KeyCapVisualText.Foreground = (Brush)Application.Current.Resources["TextOnAccentFillColorPrimaryBrush"];
                SelectedKeyHeroTitle.Text = $"{_model.SelectedCount} 个按键";
                SelectedKeyHeroDesc.Text = _model.MultiSelectionSummary;
                SelectedKeyTagText.Text = "多键选择";
                SelectedKeyDebugInfo.Text = "";
            }
            else
            {
                KeyCapVisualText.Text = "--";
                KeyCapVisualBorder.Background = (Brush)Application.Current.Resources["SubtleFillColorSecondaryBrush"];
                KeyCapVisualText.Foreground = (Brush)Application.Current.Resources["TextFillColorSecondaryBrush"];
                SelectedKeyHeroTitle.Text = "未选择按键";
                SelectedKeyHeroDesc.Text = "请在左侧键盘中点击任意实体按键开始配置";
                SelectedKeyTagText.Text = "未选择";
                SelectedKeyDebugInfo.Text = "";
            }

            foreach (var (id, button) in _keyButtons)
            {
                bool isSelected = _model.SelectedLogicalId == id || _model.IsMultiMode && _model.SelectedLogicalIds.Contains(id);
                button.Style = isSelected ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
                var visual = MagneticKeyLayout.Find(id)!;
                var overlay = _model.KeyOverlay(id);
                var parts = _keycapParts[id];
                parts.Actuation.Text = overlay.ActuationText;
                parts.RtPress.Text = overlay.RtPressText;
                parts.RtRelease.Text = overlay.RtReleaseText;
                // The Button's theme-aware foreground also tracks its AccentButtonStyle.
                // A brush fetched from Application.Resources can have the wrong theme
                // when only this window requests Light or Dark.
                parts.Legend.Foreground = button.Foreground;
                parts.Actuation.Foreground = button.Foreground;
                parts.RtPress.Foreground = button.Foreground;
                parts.RtRelease.Foreground = button.Foreground;
                parts.Actuation.Opacity = overlay.ActuationText == "—" ? 0.20 :
                    overlay.ActuationSource is
                        MagneticOverlaySource.SessionGlobalBaseline or MagneticOverlaySource.HostGlobalBaseline ? 0.55 : 0.85;
                parts.RtPress.Opacity = overlay.RtPressText == "—" ? 0.18 : (overlay.RtPressKnown ? 0.85 : 0.50);
                parts.RtRelease.Opacity = overlay.RtReleaseText == "—" ? 0.18 : (overlay.RtReleaseKnown ? 0.85 : 0.50);
                button.BorderThickness = new Thickness(isSelected ? 2 : 1);
                button.IsEnabled = !_model.Busy;
                AutomationProperties.SetName(button, $"{visual.FullName}，{(isSelected ? "已选择" : "未选择")}，{overlay.AccessibilityText}");
                AutomationProperties.SetHelpText(button, "键帽显示已知逐键配置或标注的全局基线，非设备读回。点击选中；Ctrl+点击切换多选；多选模式中点击切换选择");
                ToolTipService.SetToolTip(button, $"{visual.FullName}：{overlay.AccessibilityText}。本页不是设备读回。");
            }
            AutomationProperties.SetName(SelectedKeyHeroCard, SelectedKeyHeroTitle.Text + "，" + SelectedKeyTagText.Text);
            AutomationProperties.SetName(MultiSelectButton, _model.IsMultiMode ?
                $"多选模式，已选择 {_model.SelectedCount} 个按键" : "进入多选模式");
            GlobalSelectButton.IsEnabled = !_model.Busy;
            MultiSelectButton.IsEnabled = !_model.Busy;
            MultiContextNotice.Message = _model.SelectedCount >= 12 ?
                "批量设置会逐个安全应用，选择的按键较多时可能需要一些时间。" :
                "批量设置按键逐个安全应用；每个按键保留独立事务。";
            var batchResult = _model.LastBatchResult;
            BatchResultBar.IsOpen = batchResult is { AppliedCount: > 0 } or { FailedCount: > 0 };
            BatchResultBar.Severity = batchResult?.CompletedFully == true ? InfoBarSeverity.Success : InfoBarSeverity.Error;
            BatchResultBar.Title = batchResult?.CompletedFully == true ? "批量应用完成" : "批量应用未完整完成";
            BatchResultBar.Message = batchResult == null ? "" :
                $"已应用：{batchResult.AppliedCount} · 失败：{batchResult.FailedCount} · 未执行：{batchResult.NotExecutedCount}";
            BatchResultDetailsText.Visibility = batchResult == null ? Visibility.Collapsed : Visibility.Visible;
            BatchResultDetailsText.Text = batchResult == null ? "" : string.Join("\n", batchResult.Results.Select(item =>
                $"{MagneticKeyLayout.Find(item.LogicalId)?.Label ?? $"0x{item.LogicalId:X4}"}  {item.Status switch {
                    "Applied" => "已应用", "Failed" => "失败", _ => "未执行"}}" +
                (string.IsNullOrWhiteSpace(item.Detail) ? "" : $"：{item.Detail}")));

            RapidTriggerMasterStatusText.Text = _model.RapidTriggerMasterText;
            RapidTriggerMasterExplanationText.Text = MagneticSettingsModel.RapidTriggerMasterExplanation;

            if (_model.IsGlobalMode)
            {
                // Card headers & descriptions
                ActuationCardTitle.Text = "全局触发点";
                ActuationCardDesc.Text = "手动全局设置的基础触发深度 (0.1–4.0 mm)；配置文件覆盖时仍显示基础值";
                RapidTriggerCardTitle.Text = "旧版快速触发参数（只读）";
                RapidTriggerCardDesc.Text = "请在键盘中选择单键或多键后设置快速触发；本页不再编辑旧版整体参数";
                DeadzoneCardTitle.Text = "全局死区设置";
                DeadzoneCardDesc.Text = "手动全局设置的基础死区 (0.0–0.5 mm)；不清除已有逐键覆盖";
                DksSection.Visibility = Visibility.Collapsed;

                RapidTriggerToggle.Visibility = Visibility.Collapsed;
                SeparateModeToggle.Visibility = Visibility.Visible;

                // Enabled states
                ActuationSlider.IsEnabled = _model.CanWriteGlobal;
                PressSlider.IsEnabled = false;
                ReleaseSlider.IsEnabled = false;
                TopSlider.IsEnabled = _model.CanWriteGlobal;
                BottomSlider.IsEnabled = _model.CanWriteGlobal;
                SeparateModeToggle.IsEnabled = false;

                // Values & Displays
                var globalDraft = _model.GlobalDraft;
                var globalAct = _model.ManualGlobalActuation;
                var globalRt = _model.ManualGlobalRapidTrigger;
                var globalDz = _model.ManualGlobalDeadzone;

                bool sepMode = globalDraft.SeparateMode ?? globalRt.SeparateMode ??
                    (globalRt.Known && globalRt.PressRaw != globalRt.ReleaseRaw);
                SeparateModeToggle.IsOn = sepMode;

                ActuationSlider.Value = globalDraft.ActuationMm ??
                    (globalAct.Known ? globalAct.Raw / 10.0 : 1.0);
                PressSlider.Value = globalDraft.PressMm ??
                    (globalRt.Known ? globalRt.PressRaw / 10.0 : 0.4);
                ReleaseSlider.Value = globalDraft.ReleaseMm ??
                    (globalRt.Known ? globalRt.ReleaseRaw / 10.0 : 0.2);
                TopSlider.Value = globalDraft.TopMm ??
                    (globalDz.Known ? globalDz.TopRaw / 10.0 : 0.0);
                BottomSlider.Value = globalDraft.BottomMm ??
                    (globalDz.Known ? globalDz.BottomRaw / 10.0 : 0.1);

                ActuationDraftText.Text = Label("草稿", globalDraft.ActuationMm);
                ActuationValueDisplay.Text = $"{ActuationSlider.Value:F1} mm";
                PressDraftText.Text = Label("按下草稿", globalDraft.PressMm);
                PressValueDisplay.Text = $"{PressSlider.Value:F1} mm";
                ReleaseDraftText.Text = Label("抬起草稿", globalDraft.ReleaseMm);
                ReleaseValueDisplay.Text = $"{ReleaseSlider.Value:F1} mm";
                TopDraftText.Text = Label("顶部草稿", globalDraft.TopMm);
                TopValueDisplay.Text = $"{TopSlider.Value:F1} mm";
                BottomDraftText.Text = Label("底部草稿", globalDraft.BottomMm);
                BottomValueDisplay.Text = $"{BottomSlider.Value:F1} mm";

                // Provenance
                ActuationAppliedText.Text = globalAct.Known ?
                    $"{SourceLabel(globalAct.Source)}：{globalAct.Raw / 10.0:F1} mm" +
                    (_model.ProfileOverridesActuation ?
                        $" · 当前配置文件提交：{status!.GlobalActuation.Raw / 10.0:F1} mm（非设备读回）" : "") :
                    "手动全局触发点：未知";
                RapidTriggerAppliedText.Text = globalRt.Known ?
                    $"{SourceLabel(globalRt.Source)}：按下 {globalRt.PressRaw / 10.0:F1} mm，抬起 {globalRt.ReleaseRaw / 10.0:F1} mm ({(globalRt.SeparateMode == true ? "独立灵敏度" : "同步灵敏度")})" :
                    "请选择按键后设置快速触发；旧版批量写入已停用";
                RapidTriggerDisableWarningText.Visibility = Visibility.Collapsed;
                DeadzoneAppliedText.Text = globalDz.Known ?
                    $"{SourceLabel(globalDz.Source)}：顶部 {globalDz.TopRaw / 10.0:F1} mm，底部 {globalDz.BottomRaw / 10.0:F1} mm" +
                    (_model.ProfileOverridesDeadzone ?
                        $" · 当前配置文件提交：顶部 {status!.GlobalDeadzone.TopRaw / 10.0:F1} mm，底部 {status.GlobalDeadzone.BottomRaw / 10.0:F1} mm（非设备读回）" : "") :
                    "手动全局死区：未知";

                // Apply buttons
                ActuationApplyButton.Content = "应用全局触发点";
                bool actDirty = _model.CanWriteGlobal && globalDraft.ActuationMm != null;
                ActuationApplyButton.IsEnabled = actDirty;
                ActuationApplyButton.Style = actDirty ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;

                RapidTriggerApplyButton.Content = "旧版写入已停用";
                bool rtDirty = false; // 51 53 remains fail-closed.
                RapidTriggerApplyButton.IsEnabled = rtDirty;
                RapidTriggerApplyButton.Style = rtDirty ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;

                DeadzoneApplyButton.Content = "应用全局死区";
                bool dzDirty = _model.CanWriteGlobal && globalDraft.TopMm != null && globalDraft.BottomMm != null;
                DeadzoneApplyButton.IsEnabled = dzDirty;
                DeadzoneApplyButton.Style = dzDirty ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
            }
            else
            {
                // Reset card headers for per-key mode
                ActuationCardTitle.Text = "触发点";
                ActuationCardDesc.Text = "按键按下触发深度 (0.1–4.0 mm)；移动滑杆仅修改草稿";
                RapidTriggerCardTitle.Text = "快速触发";
                RapidTriggerCardDesc.Text = "根据移动方向即时重置与重新触发";
                DeadzoneCardTitle.Text = "死区设置";
                DeadzoneCardDesc.Text = "消除微触误触与触底杂音 (0.0–0.5 mm)";
                DksSection.Visibility = _model.IsMultiMode ? Visibility.Collapsed : Visibility.Visible;
                DksEditorBody.Visibility = _model.DksEditorOpen ? Visibility.Visible : Visibility.Collapsed;
                DksConfigureButton.Content = _model.DksEditorOpen ? "收起编辑器" : "展开编辑器";

                RapidTriggerToggle.Visibility = Visibility.Visible;
                SeparateModeToggle.Visibility = Visibility.Collapsed;

                ActuationApplyButton.Content = "应用触发点";
                RapidTriggerApplyButton.Content = "应用快速触发";
                DeadzoneApplyButton.Content = "应用单键死区";

                // 2. Control Enabled States
                ActuationSlider.IsEnabled = _model.CanWriteSelected;
            PressSlider.IsEnabled = _model.CanWriteSelected;
            ReleaseSlider.IsEnabled = _model.CanWriteSelected;
            TopSlider.IsEnabled = _model.CanWriteSelected;
            BottomSlider.IsEnabled = _model.CanWriteSelected;
            RapidTriggerToggle.IsEnabled = _model.CanWriteSelected;
            DksStartSlider.IsEnabled = _model.CanWriteSelected;
            DksEndSlider.IsEnabled = _model.CanWriteSelected;

            // 3. Values & Displays
            ActuationSlider.Value = draft?.ActuationMm ?? (actuation?.Raw / 10.0) ?? 0.1;
            PressSlider.Value = draft?.PressMm ?? (rt is { Source: "SessionApplied", PressKnown: true } ? rt.PressRaw / 10.0 : 0.1);
            ReleaseSlider.Value = draft?.ReleaseMm ?? (rt is { Source: "SessionApplied", ReleaseKnown: true } ? rt.ReleaseRaw / 10.0 : 0.1);
            TopSlider.Value = draft?.TopMm ?? (deadzone?.TopRaw / 10.0) ?? 0;
            BottomSlider.Value = draft?.BottomMm ?? (deadzone?.BottomRaw / 10.0) ?? 0;
            RapidTriggerToggle.IsOn = draft?.RapidTriggerEnabled ?? rt?.Enabled ?? false;
            DksStartSlider.Value = draft?.DksStartMm ?? (dks?.StartRaw / 10.0) ?? 0.1;
            DksEndSlider.Value = draft?.DksEndMm ?? (dks?.EndRaw / 10.0) ?? 0.1;

            ActuationDraftText.Text = Label("草稿", draft?.ActuationMm);
            ActuationValueDisplay.Text = $"{ActuationSlider.Value:F1} mm";
            PressDraftText.Text = Label("按下草稿", draft?.PressMm);
            PressValueDisplay.Text = $"{PressSlider.Value:F1} mm";
            ReleaseDraftText.Text = Label("抬起草稿", draft?.ReleaseMm);
            ReleaseValueDisplay.Text = $"{ReleaseSlider.Value:F1} mm";
            TopDraftText.Text = Label("顶部草稿", draft?.TopMm);
            TopValueDisplay.Text = $"{TopSlider.Value:F1} mm";
            BottomDraftText.Text = Label("底部草稿", draft?.BottomMm);
            BottomValueDisplay.Text = $"{BottomSlider.Value:F1} mm";
            DksStartValueDisplay.Text = $"{DksStartSlider.Value:F1} mm";
            DksEndValueDisplay.Text = $"{DksEndSlider.Value:F1} mm";

            DksDraftText.Text = draft == null ? "起止行程：未指定" :
                $"起点：{(draft.DksStartMm is double start ? $"{start:F1} mm" : "未指定")}；终点：{(draft.DksEndMm is double end ? $"{end:F1} mm" : "未指定")}";

            for (int i = 0; i < _dksControls.Count; ++i)
            {
                var controls = _dksControls[i];
                var slot = draft?.DksSlots[i];
                ushort? target = slot?.Target.Kind == "LogicalKey" ? slot.Target.LogicalId : null;
                controls.Target.SelectedIndex = target is ushort targetId && MagneticKeyLayout.IsValidDksActionTarget(targetId) ?
                    MagneticKeyLayout.Keys.Where(k => MagneticKeyLayout.IsValidDksActionTarget(k.LogicalId)).ToList().FindIndex(k => k.LogicalId == targetId) + 1 : 0;
                static int StateIndex(string? state) => state switch {
                    "Tap" => 1, "Release" => 2, "Hold" => 3, _ => 0 };
                controls.DownStart.SelectedIndex = StateIndex(slot?.DownStart);
                controls.DownEnd.SelectedIndex = StateIndex(slot?.DownEnd);
                controls.UpStart.SelectedIndex = StateIndex(slot?.UpStart);
                controls.UpEnd.SelectedIndex = StateIndex(slot?.UpEnd);
                foreach (var box in new[] { controls.Target, controls.DownStart, controls.DownEnd,
                    controls.UpStart, controls.UpEnd }) box.IsEnabled = _model.CanWriteSelected;
            }

            // 4. Provenance Labels
            ActuationAppliedText.Text = actuation != null ?
                $"{SourceLabel(actuation.Source)}：{actuation.Raw / 10.0:F1} mm" :
                status?.HostProfile.GlobalActuation is { Known: true } globalActuation ?
                $"已保存配置全局值：{globalActuation.Raw / 10.0:F1} mm；此键逐键值未知" :
                "此键逐键触发点：未知";
            RapidTriggerAppliedText.Text = rt == null ?
                status?.HostProfile.PerKeyRtListKnown == true ?
                "已保存配置逐键 RT 列表未包含此键；设备状态未知" : "逐键 RT：未知" :
                rt.Source == "SessionApplied" && (!rt.PressKnown || !rt.ReleaseKnown) ?
                "本次会话仅提交了部分灵敏度，完整快速触发设置尚未确认。" : rt.Source == "SessionApplied" ?
                $"本次会话：{(rt.Enabled ? "此键使用快速触发" : "关闭")}；按下 {rt.PressRaw / 10.0:F1} mm，抬起 {rt.ReleaseRaw / 10.0:F1} mm" :
                "已保存配置：此键使用快速触发（灵敏度未知）";
            DeadzoneAppliedText.Text = deadzone == null ? "此键逐键死区：未知（可参考下方已保存全局值）" :
                $"{SourceLabel(deadzone.Source)}：顶部 {deadzone.TopRaw / 10.0:F1} mm，底部 {deadzone.BottomRaw / 10.0:F1} mm";
            DksStatusText.Text = _model.DksStateText;
            AutomationProperties.SetName(DksStatusText, _model.DksStateText);
            DksAppliedText.Text = _model.DksState == MagneticDksState.SessionConfigured && dks != null ?
                $"本次会话：DKS {dks.StartRaw / 10.0:F1}–{dks.EndRaw / 10.0:F1} mm" :
                _model.DksStateText;

            // 5. Apply Affordance & Dirty Highlighting
            bool actuationDirty = _model.CanWriteSelected && draft?.ActuationMm != null;
            ActuationApplyButton.IsEnabled = actuationDirty;
            ActuationApplyButton.Style = actuationDirty ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;

            bool rtDirty = _model.CanWriteSelected &&
                (draft?.RapidTriggerEnabled == true ? draft.PressMm != null && draft.ReleaseMm != null :
                 draft?.RapidTriggerEnabled == false && _model.CanDisableRapidTrigger);
            RapidTriggerApplyButton.IsEnabled = rtDirty;
            RapidTriggerApplyButton.Style = rtDirty ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
            RapidTriggerDisableWarningText.Visibility = (draft?.RapidTriggerEnabled == false && !_model.CanDisableRapidTrigger) ?
                Visibility.Visible : Visibility.Collapsed;

            bool deadzoneDirty = _model.CanWriteSelected && draft?.TopMm != null && draft.BottomMm != null;
            DeadzoneApplyButton.IsEnabled = deadzoneDirty;
            DeadzoneApplyButton.Style = deadzoneDirty ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;

            bool dksDirty = _model.CanWriteSelected && draft?.DksDirty == true &&
                draft.DksStartMm is double dksStart && draft.DksEndMm is double dksEnd && dksStart <= dksEnd;
            DksApplyButton.IsEnabled = dksDirty;
            DksApplyButton.Style = dksDirty ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
            DksStandardButton.IsEnabled = _model.CanWriteSelected;
            DksConfigureButton.IsEnabled = !_model.Busy && !_model.Refreshing;
            }

            if (_model.IsMultiMode)
            {
                DksSection.Visibility = Visibility.Collapsed;
                RapidTriggerToggle.Visibility = Visibility.Collapsed;
                SeparateModeToggle.Visibility = Visibility.Collapsed;
                foreach (var slider in new[] { ActuationSlider, PressSlider, ReleaseSlider, TopSlider, BottomSlider })
                    slider.Visibility = Visibility.Collapsed;
                ActuationAppliedText.Text = _model.MultiActuationText;
                RapidTriggerAppliedText.Text = _model.MultiRapidTriggerText;
                DeadzoneAppliedText.Text = _model.MultiDeadzoneText;
                ActuationValueDisplay.Text = "—";
                PressValueDisplay.Text = "—";
                ReleaseValueDisplay.Text = "—";
                TopValueDisplay.Text = "—";
                BottomValueDisplay.Text = "—";
                var batch = _model.BatchDraft;
                ActuationDraftText.Text = Label("批量草稿", batch.ActuationMm);
                PressDraftText.Text = Label("按下批量草稿", batch.PressMm);
                ReleaseDraftText.Text = Label("抬起批量草稿", batch.ReleaseMm);
                TopDraftText.Text = Label("顶部批量草稿", batch.TopMm);
                BottomDraftText.Text = Label("底部批量草稿", batch.BottomMm);
                ShowBatchChoice(BatchActuationPicker, batch.ActuationMm, 1);
                ShowBatchChoice(BatchRtPressPicker, batch.PressMm, 1);
                ShowBatchChoice(BatchRtReleasePicker, batch.ReleaseMm, 1);
                ShowBatchChoice(BatchTopPicker, batch.TopMm, 0);
                ShowBatchChoice(BatchBottomPicker, batch.BottomMm, 0);
                BatchRtActionPicker.SelectedIndex = (int)batch.RtAction;
                bool enable = batch.RtAction == MagneticBatchRtAction.Enable;
                BatchRtPressPicker.Visibility = enable ? Visibility.Visible : Visibility.Collapsed;
                BatchRtReleasePicker.Visibility = enable ? Visibility.Visible : Visibility.Collapsed;
                RapidTriggerDisableWarningText.Visibility = batch.RtAction == MagneticBatchRtAction.Disable &&
                    !_model.CanBatchRapidTrigger ? Visibility.Visible : Visibility.Collapsed;
                RapidTriggerDisableWarningText.Text = "缺少可信来源的全局 RT 按下/抬起灵敏度，无法批量禁用。";
                ActuationApplyButton.IsEnabled = _model.CanBatchActuation;
                RapidTriggerApplyButton.IsEnabled = _model.CanBatchRapidTrigger;
                DeadzoneApplyButton.IsEnabled = _model.CanBatchDeadzone;
                foreach (var button in new[] { ActuationApplyButton, RapidTriggerApplyButton, DeadzoneApplyButton })
                {
                    button.Content = $"应用到 {_model.SelectedCount} 个按键";
                    button.Style = button.IsEnabled ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
                }
            }
            else
            {
                foreach (var slider in new[] { ActuationSlider, PressSlider, ReleaseSlider, TopSlider, BottomSlider })
                    slider.Visibility = Visibility.Visible;
                RapidTriggerDisableWarningText.Text = "当前无法确认已保存的全局灵敏度基线，暂无法安全禁用单键 RT。";
            }
            foreach (var picker in new[] { BatchActuationPicker, BatchRtActionPicker, BatchRtPressPicker,
                BatchRtReleasePicker, BatchTopPicker, BatchBottomPicker })
            {
                if (picker != BatchRtPressPicker && picker != BatchRtReleasePicker)
                    picker.Visibility = _model.IsMultiMode ? Visibility.Visible : Visibility.Collapsed;
                picker.IsEnabled = _model.CanWrite && _model.IsMultiMode;
            }
            if (!_model.IsMultiMode) {
                BatchRtPressPicker.Visibility = Visibility.Collapsed;
                BatchRtReleasePicker.Visibility = Visibility.Collapsed;
            }
            BatchDeadzoneNote.Visibility = _model.IsMultiMode ? Visibility.Visible : Visibility.Collapsed;

            // 6. Global Features & Provenance
            var host = status?.HostProfile;
            GlobalDeadzoneText.Text = host is { GlobalDeadzoneTop.Known: true, GlobalDeadzoneBottom.Known: true } ?
                $"已保存配置全局值：顶部 {host.GlobalDeadzoneTop.Raw / 10.0:F1} mm，底部 {host.GlobalDeadzoneBottom.Raw / 10.0:F1} mm" :
                "已保存配置全局值：未知；无法安全重置全部覆盖";
            ResetAllDeadzoneButton.IsEnabled = _model.CanResetAllDeadzone;

            var speedtap = status?.SpeedTap;
            SpeedTapStateText.Text = speedtap == null ? "键对与 Master 来源：未知" :
                $"总开关：{(speedtap.Master.Known ? $"{(speedtap.Master.Value ? "开" : "关")}（{SourceLabel(speedtap.Master.Source)}）" : "未知")}；" +
                $"本次会话提交 {speedtap.PairSubmissions.Count} 对；已保存配置 {(speedtap.SavedProfilePairsKnown ? $"{speedtap.SavedProfilePairs.Count} 对" : "未知")}；" +
                $"键对基线：{speedtap.PairKnowledge}";

            bool speedTapUnknownBaseline = status != null && status.SpeedTap?.SavedProfilePairsKnown == false;
            SpeedTapUnknownBaselineText.Visibility = speedTapUnknownBaseline ? Visibility.Visible : Visibility.Collapsed;

            SpeedTapKey1.IsEnabled = _model.CanWrite;
            SpeedTapKey2.IsEnabled = _model.CanWrite;
            SpeedTapMasterToggle.IsEnabled = _model.CanWrite;
            SpeedTapMasterToggle.IsOn = _model.SpeedTapMasterDraft ??
                (speedtap?.Master is { Known: true, Value: true });
            SpeedTapPairOnButton.IsEnabled = _model.CanEnableSpeedTapPair;
            SpeedTapPairOffButton.IsEnabled = _model.CanDisableSpeedTapPair;
            SpeedTapMasterApplyButton.IsEnabled = _model.CanWrite && _model.SpeedTapMasterDraft != null;
            SpeedTapMasterApplyButton.Style = _model.SpeedTapMasterDraft != null ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
            SpeedTapProfileResetButton.IsEnabled = _model.CanWrite;

            AnalogDescriptionText.Text = MagneticSettingsModel.HardwareAnalogDescription;
            AnalogBlockedReasonText.Text = MagneticSettingsModel.HardwareAnalogBlockedReason;
            AnalogStateText.Text = status?.StaticAnalogEffect is { Known: true } analog ?
                $"{SourceLabel(analog.Source)}：{(analog.Value ? "开启" : "关闭")}" : "当前来源：未知";
            AnalogToggle.IsEnabled = _model.CanApplyStaticAnalog && _model.CanWrite;
            AnalogToggle.IsOn = _model.StaticAnalogDraft ??
                (status?.StaticAnalogEffect is { Known: true, Value: true });
            AnalogApplyButton.IsEnabled = _model.CanApplyStaticAnalog && _model.CanWrite && _model.StaticAnalogDraft != null;
            AnalogApplyButton.Style = AnalogApplyButton.IsEnabled ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;

            SelectedKeyLiveDepthPanel.Visibility = (_hallActive && !_model.IsGlobalMode && !_model.IsMultiMode && selected != null)
                ? Visibility.Visible : Visibility.Collapsed;
            HallVerifyTargetText.Text = selected != null ? $"当前选中：{selected.FullName}" : "当前选中：未选择";
            HallVerifyStartButton.IsEnabled = !_verifyRunning && selected != null && _model.CanWriteSelected;
            HallVerifyStopButton.IsEnabled = _verifyRunning;
            UpdateVerticalHallVisualizer();

            // 7. Compact Health Strip & Escalation InfoBar
            RefreshButton.IsEnabled = !_model.Busy && !_model.Refreshing;
            BusyRing.IsActive = _model.Busy;
            BusyRing.Visibility = _model.Busy ? Visibility.Visible : Visibility.Collapsed;

            if (_model.Busy)
            {
                HealthStatusText.Text = "正在提交硬件事务…";
                HealthStatusDot.Fill = (Brush)Application.Current.Resources["SystemFillColorAttentionBrush"];
                HealthBar.IsOpen = false;
                _quarantineRecoveryButton.Visibility = Visibility.Collapsed;
            }
            else if (_model.Quarantined)
            {
                HealthStatusText.Text = "安全隔离 · 写入已停止";
                HealthStatusDot.Fill = (Brush)Application.Current.Resources["SystemFillColorCriticalBrush"];
                HealthBar.IsOpen = true;
                HealthBar.Severity = InfoBarSeverity.Error;
                HealthBar.Title = "跨重启安全隔离中";
                HealthBar.Message = "检测到上次磁轴事务未确认完成。为避免继续写入造成设备状态进一步不确定，磁轴写入已停止。请在外部完成重新同步后执行恢复。";
                _quarantineRecoveryButton.Visibility = (_model.Status?.PersistentSafetyQuarantine == true) ?
                    Visibility.Visible : Visibility.Collapsed;
            }
            else if (status != null && !status.Available)
            {
                HealthStatusText.Text = "设备未连接";
                HealthStatusDot.Fill = (Brush)Application.Current.Resources["SystemFillColorCautionBrush"];
                HealthBar.IsOpen = true;
                HealthBar.Severity = InfoBarSeverity.Warning;
                HealthBar.Title = "设备未连接";
                HealthBar.Message = "原生 HID 设备当前不可用，或核心正在模拟/使用其他后端。";
                _quarantineRecoveryButton.Visibility = Visibility.Collapsed;
            }
            else if (status?.Health == "IndeterminateStagedState")
            {
                HealthStatusText.Text = "状态不确定";
                HealthStatusDot.Fill = (Brush)Application.Current.Resources["SystemFillColorCriticalBrush"];
                HealthBar.IsOpen = true;
                HealthBar.Severity = InfoBarSeverity.Error;
                HealthBar.Title = "配置状态不确定";
                HealthBar.Message = "磁轴配置状态不确定，本次会话已停止写入；需要外部重新同步。";
                _quarantineRecoveryButton.Visibility = Visibility.Collapsed;
            }
            else if (status is { Succeeded: true, Available: true, Health: "Clean" })
            {
                HealthStatusText.Text = "设备已连接 · 状态正常";
                HealthStatusDot.Fill = (Brush)Application.Current.Resources["SystemFillColorSuccessBrush"];
                HealthBar.IsOpen = false;
                _quarantineRecoveryButton.Visibility = Visibility.Collapsed;
            }
            else
            {
                HealthStatusText.Text = "运行状态：就绪";
                HealthStatusDot.Fill = (Brush)Application.Current.Resources["SystemFillColorNeutralBrush"];
                HealthBar.IsOpen = false;
                _quarantineRecoveryButton.Visibility = Visibility.Collapsed;
            }
        }
        finally { _rendering = false; }
    }

    private static string Label(string name, double? value) =>
        value is double mm ? $"{name}：{mm:F1} mm" : $"{name}：未指定";
    private static string SourceLabel(string source) => source switch {
        "SessionApplied" => "本次会话", "DeviceBaseline" => "手动全局设置",
        "HostProfile" => "已保存配置", _ => "未知" };

    private void GlobalSelectButton_Click(object sender, RoutedEventArgs e)
    {
        if (_rendering) return;
        _model.SelectGlobal();
        Render();
    }

    private void MultiSelectButton_Click(object sender, RoutedEventArgs e)
    {
        if (_rendering) return;
        if (_model.EnterMulti()) Render();
    }

    private void ClearSelectionButton_Click(object sender, RoutedEventArgs e)
    {
        if (_rendering) return;
        if (_model.ClearMulti()) Render();
    }

    private void DksConfigureButton_Click(object sender, RoutedEventArgs e)
    {
        if (_rendering) return;
        if (_model.ConfigureDksEditor())
        {
            Render();
        }
    }

    private void MagneticSectionBar_SelectionChanged(SelectorBar sender, SelectorBarSelectionChangedEventArgs args)
    {
        if (SectionBasicPanel == null) return;
        string tag = (sender.SelectedItem as SelectorBarItem)?.Tag as string ?? "basic";
        SectionBasicPanel.Visibility = tag == "basic" ? Visibility.Visible : Visibility.Collapsed;
        SectionHallPanel.Visibility = tag == "hall" ? Visibility.Visible : Visibility.Collapsed;
        SectionAdvancedPanel.Visibility = tag == "advanced" ? Visibility.Visible : Visibility.Collapsed;
        SectionUtilityPanel.Visibility = tag == "utility" ? Visibility.Visible : Visibility.Collapsed;
    }

    private void ActuationSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering) return;
        if (_model.IsGlobalMode)
        {
            _model.EditGlobalActuation(Math.Round(e.NewValue, 1));
            Render();
            return;
        }
        if (_model.SelectedKey == null) return;
        _model.EditActuation(Math.Round(e.NewValue, 1)); Render();
    }
    private void RapidTriggerToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_rendering || _model.SelectedKey == null) return;
        _model.EditRapidTrigger(RapidTriggerToggle.IsOn, null, null); Render();
    }
    private void SeparateModeToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_rendering || !_model.IsGlobalMode) return;
        _model.EditGlobalRapidTrigger(null, null, SeparateModeToggle.IsOn);
        Render();
    }
    private void PressSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering) return;
        if (_model.IsGlobalMode)
        {
            double p = Math.Round(e.NewValue, 1);
            var baselineRt = _model.ManualGlobalRapidTrigger;
            bool separate = _model.GlobalDraft.SeparateMode ?? baselineRt.SeparateMode ??
                (baselineRt.Known && baselineRt.PressRaw != baselineRt.ReleaseRaw);
            _model.EditGlobalRapidTrigger(p, separate ? null : p, null);
            Render();
            return;
        }
        if (_model.SelectedKey == null) return;
        _model.EditRapidTrigger(RapidTriggerToggle.IsOn, Math.Round(e.NewValue, 1), null); Render();
    }
    private void ReleaseSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering) return;
        if (_model.IsGlobalMode)
        {
            double r = Math.Round(e.NewValue, 1);
            var baselineRt = _model.ManualGlobalRapidTrigger;
            bool separate = _model.GlobalDraft.SeparateMode ?? baselineRt.SeparateMode ??
                (baselineRt.Known && baselineRt.PressRaw != baselineRt.ReleaseRaw);
            _model.EditGlobalRapidTrigger(separate ? null : r, r, null);
            Render();
            return;
        }
        if (_model.SelectedKey == null) return;
        _model.EditRapidTrigger(RapidTriggerToggle.IsOn, null, Math.Round(e.NewValue, 1)); Render();
    }
    private void TopSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering) return;
        if (_model.IsGlobalMode)
        {
            _model.EditGlobalDeadzone(Math.Round(e.NewValue, 1), null);
            Render();
            return;
        }
        if (_model.SelectedKey == null) return;
        _model.EditDeadzone(Math.Round(e.NewValue, 1), null); Render();
    }
    private void BottomSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering) return;
        if (_model.IsGlobalMode)
        {
            _model.EditGlobalDeadzone(null, Math.Round(e.NewValue, 1));
            Render();
            return;
        }
        if (_model.SelectedKey == null) return;
        _model.EditDeadzone(null, Math.Round(e.NewValue, 1)); Render();
    }
    private async void RefreshButton_Click(object sender, RoutedEventArgs e)
    {
        RefreshButton.IsEnabled = false;
        await _model.RefreshAsync(); Render();
    }
    private async Task ApplyAsync(Func<Task<bool>> action)
    {
        var pending = action();
        Render(); // Busy is set synchronously before the first await.
        await pending;
        Render();
    }
    private async void ActuationApplyButton_Click(object sender, RoutedEventArgs e)
    {
        if (_model.IsMultiMode) { await ApplyAsync(_model.ApplyBatchActuationAsync); return; }
        if (_model.IsGlobalMode) await ApplyAsync(_model.ApplyGlobalActuationAsync);
        else await ApplyAsync(_model.ApplyActuationAsync);
    }
    private async void RapidTriggerApplyButton_Click(object sender, RoutedEventArgs e)
    {
        if (_model.IsMultiMode)
        {
            await ApplyAsync(() => _model.ApplyBatchRapidTriggerAsync());
            var conflict = _model.LastBatchResult;
            if (_model.BatchDraft.RtAction != MagneticBatchRtAction.Enable || conflict == null ||
                conflict.AppliedCount != 0 || conflict.FailedCount != 0 ||
                conflict.ConfiguredDksKeys.Count + conflict.UnknownDksKeys.Count == 0) return;
            var copy = $"所选 {_model.SelectedCount} 个按键中：\n" +
                $"{conflict.ConfiguredDksKeys.Count} 个已在本次会话配置 DKS，" +
                $"{conflict.UnknownDksKeys.Count} 个 DKS 状态未知。\n\n" +
                "快速触发与自定义 DKS 不应同时使用。继续后，Aura 会先将已配置或状态未知的按键恢复为标准按键行为，再逐个应用快速触发。";
            if (await ConfirmAsync("批量快速触发与 DKS", copy, "继续", "取消"))
                await ApplyAsync(() => _model.ApplyBatchRapidTriggerAsync(resolveDks: true));
            return;
        }
        if (_model.IsGlobalMode)
        {
            await ApplyAsync(_model.ApplyGlobalRapidTriggerAsync);
            return;
        }
        var resolve = _model.Draft?.RapidTriggerEnabled == true && _model.RtConflictPossible;
        if (resolve && !await ConfirmAsync("DKS 与快速触发",
            _model.RtDksConflictCopy + "\n\n确认继续吗？",
            "继续", "取消")) return;
        await ApplyAsync(() => _model.ApplyRapidTriggerAsync(resolve));
    }
    private async void DeadzoneApplyButton_Click(object sender, RoutedEventArgs e)
    {
        if (_model.IsMultiMode) { await ApplyAsync(_model.ApplyBatchDeadzoneAsync); return; }
        if (_model.IsGlobalMode) await ApplyAsync(_model.ApplyGlobalDeadzoneAsync);
        else await ApplyAsync(_model.ApplyDeadzoneAsync);
    }

    private static double? PickerValue(ComboBox picker) =>
        picker.SelectedItem is ComboBoxItem { Tag: double value } ? value : null;

    private void BatchActuationPicker_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering || PickerValue(BatchActuationPicker) is not double value) return;
        _model.EditBatchActuation(value); Render();
    }
    private void BatchRtActionPicker_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering || BatchRtActionPicker.SelectedItem is not ComboBoxItem item) return;
        if (Enum.TryParse<MagneticBatchRtAction>(item.Tag?.ToString(), out var action))
            { _model.EditBatchRapidTrigger(action); Render(); }
    }
    private void BatchRtPressPicker_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering || PickerValue(BatchRtPressPicker) is not double value) return;
        _model.EditBatchRapidTrigger(MagneticBatchRtAction.Enable, press: value); Render();
    }
    private void BatchRtReleasePicker_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering || PickerValue(BatchRtReleasePicker) is not double value) return;
        _model.EditBatchRapidTrigger(MagneticBatchRtAction.Enable, release: value); Render();
    }
    private void BatchTopPicker_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering || PickerValue(BatchTopPicker) is not double value) return;
        _model.EditBatchDeadzone(value, null); Render();
    }
    private void BatchBottomPicker_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering || PickerValue(BatchBottomPicker) is not double value) return;
        _model.EditBatchDeadzone(null, value); Render();
    }

    private void DksStartSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering || _model.SelectedKey == null) return;
        _model.EditDksThresholds(Math.Round(e.NewValue, 1), null); Render();
    }
    private void DksEndSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering || _model.SelectedKey == null) return;
        _model.EditDksThresholds(null, Math.Round(e.NewValue, 1)); Render();
    }
    private void DksSlot_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering || _model.SelectedKey == null) return;
        for (int i = 0; i < _dksControls.Count; ++i)
        {
            var row = _dksControls[i];
            if (row.Target.SelectedItem is not ComboBoxItem target ||
                row.DownStart.SelectedItem is not ComboBoxItem downStart ||
                row.DownEnd.SelectedItem is not ComboBoxItem downEnd ||
                row.UpStart.SelectedItem is not ComboBoxItem upStart ||
                row.UpEnd.SelectedItem is not ComboBoxItem upEnd) continue;
            _model.EditDksSlot(i, target.Tag as ushort?,
                (string)downStart.Tag, (string)downEnd.Tag, (string)upStart.Tag, (string)upEnd.Tag);
        }
        Render();
    }
    private async void DksApplyButton_Click(object sender, RoutedEventArgs e)
    {
        var resolve = _model.DksConflictPossible;
        if (resolve && !await ConfirmAsync("DKS 与快速触发",
            "启用 DKS 将关闭此按键的逐键快速触发，再应用 DKS 四段动作。两次操作分别提交。\n\n确认继续吗？",
            "继续", "取消")) return;
        await ApplyAsync(() => _model.ApplyDksAsync(resolve));
    }
    private async void DksStandardButton_Click(object sender, RoutedEventArgs e) =>
        await ApplyAsync(_model.RestoreDksStandardAsync);

    private async void ResetAllDeadzoneButton_Click(object sender, RoutedEventArgs e)
    {
        if (!await ConfirmAsync("清除所有逐键死区设置",
            "此操作将清除键盘上所有按键的逐键死区覆盖，影响全部 68 颗按键。所有按键将重新继承当前全局顶部/底部死区设置。\n\n确认要清除全部逐键死区吗？",
            "清除全部", "取消")) return;
        await ApplyAsync(_model.ResetAllDeadzoneAsync);
    }
    private void SpeedTapPair_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering) return;
        _model.EditSpeedTapPair((SpeedTapKey1.SelectedItem as ComboBoxItem)?.Tag as ushort?,
                                (SpeedTapKey2.SelectedItem as ComboBoxItem)?.Tag as ushort?);
        Render();
    }
    private async void SpeedTapPairOnButton_Click(object sender, RoutedEventArgs e) =>
        await ApplyAsync(() => _model.ApplySpeedTapPairAsync(true));
    private async void SpeedTapPairOffButton_Click(object sender, RoutedEventArgs e) =>
        await ApplyAsync(() => _model.ApplySpeedTapPairAsync(false));
    private void SpeedTapMasterToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_rendering) return;
        _model.EditSpeedTapMaster(SpeedTapMasterToggle.IsOn); Render();
    }
    private async void SpeedTapMasterApplyButton_Click(object sender, RoutedEventArgs e) =>
        await ApplyAsync(_model.ApplySpeedTapMasterAsync);
    private async void SpeedTapProfileResetButton_Click(object sender, RoutedEventArgs e)
    {
        if (!await ConfirmAsync("恢复 SpeedTap 配置基线",
            "这会把运行时键对恢复到活动配置基线；不会清空已保存的键对，也不会更改 Master。\n\n确认继续吗？",
            "继续", "取消")) return;
        await ApplyAsync(_model.ResetSpeedTapToProfileAsync);
    }
    private void AnalogToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_rendering) return;
        _model.EditStaticAnalog(AnalogToggle.IsOn); Render();
    }
    private async void AnalogApplyButton_Click(object sender, RoutedEventArgs e) =>
        await ApplyAsync(_model.ApplyStaticAnalogAsync);

    private async void QuarantineRecoveryButton_Click(object sender, RoutedEventArgs e)
    {
        const string title = "外部重新同步确认";
        const string message =
            "在继续之前，请确认以下安全事项：\n\n" +
            "• AceHFXAura 当前并不知道键盘的实际磁轴硬件配置。\n" +
            "• 此操作不会向键盘恢复或更改任何设置（0 次硬件写入）。\n" +
            "• 您必须先通过外部方式（例如华硕官方 Armoury Crate 软件）将键盘恢复或验证至已知良好状态。\n" +
            "• 请重新连接键盘。后台会重新打开并验证连接；仅拔插或重启 Aura 不会清除安全隔离。\n" +
            "• 继续操作仅会清除 AceHFXAura 的持久安全隔离状态，并清空本次会话已应用记录（SessionApplied）。\n\n" +
            "确认已在外部完成重新同步并清除安全隔离？";

        if (!await ConfirmAsync(title, message, "确认已完成重新同步", "取消"))
            return;

        await ApplyAsync(_model.AcknowledgeExternalResynchronizationAsync);
    }

    private async Task<bool> ConfirmAsync(string title, string message, string primaryText = "继续", string closeText = "取消")
    {
        var dialog = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = title,
            Content = message,
            PrimaryButtonText = primaryText,
            CloseButtonText = closeText,
            DefaultButton = ContentDialogButton.Close
        };
        return await dialog.ShowAsync() == ContentDialogResult.Primary;
    }

    private async void HallTelemetryToggle_Click(object sender, RoutedEventArgs e)
    {
        await ToggleHallTelemetryAsync(HallTelemetryToggle.IsChecked == true);
    }

    private async void HallVizToggleButton_Click(object sender, RoutedEventArgs e)
    {
        await ToggleHallTelemetryAsync(!_hallActive);
    }

    private async Task ToggleHallTelemetryAsync(bool wantActive)
    {
        HallTelemetryToggle.IsEnabled = false;
        HallVizToggleButton.IsEnabled = false;
        try
        {
            var snap = wantActive
                ? await _hallClient.StartAsync(HallUnlockFaCheckBox.IsChecked == true)
                : await _hallClient.StopAsync();
            _hallActive = snap.Active;
            HallTelemetryToggle.IsChecked = _hallActive;
            if (_hallActive)
            {
                HallTelemetryStatsBorder.Visibility = Visibility.Visible;
                HallTelemetryStatsText.Text = snap.SharedMemoryOk ? "已启动 · Wooting 共享内存就绪" : "已启动";
                _hallTimer?.Start();
            }
            else
            {
                if (!_verifyRunning) _hallTimer?.Stop();
                ClearKeyDepthBars();
                HallTelemetryStatsBorder.Visibility = string.IsNullOrWhiteSpace(snap.LastError) ? Visibility.Collapsed : Visibility.Visible;
                HallTelemetryStatsText.Text = snap.LastError;
            }
            Render();
        }
        finally
        {
            HallTelemetryToggle.IsEnabled = true;
            HallVizToggleButton.IsEnabled = true;
        }
    }

    private void ClearKeyDepthBars()
    {
        foreach (var bar in _keyDepthBars.Values) bar.Height = 0;
        _liveSelectedDepthMm = 0.0;
        _liveSelectedRaw = 0;
        _liveSelectedDigitalDown = false;
        SelectedKeyDepthBar.Value = 0;
        SelectedKeyDepthText.Text = "0.00 mm";
        UpdateVerticalHallVisualizer();
    }

    private void UpdateVerticalHallVisualizer()
    {
        if (HallVizTrackCanvas == null) return;
        const double trackH = 238.0;
        const double pxPerMm = trackH / 4.0;

        var selected = _model.SelectedKey;
        HallVizKeyBadgeText.Text = selected != null ? selected.Label :
            _model.IsGlobalMode ? "全局" : _model.IsMultiMode ? $"{_model.SelectedCount} 键" : "—";
        HallVizToggleButton.Content = _hallActive ? "停止实时行程" : "开启实时行程";

        double actMm = Math.Clamp(ActuationSlider.Value, 0.1, 4.0);
        double actY = Math.Clamp(actMm * pxPerMm - 1.0, 0.0, trackH - 2.0);
        Canvas.SetTop(HallVizActuationLine, actY);
        Canvas.SetTop(HallVizActuationTag, Math.Clamp(actMm * pxPerMm - 11.0, 0.0, trackH - 22.0));
        HallVizActuationTagText.Text = $"{actMm:F1} mm";

        double topMm = Math.Clamp(TopSlider.Value, 0.0, 0.5);
        double topH = Math.Clamp(topMm * pxPerMm, 0.0, trackH);
        HallVizTopDzBand.Height = topH;
        HallVizTopDzBand.Visibility = topMm > 0.01 ? Visibility.Visible : Visibility.Collapsed;

        double botMm = Math.Clamp(BottomSlider.Value, 0.0, 0.5);
        double botH = Math.Clamp(botMm * pxPerMm, 0.0, trackH);
        Canvas.SetTop(HallVizBottomDzBand, trackH - botH);
        HallVizBottomDzBand.Height = botH;
        HallVizBottomDzBand.Visibility = botMm > 0.01 ? Visibility.Visible : Visibility.Collapsed;

        bool showRt = !_model.IsGlobalMode && !_model.IsMultiMode && RapidTriggerToggle.IsOn;
        if (showRt)
        {
            double pressMm = Math.Clamp(PressSlider.Value, 0.1, 2.5);
            double releaseMm = Math.Clamp(ReleaseSlider.Value, 0.1, 2.5);
            double anchor = (_hallActive && _liveSelectedDepthMm > 0.05) ? _liveSelectedDepthMm : actMm;
            double rtTopMm = Math.Max(0.0, anchor - releaseMm);
            double rtBotMm = Math.Min(4.0, anchor + pressMm);
            Canvas.SetTop(HallVizRtBand, rtTopMm * pxPerMm);
            HallVizRtBand.Height = Math.Max(2.0, (rtBotMm - rtTopMm) * pxPerMm);
            HallVizRtBand.Visibility = Visibility.Visible;
        }
        else
        {
            HallVizRtBand.Visibility = Visibility.Collapsed;
        }

        if (_hallActive && !_model.IsGlobalMode && !_model.IsMultiMode && selected != null)
        {
            double d = Math.Clamp(_liveSelectedDepthMm, 0.0, 4.0);
            bool triggered = _liveSelectedDigitalDown || d >= actMm;
            HallVizDepthFill.Height = d * pxPerMm;
            HallVizDepthFill.Background = (Brush)Application.Current.Resources[
                triggered ? "SystemFillColorSuccessBrush" : "AccentFillColorSecondaryBrush"];
            HallVizDepthTip.Visibility = d > 0.02 ? Visibility.Visible : Visibility.Collapsed;
            Canvas.SetTop(HallVizDepthTip, Math.Clamp(d * pxPerMm - 1.5, 0.0, trackH - 3.0));
            HallVizLiveTag.Visibility = d > 0.05 ? Visibility.Visible : Visibility.Collapsed;
            Canvas.SetTop(HallVizLiveTag, Math.Clamp(d * pxPerMm - 10.0, 0.0, trackH - 20.0));
            HallVizLiveTagText.Text = $"{d:F2}";
            HallVizDepthReadout.Text = $"{d:F2} mm";
            HallVizRawReadout.Text = _liveSelectedRaw > 0 ? $"Raw {_liveSelectedRaw} · 触发点 {actMm:F1} mm" : "正在等待霍尔帧…";
            HallVizStateText.Text = triggered ? "已触发" : "未触发";
            HallVizStateText.Foreground = (Brush)Application.Current.Resources[
                triggered ? "SystemFillColorSuccessBrush" : "TextFillColorSecondaryBrush"];
        }
        else
        {
            HallVizDepthFill.Height = 0;
            HallVizDepthTip.Visibility = Visibility.Collapsed;
            HallVizLiveTag.Visibility = Visibility.Collapsed;
            HallVizDepthReadout.Text = _hallActive ? "请选择单键" : "0.00 mm";
            HallVizStateText.Text = _hallActive ? "全键监听" : "未开启";
            HallVizStateText.Foreground = (Brush)Application.Current.Resources["TextFillColorSecondaryBrush"];
            HallVizRawReadout.Text = _hallActive
                ? "点击任意按键可查看实时按压深度"
                : "拖动滑杆可实时预览触发线、死区与 RT 区间";
        }
    }

    private void HallVerifyModeCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (HallVerifyParamsBox == null) return;
        string mode = (HallVerifyModeCombo.SelectedItem as ComboBoxItem)?.Tag as string ?? "actuation";
        switch (mode)
        {
            case "rt":
                HallVerifyParamsBox.Header = "RT 灵敏度列表 (mm，逗号分隔)";
                HallVerifyParamsBox.Text = "0.3";
                break;
            case "deadzone":
                HallVerifyParamsBox.Header = "顶部与底部死区 (top, bottom mm)";
                HallVerifyParamsBox.Text = "0.4, 0.4";
                break;
            default:
                HallVerifyParamsBox.Header = "目标触发点列表 (mm，逗号分隔)";
                HallVerifyParamsBox.Text = "0.8, 2.0, 3.2";
                break;
        }
    }

    private async void HallVerifyStartButton_Click(object sender, RoutedEventArgs e)
    {
        if (_model.SelectedKey is not { } selectedKey)
        {
            HallVerifyGuideBar.Severity = InfoBarSeverity.Warning;
            HallVerifyGuideBar.Title = "未选择按键";
            HallVerifyGuideBar.Message = "请先在左侧键盘中单击选中一个实体按键，再开始闭环校验。";
            HallVerifyGuideBar.IsOpen = true;
            return;
        }

        string mode = (HallVerifyModeCombo.SelectedItem as ComboBoxItem)?.Tag as string ?? "actuation";
        var values = HallVerifyParamsBox.Text
            .Split([',', '，', ' '], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(s => double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out var v) ? (double?)v : null)
            .Where(v => v.HasValue)
            .Select(v => Math.Round(v!.Value, 1))
            .ToList();

        var request = new MagneticHallVerifyStartRequest
        {
            LogicalId = selectedKey.LogicalId,
            Mode = mode,
            RestoreActuationMm = 2.0,
            UnlockFa = HallUnlockFaCheckBox.IsChecked == true
        };

        if (mode == "actuation")
        {
            if (values.Count == 0 || values.Any(v => v < 0.1 || v > 4.0))
            {
                ShowVerifyInputError("触发点参数需在 0.1–4.0 mm 之间（如 0.8, 2.0, 3.2）。");
                return;
            }
            request.ActuationPoints = values;
        }
        else if (mode == "rt")
        {
            if (values.Count == 0 || values.Any(v => v < 0.1 || v > 2.5))
            {
                ShowVerifyInputError("RT 灵敏度参数需在 0.1–2.5 mm 之间（如 0.3）。");
                return;
            }
            request.RtPoints = values;
        }
        else
        {
            double top = values.Count >= 1 ? values[0] : 0.4;
            double bot = values.Count >= 2 ? values[1] : top;
            if (top < 0.0 || top > 0.5 || bot < 0.0 || bot > 0.5)
            {
                ShowVerifyInputError("死区参数需在 0.0–0.5 mm 之间（如 0.4, 0.4）。");
                return;
            }
            request.DzTopMm = top;
            request.DzBottomMm = bot;
            request.DzRtMm = 0.2;
        }

        HallVerifyResultsPanel.Children.Clear();
        HallVerifyLivePanel.Visibility = Visibility.Visible;
        _verifyRunning = true;
        _hallActive = true;
        HallTelemetryToggle.IsChecked = true;
        Render();

        var st = await _hallClient.StartVerifyAsync(request);
        UpdateHallVerifyVisuals(st);
        if (st.State is "Failed")
        {
            _verifyRunning = false;
            Render();
            return;
        }
        _hallTimer?.Start();
    }

    private void ShowVerifyInputError(string message)
    {
        HallVerifyGuideBar.Severity = InfoBarSeverity.Error;
        HallVerifyGuideBar.Title = "参数格式无效";
        HallVerifyGuideBar.Message = message;
        HallVerifyGuideBar.IsOpen = true;
    }

    private async void HallVerifyStopButton_Click(object sender, RoutedEventArgs e)
    {
        HallVerifyStopButton.IsEnabled = false;
        var st = await _hallClient.StopVerifyAsync();
        _verifyRunning = false;
        UpdateHallVerifyVisuals(st);
        Render();
    }

    private async void HallTimer_Tick(Microsoft.UI.Dispatching.DispatcherQueueTimer sender, object args)
    {
        if (_hallPolling) return;
        _hallPolling = true;
        try
        {
            if (_hallActive)
            {
                var snap = await _hallClient.GetSnapshotAsync();
                UpdateHallDepthVisuals(snap);
            }
            if (_verifyRunning)
            {
                var st = await _hallClient.GetVerifyStatusAsync();
                UpdateHallVerifyVisuals(st);
                if (st.State is "Completed" or "Failed" or "Cancelled")
                {
                    _verifyRunning = false;
                    if (HallTelemetryToggle.IsChecked != true) _hallTimer?.Stop();
                    Render();
                }
            }
        }
        finally
        {
            _hallPolling = false;
        }
    }

    private void UpdateHallDepthVisuals(MagneticHallSnapshot snap)
    {
        if (!snap.Active)
        {
            _hallActive = false;
            HallTelemetryToggle.IsChecked = false;
            HallTelemetryStatsBorder.Visibility = Visibility.Collapsed;
            ClearKeyDepthBars();
            if (!_verifyRunning) _hallTimer?.Stop();
            return;
        }

        HallTelemetryStatsBorder.Visibility = Visibility.Visible;
        HallTelemetryStatsText.Text =
            $"{snap.PacketRateHz:F0} Hz · Wooting {(snap.SharedMemoryOk ? "共享内存就绪" : "未就绪")}";

        double maxBarH = Math.Max(20.0, _lastKeyboardKeyHeight - 2.0);
        foreach (var k in snap.Keys)
        {
            if (_keyDepthBars.TryGetValue(k.LogicalId, out var bar))
            {
                bar.Height = maxBarH * Math.Clamp(k.Normalized, 0.0, 1.0);
            }
            if (_model.SelectedLogicalId == k.LogicalId)
            {
                _liveSelectedDepthMm = k.DepthMm;
                _liveSelectedRaw = k.Raw;
                _liveSelectedDigitalDown = k.DigitalDown;
                if (SelectedKeyLiveDepthPanel.Visibility == Visibility.Visible)
                {
                    SelectedKeyDepthBar.Value = Math.Clamp(k.DepthMm, 0.0, 4.0);
                    SelectedKeyDepthText.Text = $"{k.DepthMm:F2} mm · Raw {k.Raw}";
                }
            }
        }
        UpdateVerticalHallVisualizer();
    }

    private void UpdateHallVerifyVisuals(MagneticHallVerifyStatus st)
    {
        HallVerifyLivePanel.Visibility = Visibility.Visible;
        HallVerifyGuideBar.IsOpen = true;
        HallVerifyGuideBar.Title = string.IsNullOrEmpty(st.KeyName)
            ? "闭环校验"
            : $"{st.KeyName} · {(st.StepTotal > 0 ? $"步骤 {st.StepIndex}/{st.StepTotal}" : st.State)}";
        HallVerifyGuideBar.Message = !string.IsNullOrEmpty(st.Error) ? st.Error : st.Instruction;
        HallVerifyGuideBar.Severity = st.State switch
        {
            "Completed" => InfoBarSeverity.Success,
            "Failed" => InfoBarSeverity.Error,
            "Cancelled" => InfoBarSeverity.Warning,
            _ => InfoBarSeverity.Informational
        };

        HallVerifyDepthBar.Value = Math.Clamp(st.LiveDepthMm, 0.0, 4.0);
        string bitInfo = string.IsNullOrEmpty(st.BoundBit) ? "" : $" · 触发位 {st.BoundBit}";
        HallVerifyLiveText.Text = $"当前深度 {st.LiveDepthMm:F2} mm · Raw {st.LiveRaw} · 数字触发：{(st.DigitalDown ? "已触发" : "未触发")}{bitInfo}";

        if (HallVerifyResultsPanel.Children.Count != st.Results.Count)
        {
            HallVerifyResultsPanel.Children.Clear();
            foreach (var item in st.Results)
            {
                var badge = item.Passed ? "PASS" : "CHECK";
                HallVerifyResultsPanel.Children.Add(new TextBlock
                {
                    Text = $"[{badge}] {item.Label}：{item.Detail}",
                    Style = (Style)Application.Current.Resources["CaptionTextBlockStyle"],
                    Foreground = (Brush)Application.Current.Resources[item.Passed ? "SystemFillColorSuccessBrush" : "SystemFillColorCautionBrush"],
                    TextWrapping = TextWrapping.Wrap
                });
            }
        }
    }

    private void OpenPluginsPageButton_Click(object sender, RoutedEventArgs e)
    {
        MainWindow.CurrentInstance?.NavigateTo(typeof(PluginsPage));
    }
}
