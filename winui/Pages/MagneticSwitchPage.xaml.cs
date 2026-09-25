using Aura_WinUI.Common;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;

namespace Aura_WinUI.Pages;

public sealed partial class MagneticSwitchPage : Page
{
    private readonly MagneticSettingsModel _model = new(new MagneticControlClient());
    private readonly Dictionary<ushort, Button> _keyButtons = [];
    private readonly List<Canvas> _keyboardCanvases = [];
    private readonly List<(ComboBox Target, ComboBox DownStart, ComboBox DownEnd,
        ComboBox UpStart, ComboBox UpEnd)> _dksControls = [];
    private bool _rendering;

    public MagneticSwitchPage()
    {
        InitializeComponent();
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
            bool wide = width >= 1160;
            ContentColumns.ColumnDefinitions[1].Width = wide ? new GridLength(340) : new GridLength(0);
            Grid.SetColumn(DetailsPanel, wide ? 1 : 0);
            Grid.SetRow(DetailsPanel, wide ? 0 : 1);
            var unit = wide ? Math.Min(55, (width - 430) / 18.5) : 38;
            foreach (var key in MagneticKeyLayout.Keys)
            {
                var button = _keyButtons[key.LogicalId];
                button.Width = unit * key.Units;
                button.Height = wide ? 46 : 38;
                Canvas.SetLeft(button, MagneticKeyLayout.LeftOf(key, unit));
            }
            foreach (var canvas in _keyboardCanvases)
            {
                canvas.Width = MagneticKeyLayout.RightmostColumn(unit) + 1.3 * unit;
                canvas.Height = wide ? 46 : 38;
            }
        }, maxWidth: 1500);
        PageScroll.SizeChanged += (_, _) => PageHost.MinHeight = PageScroll.ViewportHeight;
        Loaded += async (_, _) =>
        {
            await _model.RefreshAsync();
            Render();
        };
        Render();
    }

    private void BuildDksEditor()
    {
        string[] labels = ["无动作", "单次触发", "释放", "持续按住"];
        string[] states = ["Inactive", "Tap", "Release", "Hold"];
        for (int index = 0; index < 4; index++)
        {
            var panel = new StackPanel { Spacing = 4 };
            panel.Children.Add(new TextBlock { Text = $"动作槽 {index + 1}" });
            var target = new ComboBox { Header = "目标键", MinWidth = 210 };
            target.Items.Add(new ComboBoxItem { Content = "标准 / 无附加键", Tag = null });
            foreach (var key in MagneticKeyLayout.Keys.Where(k => MagneticKeyLayout.IsValidDksActionTarget(k.LogicalId)))
                target.Items.Add(new ComboBoxItem { Content = key.FullName, Tag = key.LogicalId });
            target.SelectionChanged += DksSlot_SelectionChanged;
            AutomationProperties.SetAutomationId(target, $"MagneticDksTarget{index + 1}");
            panel.Children.Add(target);
            ComboBox MakeState(string header)
            {
                var box = new ComboBox { Header = header, Width = 125 };
                for (int state = 0; state < 4; ++state)
                    box.Items.Add(new ComboBoxItem { Content = labels[state], Tag = states[state] });
                box.SelectionChanged += DksSlot_SelectionChanged;
                AutomationProperties.SetAutomationId(box, $"MagneticDks{header}{index + 1}");
                return box;
            }
            var downStart = MakeState("按下起点");
            var downEnd = MakeState("按下终点");
            var upStart = MakeState("抬起起点");
            var upEnd = MakeState("抬起终点");
            var first = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
            first.Children.Add(downStart); first.Children.Add(downEnd);
            var second = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
            second.Children.Add(upStart); second.Children.Add(upEnd);
            panel.Children.Add(first); panel.Children.Add(second);
            DksSlotsHost.Children.Add(panel);
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
                    Padding = new Thickness(2),
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
            SelectedKeyText.Text = selected == null ? "已选按键：无" : $"已选按键：{selected.FullName}";
            foreach (var (id, button) in _keyButtons)
                button.Style = id == _model.SelectedLogicalId ?
                    (Style)Application.Current.Resources["AccentButtonStyle"] : null;

            ActuationSlider.IsEnabled = _model.CanWriteSelected;
            PressSlider.IsEnabled = _model.CanWriteSelected;
            ReleaseSlider.IsEnabled = _model.CanWriteSelected;
            TopSlider.IsEnabled = _model.CanWriteSelected;
            BottomSlider.IsEnabled = _model.CanWriteSelected;
            RapidTriggerToggle.IsEnabled = _model.CanWriteSelected;
            ActuationSlider.Value = draft?.ActuationMm ?? (actuation?.Raw / 10.0) ?? 0.1;
            PressSlider.Value = draft?.PressMm ?? (rt?.Source == "SessionApplied" ? rt.PressRaw / 10.0 : 0.1);
            ReleaseSlider.Value = draft?.ReleaseMm ?? (rt?.Source == "SessionApplied" ? rt.ReleaseRaw / 10.0 : 0.1);
            TopSlider.Value = draft?.TopMm ?? (deadzone?.TopRaw / 10.0) ?? 0;
            BottomSlider.Value = draft?.BottomMm ?? (deadzone?.BottomRaw / 10.0) ?? 0;
            RapidTriggerToggle.IsOn = draft?.RapidTriggerEnabled ?? rt?.Enabled ?? false;
            DksStartSlider.Value = draft?.DksStartMm ?? (dks?.StartRaw / 10.0) ?? 0.1;
            DksEndSlider.Value = draft?.DksEndMm ?? (dks?.EndRaw / 10.0) ?? 0.1;
            DksDraftText.Text = draft == null ? "起止行程：未指定" :
                $"起点：{(draft.DksStartMm is double start ? $"{start:F1} mm" : "未指定")}；终点：{(draft.DksEndMm is double end ? $"{end:F1} mm" : "未指定")}";
            for (int i = 0; i < _dksControls.Count; ++i)
            {
                var controls = _dksControls[i];
                var slot = draft?.DksSlots[i];
                ushort? target = slot?.Target.Kind == "LogicalKey" ? slot.Target.LogicalId : null;
                controls.Target.SelectedIndex = target is ushort id && MagneticKeyLayout.IsValidDksActionTarget(id) ?
                    MagneticKeyLayout.Keys.Where(k => MagneticKeyLayout.IsValidDksActionTarget(k.LogicalId)).ToList().FindIndex(k => k.LogicalId == id) + 1 : 0;
                static int StateIndex(string? state) => state switch {
                    "Tap" => 1, "Release" => 2, "Hold" => 3, _ => 0 };
                controls.DownStart.SelectedIndex = StateIndex(slot?.DownStart);
                controls.DownEnd.SelectedIndex = StateIndex(slot?.DownEnd);
                controls.UpStart.SelectedIndex = StateIndex(slot?.UpStart);
                controls.UpEnd.SelectedIndex = StateIndex(slot?.UpEnd);
                foreach (var box in new[] { controls.Target, controls.DownStart, controls.DownEnd,
                    controls.UpStart, controls.UpEnd }) box.IsEnabled = _model.CanWriteSelected;
            }
            ActuationDraftText.Text = Label("草稿", draft?.ActuationMm);
            PressDraftText.Text = Label("按下草稿", draft?.PressMm);
            ReleaseDraftText.Text = Label("抬起草稿", draft?.ReleaseMm);
            TopDraftText.Text = Label("顶部草稿", draft?.TopMm);
            BottomDraftText.Text = Label("底部草稿", draft?.BottomMm);

            ActuationAppliedText.Text = actuation != null ?
                $"{SourceLabel(actuation.Source)}：{actuation.Raw / 10.0:F1} mm（不是设备读回）" :
                status?.HostProfile.GlobalActuation is { Known: true } globalActuation ?
                $"活动配置保存的全局值：{globalActuation.Raw / 10.0:F1} mm；此键逐键值未知" :
                "此键逐键值：未知";
            RapidTriggerAppliedText.Text = rt == null ?
                status?.HostProfile.PerKeyRtListKnown == true ?
                "活动配置逐键 RT 列表未包含此键；设备状态未知" : "逐键 RT：未知" :
                rt.Source == "SessionApplied" ?
                $"本次会话已应用：{(rt.Enabled ? "启用" : "禁用")}；按下 {rt.PressRaw / 10.0:F1} mm，抬起 {rt.ReleaseRaw / 10.0:F1} mm" :
                "活动配置保存的逐键 RT：启用；灵敏度未知，设备状态未知";
            DeadzoneAppliedText.Text = deadzone == null ? "此键逐键死区：未知；可参考下方活动配置全局值" :
                $"本次会话已应用：顶部 {deadzone.TopRaw / 10.0:F1} mm，底部 {deadzone.BottomRaw / 10.0:F1} mm";
            DksAppliedText.Text = dks == null ? "当前来源：未知（未读取设备配置）" :
                $"{SourceLabel(dks.Source)}：{(dks.StandardRuntimeConfiguration ? "标准按键行为" : $"DKS {dks.StartRaw / 10.0:F1}–{dks.EndRaw / 10.0:F1} mm")}";
            ActuationApplyButton.IsEnabled = _model.CanWriteSelected && draft?.ActuationMm != null;
            RapidTriggerApplyButton.IsEnabled = _model.CanWriteSelected &&
                (draft?.RapidTriggerEnabled == true ? draft.PressMm != null && draft.ReleaseMm != null :
                 draft?.RapidTriggerEnabled == false && _model.CanDisableRapidTrigger);
            DeadzoneApplyButton.IsEnabled = _model.CanWriteSelected && draft?.TopMm != null && draft.BottomMm != null;
            DksStartSlider.IsEnabled = _model.CanWriteSelected;
            DksEndSlider.IsEnabled = _model.CanWriteSelected;
            DksApplyButton.IsEnabled = _model.CanWriteSelected && draft?.DksDirty == true &&
                draft.DksStartMm is double dksStart && draft.DksEndMm is double dksEnd && dksStart <= dksEnd;
            DksStandardButton.IsEnabled = _model.CanWriteSelected;
            var host = status?.HostProfile;
            GlobalDeadzoneText.Text = host is { GlobalDeadzoneTop.Known: true, GlobalDeadzoneBottom.Known: true } ?
                $"活动配置保存值：顶部 {host.GlobalDeadzoneTop.Raw / 10.0:F1} mm，底部 {host.GlobalDeadzoneBottom.Raw / 10.0:F1} mm（不是设备读回）" :
                "活动配置全局值：未知；无法安全重置全部覆盖";
            ResetAllDeadzoneButton.IsEnabled = _model.CanResetAllDeadzone;
            var speedtap = status?.SpeedTap;
            SpeedTapStateText.Text = speedtap == null ? "键对与 Master 来源：未知" :
                $"Master：{(speedtap.Master.Known ? $"{(speedtap.Master.Value ? "开" : "关")}（{SourceLabel(speedtap.Master.Source)}）" : "未知")}；" +
                $"本次会话提交 {speedtap.PairSubmissions.Count} 对；活动配置保存 {(speedtap.SavedProfilePairsKnown ? $"{speedtap.SavedProfilePairs.Count} 对" : "未知（不可启用新键对）")}；" +
                $"键对基线：{speedtap.PairKnowledge}（设备完整键对表未知）";
            SpeedTapKey1.IsEnabled = _model.CanWrite;
            SpeedTapKey2.IsEnabled = _model.CanWrite;
            SpeedTapMasterToggle.IsEnabled = _model.CanWrite;
            SpeedTapMasterToggle.IsOn = _model.SpeedTapMasterDraft ??
                (speedtap?.Master is { Known: true, Value: true });
            SpeedTapPairOnButton.IsEnabled = _model.CanEnableSpeedTapPair;
            SpeedTapPairOffButton.IsEnabled = _model.CanDisableSpeedTapPair;
            SpeedTapMasterApplyButton.IsEnabled = _model.CanWrite && _model.SpeedTapMasterDraft != null;
            SpeedTapProfileResetButton.IsEnabled = _model.CanWrite;
            AnalogStateText.Text = status?.StaticAnalogEffect is { Known: true } analog ?
                $"{SourceLabel(analog.Source)}：{(analog.Value ? "开启" : "关闭")}（不是设备读回）" : "当前来源：未知";
            AnalogToggle.IsEnabled = _model.CanWrite;
            AnalogToggle.IsOn = _model.StaticAnalogDraft ??
                (status?.StaticAnalogEffect is { Known: true, Value: true });
            AnalogApplyButton.IsEnabled = _model.CanWrite && _model.StaticAnalogDraft != null;
            RefreshButton.IsEnabled = !_model.Busy && !_model.Refreshing;

            HealthBar.Title = _model.Busy ? "正在应用" : _model.Quarantined ? "磁轴写入已停止" : "磁轴运行状态";
            HealthBar.Severity = _model.Quarantined ? InfoBarSeverity.Error :
                status is { Succeeded: true, Available: true, Health: "Clean" } ? InfoBarSeverity.Informational :
                InfoBarSeverity.Warning;
            HealthBar.Message = _model.LastMessage;
        }
        finally { _rendering = false; }
    }

    private static string Label(string name, double? value) =>
        value is double mm ? $"{name}：{mm:F1} mm" : $"{name}：未指定";
    private static string SourceLabel(string source) => source switch {
        "SessionApplied" => "本次会话已应用", "HostProfile" => "活动配置保存值", _ => "未知" };

    private void ActuationSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering || _model.SelectedKey == null) return;
        _model.EditActuation(Math.Round(e.NewValue, 1)); Render();
    }
    private void RapidTriggerToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_rendering || _model.SelectedKey == null) return;
        _model.EditRapidTrigger(RapidTriggerToggle.IsOn, null, null); Render();
    }
    private void PressSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering || _model.SelectedKey == null) return;
        _model.EditRapidTrigger(RapidTriggerToggle.IsOn, Math.Round(e.NewValue, 1), null); Render();
    }
    private void ReleaseSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering || _model.SelectedKey == null) return;
        _model.EditRapidTrigger(RapidTriggerToggle.IsOn, null, Math.Round(e.NewValue, 1)); Render();
    }
    private void TopSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering || _model.SelectedKey == null) return;
        _model.EditDeadzone(Math.Round(e.NewValue, 1), null); Render();
    }
    private void BottomSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_rendering || _model.SelectedKey == null) return;
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
    private async void ActuationApplyButton_Click(object sender, RoutedEventArgs e) => await ApplyAsync(_model.ApplyActuationAsync);
    private async void RapidTriggerApplyButton_Click(object sender, RoutedEventArgs e)
    {
        var resolve = _model.Draft?.RapidTriggerEnabled == true && _model.RtConflictPossible;
        if (resolve && !await ConfirmAsync("DKS 与快速触发", "启用逐键快速触发将先为此键恢复标准按键行为，再应用快速触发。两次操作分别提交。")) return;
        await ApplyAsync(() => _model.ApplyRapidTriggerAsync(resolve));
    }
    private async void DeadzoneApplyButton_Click(object sender, RoutedEventArgs e) => await ApplyAsync(_model.ApplyDeadzoneAsync);

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
        if (resolve && !await ConfirmAsync("DKS 与快速触发", "启用 DKS 将先关闭该键逐键快速触发，再应用四段动作。两次操作分别提交。")) return;
        await ApplyAsync(() => _model.ApplyDksAsync(resolve));
    }
    private async void DksStandardButton_Click(object sender, RoutedEventArgs e) =>
        await ApplyAsync(_model.RestoreDksStandardAsync);

    private async void ResetAllDeadzoneButton_Click(object sender, RoutedEventArgs e)
    {
        if (!await ConfirmAsync("重置全部逐键死区覆盖", "这会清除所有按键的逐键死区覆盖，不限于当前所选按键。将使用活动配置保存的全局顶部和底部值。")) return;
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
        if (!await ConfirmAsync("恢复 SpeedTap 配置基线", "这会把运行时键对恢复到活动配置基线；不会清空已保存的键对，也不会更改 Master。")) return;
        await ApplyAsync(_model.ResetSpeedTapToProfileAsync);
    }
    private void AnalogToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_rendering) return;
        _model.EditStaticAnalog(AnalogToggle.IsOn); Render();
    }
    private async void AnalogApplyButton_Click(object sender, RoutedEventArgs e) =>
        await ApplyAsync(_model.ApplyStaticAnalogAsync);

    private async Task<bool> ConfirmAsync(string title, string message)
    {
        var dialog = new ContentDialog {
            XamlRoot = XamlRoot, Title = title, Content = message,
            PrimaryButtonText = "确认应用", CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Close
        };
        return await dialog.ShowAsync() == ContentDialogResult.Primary;
    }
}
