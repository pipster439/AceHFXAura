using Aura_WinUI.Common;
using Aura_WinUI.Services;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Aura_WinUI.Pages;

public sealed partial class MagneticSwitchPage : Page
{
    private MagneticSettingsModel _model;
    private readonly Dictionary<ushort, Button> _keyButtons = [];
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

    public MagneticSwitchPage() : this(new MagneticSettingsModel(new MagneticControlClient()))
    {
    }

    public MagneticSwitchPage(MagneticSettingsModel model)
    {
        _model = model;
        InitializeComponent();
        AutomationProperties.SetAutomationId(_quarantineRecoveryButton, "MagneticQuarantineRecoveryButton");
        _quarantineRecoveryButton.Click += QuarantineRecoveryButton_Click;
        HealthBar.ActionButton = _quarantineRecoveryButton;
        NavigationCacheMode = Microsoft.UI.Xaml.Navigation.NavigationCacheMode.Required;
        BuildKeyboard();
        BuildDksEditor();
        foreach (var key in MagneticKeyLayout.Keys)
        {
            SpeedTapKey1.Items.Add(new ComboBoxItem { Content = key.FullName, Tag = key.LogicalId });
            SpeedTapKey2.Items.Add(new ComboBoxItem { Content = key.FullName, Tag = key.LogicalId });
        }
        PageLayout.Attach(this, PageScroll, PageContent, width =>
        {
            bool wide = width >= 1180;
            ContentColumns.ColumnDefinitions[0].Width = wide ? GridLength.Auto : new GridLength(1, GridUnitType.Star);
            ContentColumns.ColumnDefinitions[1].Width = wide ? new GridLength(460) : new GridLength(0);
            Grid.SetColumn(DetailsPanel, wide ? 1 : 0);
            Grid.SetRow(DetailsPanel, wide ? 0 : 1);

            // Responsive positioning for GlobalFeaturesPanel:
            // Wide: Row 1, ColumnSpan 2 (spans beneath Keyboard and Details)
            // Stacked: Row 2, ColumnSpan 1
            Grid.SetRow(GlobalFeaturesPanel, wide ? 1 : 2);
            Grid.SetColumnSpan(GlobalFeaturesPanel, wide ? 2 : 1);

            // Responsive DKS slots layout:
            // In wide mode (2-column layout), DetailsPanel is 460px -> 1 column of slots avoids horizontal clipping.
            // In stacked mode with width >= 800 (e.g. 1060px desktop), DetailsPanel is full-width -> 2x2 grid is balanced.
            int dksCols = (!wide && width >= 800) ? 2 : 1;
            PageLayout.Columns(DksSlotsHost, dksCols);

            double unit = wide ? Math.Clamp((width - 500) / 19.5, 38.0, 44.0) : 38.0;
            foreach (var key in MagneticKeyLayout.Keys)
            {
                var button = _keyButtons[key.LogicalId];
                button.Width = unit * key.Units;
                button.Height = wide ? 44 : 38;
                Canvas.SetLeft(button, MagneticKeyLayout.LeftOf(key, unit));
            }
            foreach (var canvas in _keyboardCanvases)
            {
                canvas.Width = MagneticKeyLayout.RightmostColumn(unit) + 1.3 * unit;
                canvas.Height = wide ? 44 : 38;
            }
        }, maxWidth: 1380);
        PageScroll.SizeChanged += (_, _) => PageHost.MinHeight = PageScroll.ViewportHeight;
        Loaded += async (_, _) =>
        {
            await _model.RefreshAsync();
            Render();
        };
        Render();
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
                var button = new Button
                {
                    Content = key.Label,
                    Width = 38 * key.Units,
                    Height = 38,
                    MinWidth = 0,
                    Padding = new Thickness(1),
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
            }
            panel.Width = MagneticKeyLayout.RightmostColumn(38) + 1.3 * 38;
            _keyboardCanvases.Add(panel);
            KeyboardRows.Children.Add(panel);
        }
    }

    private void Key_Click(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: ushort logicalId } && _model.Select(logicalId)) Render();
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
            var dks = status?.Dks.FirstOrDefault(value => value.LogicalId == key);

            GlobalSelectButton.Style = _model.IsGlobalMode ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
            GlobalContextNotice.IsOpen = _model.IsGlobalMode;

            // 1. Selected Key / Global Hero Presentation
            if (_model.IsGlobalMode)
            {
                KeyCapVisualText.Text = "全";
                KeyCapVisualBorder.Background = (Brush)Application.Current.Resources["AccentFillColorDefaultBrush"];
                KeyCapVisualText.Foreground = (Brush)Application.Current.Resources["TextOnAccentFillColorPrimaryBrush"];
                SelectedKeyHeroTitle.Text = "全部按键";
                SelectedKeyHeroDesc.Text = "全局磁轴设置";
                SelectedKeyTagText.Text = "全局基础值";
                SelectedKeyDebugInfo.Text = "Global";
                SelectedKeyText.Text = "已选按键：全部按键 (全局磁轴设置)";

                foreach (var (_, button) in _keyButtons) button.Style = null;
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
                SelectedKeyText.Text = $"已选按键：{selected.FullName}";

                foreach (var (id, button) in _keyButtons)
                    button.Style = id == _model.SelectedLogicalId ?
                        (Style)Application.Current.Resources["AccentButtonStyle"] : null;
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
                SelectedKeyText.Text = "已选按键：无";

                foreach (var (_, button) in _keyButtons) button.Style = null;
            }

            RapidTriggerMasterStatusText.Text = _model.RapidTriggerMasterText;
            RapidTriggerMasterExplanationText.Text = MagneticSettingsModel.RapidTriggerMasterExplanation;

            if (_model.IsGlobalMode)
            {
                // Card headers & descriptions
                ActuationCard.Header = "全局触发点";
                ActuationCard.Description = "全键盘按键基础触发深度 (0.1–4.0 mm)；移动滑杆仅修改草稿";
                RapidTriggerCard.Header = "全局快速触发";
                RapidTriggerCard.Description = "全键盘按键基础灵敏度与独立按下/抬起模式";
                DeadzoneCard.Header = "全局死区设置";
                DeadzoneCard.Description = "全键盘按键基础死区 (0.0–0.5 mm)；不清除已有逐键覆盖";
                DksExpander.Visibility = Visibility.Collapsed;

                RapidTriggerToggle.Visibility = Visibility.Collapsed;
                SeparateModeToggle.Visibility = Visibility.Visible;

                // Enabled states
                ActuationSlider.IsEnabled = _model.CanWriteGlobal;
                PressSlider.IsEnabled = _model.CanWriteGlobal;
                ReleaseSlider.IsEnabled = _model.CanWriteGlobal;
                TopSlider.IsEnabled = _model.CanWriteGlobal;
                BottomSlider.IsEnabled = _model.CanWriteGlobal;
                SeparateModeToggle.IsEnabled = _model.CanWriteGlobal;

                // Values & Displays
                var globalDraft = _model.GlobalDraft;
                var globalAct = status?.GlobalActuation;
                var globalRt = status?.GlobalRapidTrigger;
                var globalDz = status?.GlobalDeadzone;

                bool sepMode = globalDraft.SeparateMode ?? globalRt?.SeparateMode ??
                    (globalRt is { Known: true } ? globalRt.PressRaw != globalRt.ReleaseRaw :
                     status?.HostProfile.GlobalRtPress.Known == true && status?.HostProfile.GlobalRtRelease.Known == true &&
                     status.HostProfile.GlobalRtPress.Raw != status.HostProfile.GlobalRtRelease.Raw);
                SeparateModeToggle.IsOn = sepMode;

                ActuationSlider.Value = globalDraft.ActuationMm ??
                    (globalAct is { Known: true } ? globalAct.Raw / 10.0 :
                     status?.HostProfile.GlobalActuation.Known == true ? status.HostProfile.GlobalActuation.Raw / 10.0 : 1.0);
                PressSlider.Value = globalDraft.PressMm ??
                    (globalRt is { Known: true } ? globalRt.PressRaw / 10.0 :
                     status?.HostProfile.GlobalRtPress.Known == true ? status.HostProfile.GlobalRtPress.Raw / 10.0 : 0.4);
                ReleaseSlider.Value = globalDraft.ReleaseMm ??
                    (globalRt is { Known: true } ? globalRt.ReleaseRaw / 10.0 :
                     status?.HostProfile.GlobalRtRelease.Known == true ? status.HostProfile.GlobalRtRelease.Raw / 10.0 : 0.2);
                TopSlider.Value = globalDraft.TopMm ??
                    (globalDz is { Known: true } ? globalDz.TopRaw / 10.0 :
                     status?.HostProfile.GlobalDeadzoneTop.Known == true ? status.HostProfile.GlobalDeadzoneTop.Raw / 10.0 : 0.0);
                BottomSlider.Value = globalDraft.BottomMm ??
                    (globalDz is { Known: true } ? globalDz.BottomRaw / 10.0 :
                     status?.HostProfile.GlobalDeadzoneBottom.Known == true ? status.HostProfile.GlobalDeadzoneBottom.Raw / 10.0 : 0.1);

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
                ActuationAppliedText.Text = globalAct is { Known: true } ?
                    $"{SourceLabel(globalAct.Source)}：{globalAct.Raw / 10.0:F1} mm" :
                    status?.HostProfile.GlobalActuation.Known == true ?
                    $"已保存配置全局值：{status.HostProfile.GlobalActuation.Raw / 10.0:F1} mm" :
                    "全局基础触发点：未知";
                RapidTriggerAppliedText.Text = globalRt is { Known: true } ?
                    $"{SourceLabel(globalRt.Source)}：按下 {globalRt.PressRaw / 10.0:F1} mm，抬起 {globalRt.ReleaseRaw / 10.0:F1} mm ({(globalRt.SeparateMode == true ? "独立灵敏度" : "同步灵敏度")})" :
                    (status?.HostProfile.GlobalRtPress.Known == true && status?.HostProfile.GlobalRtRelease.Known == true) ?
                    $"已保存配置全局值：按下 {status.HostProfile.GlobalRtPress.Raw / 10.0:F1} mm，抬起 {status.HostProfile.GlobalRtRelease.Raw / 10.0:F1} mm" :
                    "全局基础快速触发：未知";
                RapidTriggerDisableWarningText.Visibility = Visibility.Collapsed;
                DeadzoneAppliedText.Text = globalDz is { Known: true } ?
                    $"{SourceLabel(globalDz.Source)}：顶部 {globalDz.TopRaw / 10.0:F1} mm，底部 {globalDz.BottomRaw / 10.0:F1} mm" :
                    (status?.HostProfile.GlobalDeadzoneTop.Known == true && status?.HostProfile.GlobalDeadzoneBottom.Known == true) ?
                    $"已保存配置全局值：顶部 {status.HostProfile.GlobalDeadzoneTop.Raw / 10.0:F1} mm，底部 {status.HostProfile.GlobalDeadzoneBottom.Raw / 10.0:F1} mm" :
                    "全局基础死区：未知";

                // Apply buttons
                ActuationApplyButton.Content = "应用全局触发点";
                bool actDirty = _model.CanWriteGlobal && globalDraft.ActuationMm != null;
                ActuationApplyButton.IsEnabled = actDirty;
                ActuationApplyButton.Style = actDirty ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;

                RapidTriggerApplyButton.Content = "应用全局快速触发";
                bool rtDirty = _model.CanWriteGlobal && (globalDraft.PressMm != null || globalDraft.ReleaseMm != null || globalDraft.SeparateMode != null);
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
                ActuationCard.Header = "触发点";
                ActuationCard.Description = "按键按下触发深度 (0.1–4.0 mm)；移动滑杆仅修改草稿";
                RapidTriggerCard.Header = "快速触发";
                RapidTriggerCard.Description = "根据移动方向即时重置与重新触发";
                DeadzoneCard.Header = "死区设置";
                DeadzoneCard.Description = "消除微触误触与触底杂音 (0.0–0.5 mm)";
                DksExpander.Visibility = Visibility.Visible;

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
            PressSlider.Value = draft?.PressMm ?? (rt?.Source == "SessionApplied" ? rt.PressRaw / 10.0 : 0.1);
            ReleaseSlider.Value = draft?.ReleaseMm ?? (rt?.Source == "SessionApplied" ? rt.ReleaseRaw / 10.0 : 0.1);
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
                rt.Source == "SessionApplied" ?
                $"本次会话：{(rt.Enabled ? "此键使用快速触发" : "关闭")}；按下 {rt.PressRaw / 10.0:F1} mm，抬起 {rt.ReleaseRaw / 10.0:F1} mm" :
                "已保存配置：此键使用快速触发（灵敏度未知）";
            DeadzoneAppliedText.Text = deadzone == null ? "此键逐键死区：未知（可参考下方已保存全局值）" :
                $"{SourceLabel(deadzone.Source)}：顶部 {deadzone.TopRaw / 10.0:F1} mm，底部 {deadzone.BottomRaw / 10.0:F1} mm";
            DksAppliedText.Text = dks == null ? "当前来源：未知" :
                $"{SourceLabel(dks.Source)}：{(dks.StandardRuntimeConfiguration ? "标准按键行为" : $"DKS {dks.StartRaw / 10.0:F1}–{dks.EndRaw / 10.0:F1} mm")}";

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
            }

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

            AnalogStateText.Text = status?.StaticAnalogEffect is { Known: true } analog ?
                $"{SourceLabel(analog.Source)}：{(analog.Value ? "开启" : "关闭")}" : "当前来源：未知";
            AnalogToggle.IsEnabled = _model.CanWrite;
            AnalogToggle.IsOn = _model.StaticAnalogDraft ??
                (status?.StaticAnalogEffect is { Known: true, Value: true });
            AnalogApplyButton.IsEnabled = _model.CanWrite && _model.StaticAnalogDraft != null;
            AnalogApplyButton.Style = _model.StaticAnalogDraft != null ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;

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
                HealthStatusDot.Fill = new SolidColorBrush(Windows.UI.Color.FromArgb(255, 16, 124, 65));
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
        "SessionApplied" => "本次会话", "HostProfile" => "已保存配置", _ => "未知" };

    private void GlobalSelectButton_Click(object sender, RoutedEventArgs e)
    {
        if (_rendering) return;
        _model.SelectGlobal();
        Render();
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
            bool separate = _model.GlobalDraft.SeparateMode ?? _model.Status?.GlobalRapidTrigger.SeparateMode ??
                (_model.Status?.GlobalRapidTrigger.PressRaw != _model.Status?.GlobalRapidTrigger.ReleaseRaw);
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
            bool separate = _model.GlobalDraft.SeparateMode ?? _model.Status?.GlobalRapidTrigger.SeparateMode ??
                (_model.Status?.GlobalRapidTrigger.PressRaw != _model.Status?.GlobalRapidTrigger.ReleaseRaw);
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
        if (_model.IsGlobalMode) await ApplyAsync(_model.ApplyGlobalActuationAsync);
        else await ApplyAsync(_model.ApplyActuationAsync);
    }
    private async void RapidTriggerApplyButton_Click(object sender, RoutedEventArgs e)
    {
        if (_model.IsGlobalMode)
        {
            await ApplyAsync(_model.ApplyGlobalRapidTriggerAsync);
            return;
        }
        var resolve = _model.Draft?.RapidTriggerEnabled == true && _model.RtConflictPossible;
        if (resolve && !await ConfirmAsync("DKS 与快速触发",
            "启用逐键快速触发将先为此键恢复标准按键行为，再应用快速触发。两次操作分别提交。\n\n确认继续吗？",
            "继续", "取消")) return;
        await ApplyAsync(() => _model.ApplyRapidTriggerAsync(resolve));
    }
    private async void DeadzoneApplyButton_Click(object sender, RoutedEventArgs e)
    {
        if (_model.IsGlobalMode) await ApplyAsync(_model.ApplyGlobalDeadzoneAsync);
        else await ApplyAsync(_model.ApplyDeadzoneAsync);
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
}
