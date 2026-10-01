using Aura_WinUI.Common;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using System.Runtime.InteropServices;

namespace Aura_WinUI.Pages;

public sealed partial class ProfilesPage : Page
{
    private sealed record ProfileChoice(DeviceProfile Profile, string Label);

    [DllImport("user32.dll")]
    private static extern short GetKeyState(int virtualKey);

    private readonly ProfilePageModel _model;
    private readonly Dictionary<ushort, (Button Button, TextBlock Marker)> _keys = [];
    private readonly List<Canvas> _keyRows = [];
    private readonly HashSet<ushort> _selectedKeys = [];
    private readonly List<(ComboBox Target, ComboBox DownStart, ComboBox DownEnd,
        ComboBox UpStart, ComboBox UpEnd)> _dksControls = [];
    private bool _rendering;
    private bool _multiSelect;
    private double _editorWidth;
    private readonly DispatcherTimer _statusTimer = new() { Interval = TimeSpan.FromSeconds(1) };
    private CancellationTokenSource? _statusCancellation;
    private bool _statusRefreshing;

    public ProfilesPage() : this(new ProfilePageModel(App.ProfileClient)) { }

    internal ProfilesPage(ProfilePageModel model)
    {
        _model = model;
        _rendering = true;
        InitializeComponent();
        NavigationCacheMode = Microsoft.UI.Xaml.Navigation.NavigationCacheMode.Required;
        ConfigureSlider(GlobalActSlider, 0.1, 4.0);
        ConfigureSlider(KeyActSlider, 0.1, 4.0);
        foreach (var slider in new[] { GlobalTopSlider, GlobalBottomSlider, GlobalRtTopSlider,
            GlobalRtBottomSlider, KeyTopSlider, KeyBottomSlider }) ConfigureSlider(slider, 0, 0.5);
        foreach (var slider in new[] { GlobalPressSlider, GlobalReleaseSlider, KeyPressSlider,
            KeyReleaseSlider }) ConfigureSlider(slider, 0.1, 2.5);
        ConfigureSlider(DksStartSlider, 0.1, 4.0);
        ConfigureSlider(DksEndSlider, 0.1, 4.0);
        BuildKeyboard();
        BuildDksEditor();
        PageLayout.Attach(this, PageScroll, PageContent, Reflow, maxWidth: 1420);
        PageLayout.Notification(this, NoticeBar);
        _statusTimer.Tick += async (_, _) => {
            if (_statusRefreshing || _statusCancellation is null) return;
            _statusRefreshing = true;
            try {
                await _model.RefreshHardwareRtGateAsync(_statusCancellation.Token);
                HardwareRtGateBar.Message = _model.HardwareRtGateText;
            }
            catch (OperationCanceledException) { }
            finally { _statusRefreshing = false; }
        };
        Loaded += async (_, _) => {
            _statusCancellation?.Cancel(); _statusCancellation?.Dispose();
            _statusCancellation = new();
            var token = _statusCancellation.Token;
            try { await _model.LoadAsync(token); if (!token.IsCancellationRequested) { Render(); _statusTimer.Start(); } }
            catch (OperationCanceledException) { }
        };
        Unloaded += (_, _) => { _statusTimer.Stop(); _statusCancellation?.Cancel(); };
        ActualThemeChanged += (_, _) => RenderKeyboard();
        _rendering = false;
        Render();
    }

    private static void ConfigureSlider(Slider slider, double min, double max)
    {
        slider.Minimum = min; slider.Maximum = max; slider.StepFrequency = 0.1; slider.Value = min;
    }

    private void Reflow(double width)
    {
        _editorWidth = width;
        var wide = width >= 1080;
        EditorColumns.ColumnDefinitions[1].Width = wide ? new GridLength(286) : new GridLength(0);
        ContextPanel.Visibility = wide ? Visibility.Visible : Visibility.Collapsed;
        Grid.SetColumn(ContextPanel, 1);
        PageLayout.Columns(DksSlotsHost, wide ? 2 : 1);
        foreach (var panel in new[] { GlobalActFields, GlobalDzFields, GlobalRtFields,
            KeyActFields, KeyDzFields, KeyRtFields, DksFields }) {
            // Let the card's measured width constrain wrapped help/validation.
            // An explicit width derived from a previous ScrollViewer viewport
            // can remain too wide during a window resize.
            panel.Width = double.NaN;
            panel.MaxWidth = 720;
            panel.HorizontalAlignment = HorizontalAlignment.Stretch;
        }

        var stacked = width < 680;
        Grid.SetColumn(ProfileCommands, stacked ? 0 : 1);
        Grid.SetRow(ProfileCommands, stacked ? 1 : 0);
        Grid.SetColumn(SaveActions, stacked ? 0 : 1);
        Grid.SetRow(SaveActions, stacked ? 1 : 0);
        if (stacked) {
            Grid.SetColumn(HeroActions, 0); Grid.SetRow(HeroActions, 1);
        } else {
            Grid.SetColumn(HeroActions, 1); Grid.SetRow(HeroActions, 0);
        }
        var unit = wide ? Math.Clamp((width - 330) / 17.5, 35, 42) : 36.0;
        foreach (var visual in MagneticKeyLayout.Keys) {
            var button = _keys[visual.LogicalId].Button;
            button.Width = visual.Units * unit;
            button.Height = unit;
            Canvas.SetLeft(button, MagneticKeyLayout.LeftOf(visual, unit));
        }
        foreach (var canvas in _keyRows) {
            canvas.Width = MagneticKeyLayout.RightmostColumn(unit) + 1.3 * unit;
            canvas.Height = unit;
        }
    }

    private void BuildKeyboard()
    {
        for (var row = 0; row < 5; row++) {
            var canvas = new Canvas { Height = 36 };
            foreach (var key in MagneticKeyLayout.Keys.Where(k => k.Row == row)) {
                var content = new Grid();
                var legend = new TextBlock { Text = key.Label, TextTrimming = TextTrimming.CharacterEllipsis,
                    FontSize = 11, HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center, IsHitTestVisible = false };
                var marker = new TextBlock { Text = "•", FontSize = 14,
                    HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Top,
                    Visibility = Visibility.Collapsed, IsHitTestVisible = false };
                content.Children.Add(legend); content.Children.Add(marker);
                var button = new Button { Content = content, Width = 36 * key.Units, Height = 36,
                    MinWidth = 0, Padding = new Thickness(1), CornerRadius = new CornerRadius(5),
                    Tag = key.LogicalId, HorizontalContentAlignment = HorizontalAlignment.Stretch,
                    VerticalContentAlignment = VerticalAlignment.Stretch };
                AutomationProperties.SetAutomationId(button, $"ProfileKey{key.LogicalId:X4}");
                button.Click += Key_Click;
                Canvas.SetLeft(button, MagneticKeyLayout.LeftOf(key, 36));
                canvas.Children.Add(button);
                _keys.Add(key.LogicalId, (button, marker));
            }
            canvas.Width = MagneticKeyLayout.RightmostColumn(36) + 1.3 * 36;
            KeyboardRows.Children.Add(canvas); _keyRows.Add(canvas);
        }
    }

    // Matches the four-slot target/trigger structure of the alpha.6 DKS editor.
    // It only changes the Profile draft and never uses the manual device API.
    private void BuildDksEditor()
    {
        for (var i = 0; i < 4; i++) {
            var panel = new StackPanel { Spacing = 5 };
            panel.Children.Add(new TextBlock { Text = $"动作 {i + 1}", FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
            var target = new ComboBox { Header = "目标按键", HorizontalAlignment = HorizontalAlignment.Stretch };
            target.Items.Add(new ComboBoxItem { Content = "标准 / 无附加键", Tag = null });
            foreach (var key in MagneticKeyLayout.Keys.Where(k => MagneticKeyLayout.IsValidDksActionTarget(k.LogicalId)))
                target.Items.Add(new ComboBoxItem { Content = KeyDisplay(key), Tag = key.LogicalId });
            target.SelectionChanged += DksSlot_SelectionChanged;
            panel.Children.Add(target);
            ComboBox MakeState(string header) {
                var box = new ComboBox { Header = header, HorizontalAlignment = HorizontalAlignment.Stretch };
                foreach (var (label, state) in new[] { ("无操作", "Inactive"), ("单次触发", "Tap"),
                    ("释放", "Release"), ("保持", "Hold") })
                    box.Items.Add(new ComboBoxItem { Content = label, Tag = state });
                box.SelectionChanged += DksSlot_SelectionChanged;
                return box;
            }
            var downStart = MakeState("按下起点"); var downEnd = MakeState("按下终点");
            var upStart = MakeState("抬起起点"); var upEnd = MakeState("抬起终点");
            panel.Children.Add(downStart); panel.Children.Add(downEnd);
            panel.Children.Add(upStart); panel.Children.Add(upEnd);
            var border = new Border { Child = panel, Padding = new Thickness(10), CornerRadius = new CornerRadius(6),
                BorderThickness = new Thickness(1),
                BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"] };
            DksSlotsHost.Children.Add(border);
            _dksControls.Add((target, downStart, downEnd, upStart, upEnd));
        }
    }

    private void Render()
    {
        HardwareRtGateBar.Message = _model.HardwareRtGateText;
        _rendering = true;
        try {
            var snapshot = _model.Snapshot;
            var draft = _model.Draft;
            HeroName.Text = _model.CurrentName;
            HeroState.Text = _model.StateTitle;
            HeroDescription.Text = _model.StateDescription;
            LoadingRing.IsActive = _model.State == ProfilePageState.Loading || _model.IsBusy;
            LoadingRing.Visibility = LoadingRing.IsActive ? Visibility.Visible : Visibility.Collapsed;
            RetryButton.Visibility = _model.State is ProfilePageState.DaemonUnavailable or
                ProfilePageState.IncompatibleDaemon or
                ProfilePageState.ApiProtocolError or
                ProfilePageState.DocumentUnavailable ? Visibility.Visible : Visibility.Collapsed;
            RetryApplyButton.Visibility = _model.State is ProfilePageState.NeedsApply or
                ProfilePageState.Deferred or ProfilePageState.ApplyFailed ? Visibility.Visible : Visibility.Collapsed;
            DetailsButton.Visibility = (_model.State is ProfilePageState.ApplyFailed or
                ProfilePageState.DocumentUnavailable) &&
                !string.IsNullOrWhiteSpace(_model.ApplyDetails) ? Visibility.Visible : Visibility.Collapsed;
            NoticeBar.IsOpen = !string.IsNullOrWhiteSpace(_model.Notice);
            NoticeBar.Message = _model.Notice;
            DraftValidationBar.Message = _model.DraftValidationText;
            DraftValidationBar.IsOpen = !string.IsNullOrWhiteSpace(DraftValidationBar.Message);
            RapidTriggerValidationBar.Message = string.Join(Environment.NewLine,
                _model.RapidTriggerIssues.Where(i => i.LogicalId is null).Select(i => i.Message));
            RapidTriggerValidationBar.IsOpen = !string.IsNullOrWhiteSpace(RapidTriggerValidationBar.Message);
            KeyRtValidationBar.Message = string.Join(Environment.NewLine,
                _model.RapidTriggerIssues.Where(i => i.LogicalId is not null).Select(i => i.Message));
            KeyRtValidationBar.IsOpen = !string.IsNullOrWhiteSpace(KeyRtValidationBar.Message);
            RtKeySetSummary.Text = _model.RapidTriggerSelectionSummary;
            NoticeBar.Severity = _model.NoticeKind switch {
                ProfileNoticeKind.Success => InfoBarSeverity.Success,
                ProfileNoticeKind.Warning => InfoBarSeverity.Warning,
                ProfileNoticeKind.Error => InfoBarSeverity.Error,
                _ => InfoBarSeverity.Informational
            };
            ConflictBar.IsOpen = _model.HasConflict;
            DiscardDraftButton.Visibility = _model.HasConflict ? Visibility.Visible : Visibility.Collapsed;
            var available = snapshot is not null && _model.State is not
                (ProfilePageState.DaemonUnavailable or ProfilePageState.IncompatibleDaemon or
                ProfilePageState.ApiProtocolError or ProfilePageState.DocumentUnavailable or ProfilePageState.Loading);
            ProfilePicker.IsEnabled = available && !_model.IsBusy;
            CreateButton.IsEnabled = available && !_model.IsBusy && !_model.HasUnsavedChanges && !_model.HasConflict;
            DuplicateButton.IsEnabled = RenameButton.IsEnabled = available && draft is not null && !_model.IsBusy &&
                !_model.HasUnsavedChanges && !_model.HasConflict;
            DeleteButton.IsEnabled = available && _model.CanDeleteEditing && !_model.HasConflict && !_model.HasUnsavedChanges;
            RetryButton.IsEnabled = !_model.IsBusy;
            RetryApplyButton.IsEnabled = _model.CanApply;

            if (snapshot?.Profiles is not null) {
                var choices = snapshot.Profiles.Select(profile => {
                    var selected = profile.Id == snapshot.SelectedProfileId;
                    var active = profile.Id == snapshot.ActiveProfileId && !snapshot.Dirty;
                    var label = profile.Name + (selected ? " · 当前选择" : "") +
                        (active ? " · 已应用" : "");
                    return new ProfileChoice(profile, label);
                }).ToList();
                ProfilePicker.ItemsSource = choices;
                ProfilePicker.SelectedItem = choices.FirstOrDefault(choice => choice.Profile.Id == _model.EditingId);
            }
            EditorSurface.IsEnabled = available && draft is not null && !_model.IsBusy;
            EditorName.Text = draft is null ? "磁轴配置" : $"{draft.Name} · 磁轴配置";
            DraftStateText.Text = _model.HasConflict ? "草稿与最新版本冲突，请先处理冲突。" :
                _model.HasUnsavedChanges ? "有尚未保存的草稿更改。" :
                draft?.Id == snapshot?.SelectedProfileId ? "已保存；应用会提交当前选择的配置。" :
                "正在编辑另一个配置文件；应用会切换当前选择。";
            SaveButton.IsEnabled = available && !_model.IsBusy && _model.HasUnsavedChanges && !_model.HasConflict &&
                string.IsNullOrWhiteSpace(_model.DraftValidationText);
            ApplyButton.IsEnabled = _model.CanApply;
            SelectEnabledRtButton.IsEnabled = available && !_model.IsBusy && _model.EnabledRapidTriggerKeyCount > 0;
            SelectAllRtButton.IsEnabled = available && !_model.IsBusy;
            ContextName.Text = _model.EditingName;
            ContextState.Text = _model.StateTitle;
            ContextOverrides.Text = $"逐键覆盖：{_model.OverrideCount} 个";
            ApplyDetailsText.Text = string.IsNullOrWhiteSpace(_model.ApplyDetails) ? "尚无应用记录" : _model.ApplyDetails;
            LightingReferenceText.Text = string.IsNullOrWhiteSpace(draft?.Lighting.LegacyEffectReference) ?
                "未关联" : draft.Lighting.LegacyEffectReference;

            RenderGlobal();
            RenderSelectedKey();
            RenderKeyboard();
        } finally { _rendering = false; }
    }

    private void RenderGlobal()
    {
        var profile = _model.Draft?.Magnetic;
        var defaults = _model.Snapshot?.EffectiveGlobalDefaults ?? _model.Snapshot?.GlobalDefaults;
        GlobalActCustom.IsOn = profile?.GlobalActuationMm is not null;
        GlobalDzCustom.IsOn = profile?.GlobalDeadzone is not null;
        GlobalRtCustom.IsOn = profile?.GlobalRapidTrigger is not null;
        GlobalRtCustom.IsEnabled = profile?.GlobalRapidTrigger is not null; // remove-only compatibility control
        GlobalActSource.Text = profile?.GlobalActuationMm is double custom ? $"自定义：{custom:F1} mm" :
            defaults?.GlobalActuationMm is double a ? $"使用全局默认值（{a:F1} mm）" : "使用全局默认值：尚未设置";
        GlobalDzSource.Text = profile?.GlobalDeadzone is not null ? "来自此配置文件" :
            defaults?.GlobalDeadzone is not null ? "使用全局默认值" : "使用全局默认值：尚未设置";
        LegacyRtExpander.Visibility = profile?.GlobalRapidTrigger is not null || defaults?.GlobalRapidTrigger is not null ?
            Visibility.Visible : Visibility.Collapsed;
        if (RapidTriggerValidationBar.IsOpen) LegacyRtExpander.IsExpanded = true;
        GlobalRtSource.Text = profile?.GlobalRapidTrigger is not null ?
            "已有旧版参数被保留；当前不能应用，请使用逐键设置。" :
            defaults?.GlobalRapidTrigger is not null ? "文档仍包含旧版基础参数；尚未迁移。" : "没有旧版参数";
        GlobalActSlider.Value = profile?.GlobalActuationMm ?? defaults?.GlobalActuationMm ?? 1.0;
        GlobalTopSlider.Value = profile?.GlobalDeadzone?.TopMm ?? defaults?.GlobalDeadzone?.TopMm ?? 0;
        GlobalBottomSlider.Value = profile?.GlobalDeadzone?.BottomMm ?? defaults?.GlobalDeadzone?.BottomMm ?? 0.1;
        var rt = profile?.GlobalRapidTrigger ?? defaults?.GlobalRapidTrigger;
        GlobalRtEnabled.IsOn = rt?.Enabled ?? false;
        GlobalSeparateToggle.IsOn = rt?.SeparateMode ?? false;
        GlobalPressSlider.Value = rt?.PressMm ?? 0.4;
        GlobalReleaseSlider.Value = rt?.ReleaseMm ?? 0.4;
        GlobalRtTopSlider.Value = rt?.TopMm ?? 0;
        GlobalRtBottomSlider.Value = rt?.BottomMm ?? 0.1;
        GlobalActSlider.IsEnabled = GlobalActCustom.IsOn;
        GlobalTopSlider.IsEnabled = GlobalBottomSlider.IsEnabled = GlobalDzCustom.IsOn;
        GlobalRtEnabled.IsEnabled = false;
        GlobalSeparateToggle.IsEnabled = GlobalPressSlider.IsEnabled = GlobalRtTopSlider.IsEnabled =
            GlobalRtBottomSlider.IsEnabled = false;
        GlobalReleaseSlider.IsEnabled = false;
        GlobalActValue.Text = Mm(GlobalActSlider.Value); GlobalTopValue.Text = Mm(GlobalTopSlider.Value);
        GlobalBottomValue.Text = Mm(GlobalBottomSlider.Value); GlobalPressValue.Text = Mm(GlobalPressSlider.Value);
        GlobalReleaseValue.Text = Mm(GlobalReleaseSlider.Value);
        GlobalRtTopValue.Text = Mm(GlobalRtTopSlider.Value); GlobalRtBottomValue.Text = Mm(GlobalRtBottomSlider.Value);
    }

    private void RenderSelectedKey()
    {
        var ids = _selectedKeys.ToArray();
        SelectedKeysText.Text = ids.Length == 0 ? "请选择按键" : ids.Length == 1 ?
            $"{KeyDisplay(MagneticKeyLayout.Find(ids[0])!)} · 逐键覆盖" : $"已选择 {ids.Length} 个按键 · 批量编辑覆盖";
        MultiSelectButton.Style = _multiSelect ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
        ClearSelectionButton.IsEnabled = ids.Length > 0;
        var keys = ids.Select(id => _model.Draft?.Magnetic.Keys.FirstOrDefault(k => k.LogicalId == id)).ToArray();
        var first = keys.FirstOrDefault();
        var baseline = _model.Draft?.Magnetic;
        var defaults = _model.Snapshot?.EffectiveGlobalDefaults ?? _model.Snapshot?.GlobalDefaults;
        var act = first?.ActuationMm;
        var dz = first?.Deadzone;
        var rt = first?.RapidTrigger;
        var dks = first?.Dks;
        var has = ids.Length > 0;
        KeyActCustom.IsEnabled = KeyDzCustom.IsEnabled = KeyRtCustom.IsEnabled = has;
        KeyActCustom.IsOn = has && keys.All(k => k?.ActuationMm is not null);
        KeyDzCustom.IsOn = has && keys.All(k => k?.Deadzone is not null);
        KeyRtCustom.IsOn = has && keys.All(k => k?.RapidTrigger is not null);
        KeyActSource.Text = KeyActCustom.IsOn ? "自定义覆盖" :
            baseline?.GlobalActuationMm is double a ? $"使用配置文件默认值：{a:F1} mm" :
            defaults?.GlobalActuationMm is double ga ? $"使用全局默认值：{ga:F1} mm" : "使用配置文件默认值";
        KeyDzSource.Text = KeyDzCustom.IsOn ? "自定义覆盖" : "使用配置文件默认值";
        KeyRtSelectionText.Text = ids.Length == 0 ? "请在键盘图中选择按键。" :
            $"已选择 {ids.Length} 个按键。" + (ids.Length > 1 ? "下方显示首键草稿值，改动会批量更新全部所选按键。" : "");
        KeyRtSource.Text = KeyRtCustom.IsOn ? "所选按键由此配置文件设置。关闭快速触发会保存明确的禁用设置。" :
            "不设置所选按键。移除已有设置会尝试恢复原有手动配置；若原设置未知，应用会提示无法恢复。";
        KeyActSlider.Value = act ?? baseline?.GlobalActuationMm ?? defaults?.GlobalActuationMm ?? 1.0;
        KeyTopSlider.Value = dz?.TopMm ?? baseline?.GlobalDeadzone?.TopMm ?? defaults?.GlobalDeadzone?.TopMm ?? 0;
        KeyBottomSlider.Value = dz?.BottomMm ?? baseline?.GlobalDeadzone?.BottomMm ?? defaults?.GlobalDeadzone?.BottomMm ?? 0.1;
        KeyRtEnabled.IsOn = rt?.Enabled ?? false;
        KeyPressSlider.Value = rt?.PressMm ?? 0.4;
        KeyReleaseSlider.Value = rt?.ReleaseMm ?? 0.4;
        KeyActSlider.IsEnabled = has && KeyActCustom.IsOn;
        KeyTopSlider.IsEnabled = KeyBottomSlider.IsEnabled = has && KeyDzCustom.IsOn;
        KeyRtEnabled.IsEnabled = has && KeyRtCustom.IsOn;
        KeyPressSlider.IsEnabled = KeyReleaseSlider.IsEnabled = has && KeyRtCustom.IsOn && KeyRtEnabled.IsOn;
        KeyActValue.Text = Mm(KeyActSlider.Value); KeyTopValue.Text = Mm(KeyTopSlider.Value);
        KeyBottomValue.Text = Mm(KeyBottomSlider.Value); KeyPressValue.Text = Mm(KeyPressSlider.Value);
        KeyReleaseValue.Text = Mm(KeyReleaseSlider.Value);
        DksExpander.IsEnabled = ids.Length == 1;
        DksMode.SelectedIndex = dks is null ? 0 : dks.Standard ? 1 : 2;
        DksCustomPanel.Visibility = DksMode.SelectedIndex == 2 ? Visibility.Visible : Visibility.Collapsed;
        DksStartSlider.Value = dks?.StartMm ?? 1;
        DksEndSlider.Value = dks?.EndMm ?? 3.6;
        DksStartValue.Text = Mm(DksStartSlider.Value); DksEndValue.Text = Mm(DksEndSlider.Value);
        for (var i = 0; i < _dksControls.Count; i++) {
            var slot = dks?.Slots.ElementAtOrDefault(i);
            var row = _dksControls[i];
            var targetId = slot?.Target.Kind == "LogicalKey" ? slot.Target.LogicalId : null;
            row.Target.SelectedIndex = targetId is ushort id ?
                MagneticKeyLayout.Keys.Where(k => MagneticKeyLayout.IsValidDksActionTarget(k.LogicalId))
                    .ToList().FindIndex(k => k.LogicalId == id) + 1 : 0;
            row.DownStart.SelectedIndex = TriggerIndex(slot?.DownStart);
            row.DownEnd.SelectedIndex = TriggerIndex(slot?.DownEnd);
            row.UpStart.SelectedIndex = TriggerIndex(slot?.UpStart);
            row.UpEnd.SelectedIndex = TriggerIndex(slot?.UpEnd);
        }
    }

    private void RenderKeyboard()
    {
        foreach (var key in MagneticKeyLayout.Keys) {
            var (button, marker) = _keys[key.LogicalId];
            var overridden = _model.Draft?.Magnetic.Keys.Any(k => k.LogicalId == key.LogicalId &&
                ProfilePageModel.HasOverride(k)) == true;
            var selected = _selectedKeys.Contains(key.LogicalId);
            marker.Visibility = overridden ? Visibility.Visible : Visibility.Collapsed;
            button.Style = selected ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
            button.BorderThickness = new Thickness(selected ? 2 : 1);
            AutomationProperties.SetName(button, $"{KeyDisplay(key)}，{(selected ? "已选择" : "未选择")}，" +
                (overridden ? "有逐键覆盖" : "使用配置文件默认值"));
            ToolTipService.SetToolTip(button, $"{KeyDisplay(key)} · " +
                (overridden ? "有逐键覆盖" : "使用配置文件默认值"));
        }
    }

    private static string Mm(double value) => $"{value:F1} mm";
    private static string KeyDisplay(MagneticVisualKey key) =>
        key.FullName.StartsWith("Left ", StringComparison.Ordinal) ? "左" + key.Label :
        key.FullName.StartsWith("Right ", StringComparison.Ordinal) ? "右" + key.Label : key.Label;
    private static int TriggerIndex(string? value) => value switch {
        "Tap" => 1, "Release" => 2, "Hold" => 3, _ => 0
    };
    private static string TriggerValue(ComboBox box) => (box.SelectedItem as ComboBoxItem)?.Tag as string ?? "Inactive";
    private ProfileKey Key(ushort id)
    {
        var magnetic = _model.Draft!.Magnetic;
        var key = magnetic.Keys.FirstOrDefault(k => k.LogicalId == id);
        if (key is not null) return key;
        key = new ProfileKey { LogicalId = id }; magnetic.Keys.Add(key);
        return key;
    }
    private void PruneKeys() => _model.Draft?.Magnetic.Keys.RemoveAll(k => !ProfilePageModel.HasOverride(k));

    private async void ProfilePicker_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering || ProfilePicker.SelectedItem is not ProfileChoice profileChoice ||
            profileChoice.Profile.Id == _model.EditingId) return;
        var selected = profileChoice.Profile;
        if (_model.HasConflict) { Render(); return; }
        if (_model.HasUnsavedChanges) {
            var choice = await ConfirmDraftAsync();
            if (choice == ContentDialogResult.None) { Render(); return; }
            if (choice == ContentDialogResult.Primary && !await _model.SaveAsync()) { Render(); return; }
            if (choice == ContentDialogResult.Secondary) _model.DiscardDraft();
        }
        if (_model.Edit(selected.Id)) {
            _selectedKeys.Clear();
            await _model.ApplyAsync(selected.Id);
        }
        Render();
    }

    private void Key_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: ushort id }) return;
        var multi = _multiSelect || (GetKeyState(0x11) & 0x8000) != 0;
        if (!multi) { _selectedKeys.Clear(); _selectedKeys.Add(id); }
        else if (!_selectedKeys.Add(id)) _selectedKeys.Remove(id);
        Render();
    }
    private void MultiSelectButton_Click(object sender, RoutedEventArgs e) { _multiSelect = !_multiSelect; Render(); }
    private void ClearSelectionButton_Click(object sender, RoutedEventArgs e) { _selectedKeys.Clear(); Render(); }

    internal void SelectRapidTriggerKeys(IEnumerable<ushort> ids)
    {
        _selectedKeys.Clear();
        foreach (var id in ids.Where(id => _keys.ContainsKey(id))) _selectedKeys.Add(id);
        Render();
    }
    private void SelectEnabledRtButton_Click(object sender, RoutedEventArgs e)
    {
        SelectRapidTriggerKeys(_model.Draft?.Magnetic.Keys.Where(k => k.RapidTrigger?.Enabled == true)
            .Select(k => k.LogicalId) ?? []);
        KeyRtFields.StartBringIntoView();
    }
    private void SelectAllRtButton_Click(object sender, RoutedEventArgs e)
    {
        SelectRapidTriggerKeys(MagneticKeyLayout.Keys.Select(k => k.LogicalId));
        KeyRtFields.StartBringIntoView();
    }

    private void OverrideToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_rendering || _model.Draft is null || sender is not ToggleSwitch toggle) return;
        var magnetic = _model.Draft.Magnetic;
        switch (toggle.Tag as string) {
        case "g_act": magnetic.GlobalActuationMm = toggle.IsOn ? Math.Round(GlobalActSlider.Value, 1) : null; break;
        case "g_dz": magnetic.GlobalDeadzone = toggle.IsOn ?
            new ProfileDeadzone(Math.Round(GlobalTopSlider.Value, 1), Math.Round(GlobalBottomSlider.Value, 1)) : null; break;
        case "g_rt": if (!toggle.IsOn) magnetic.GlobalRapidTrigger = null; break;
        case "k_act": foreach (var id in _selectedKeys) Key(id).ActuationMm = toggle.IsOn ?
            Math.Round(KeyActSlider.Value, 1) : null; break;
        case "k_dz": foreach (var id in _selectedKeys) Key(id).Deadzone = toggle.IsOn ?
            new ProfileDeadzone(Math.Round(KeyTopSlider.Value, 1), Math.Round(KeyBottomSlider.Value, 1)) : null; break;
        case "k_rt": _model.SetRapidTriggerDraftForKeys(_selectedKeys, toggle.IsOn ? KeyRtValue() : null); break;
        }
        PruneKeys(); Render();
    }

    private ProfileRapidTrigger GlobalRtValue()
    {
        var press = Math.Round(GlobalPressSlider.Value, 1);
        var separate = GlobalSeparateToggle.IsOn;
        return new ProfileRapidTrigger(GlobalRtEnabled.IsOn, press,
            separate ? Math.Round(GlobalReleaseSlider.Value, 1) : press, separate,
            Math.Round(GlobalRtTopSlider.Value, 1), Math.Round(GlobalRtBottomSlider.Value, 1)) {
                Extensions = _model.Draft?.Magnetic.GlobalRapidTrigger?.Extensions };
    }
    private ProfileRapidTrigger KeyRtValue() => new(KeyRtEnabled.IsOn,
        Math.Round(KeyPressSlider.Value, 1), Math.Round(KeyReleaseSlider.Value, 1));

    private void ValueSlider_Changed(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (_rendering || _model.Draft is null || sender is not Slider slider) return;
        var magnetic = _model.Draft.Magnetic;
        switch (slider.Tag as string) {
        case "g_act": if (magnetic.GlobalActuationMm is not null) magnetic.GlobalActuationMm = Math.Round(e.NewValue, 1); break;
        case "g_top": case "g_bottom": if (magnetic.GlobalDeadzone is not null)
            magnetic.GlobalDeadzone = new(Math.Round(GlobalTopSlider.Value, 1), Math.Round(GlobalBottomSlider.Value, 1)); break;
        case "g_press": case "g_release": case "g_rt_top": case "g_rt_bottom":
            if (magnetic.GlobalRapidTrigger is not null) magnetic.GlobalRapidTrigger = GlobalRtValue(); break;
        case "k_act": foreach (var id in _selectedKeys) if (Key(id).ActuationMm is not null)
            Key(id).ActuationMm = Math.Round(e.NewValue, 1); break;
        case "k_top": case "k_bottom": foreach (var id in _selectedKeys) if (Key(id).Deadzone is not null)
            Key(id).Deadzone = new(Math.Round(KeyTopSlider.Value, 1), Math.Round(KeyBottomSlider.Value, 1)); break;
        case "k_press": case "k_release": _model.SetRapidTriggerDraftForKeys(
            _selectedKeys.Where(id => _model.Draft.Magnetic.Keys.Any(k => k.LogicalId == id && k.RapidTrigger is not null)),
            KeyRtValue()); break;
        case "dks_start": case "dks_end":
            if (_selectedKeys.Count == 1 && Key(_selectedKeys.First()).Dks is { Standard: false } dks)
                Key(_selectedKeys.First()).Dks = dks with { StartMm = Math.Round(DksStartSlider.Value, 1),
                    EndMm = Math.Round(DksEndSlider.Value, 1) };
            break;
        }
        Render();
    }

    private void GlobalSeparateToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_rendering || _model.Draft?.Magnetic.GlobalRapidTrigger is null) return;
        _model.Draft.Magnetic.GlobalRapidTrigger = GlobalRtValue(); Render();
    }
    private void KeyRtEnabled_Toggled(object sender, RoutedEventArgs e)
    {
        if (_rendering) return;
        _model.SetRapidTriggerDraftForKeys(_selectedKeys.Where(id =>
            _model.Draft?.Magnetic.Keys.Any(k => k.LogicalId == id && k.RapidTrigger is not null) == true), KeyRtValue());
        Render();
    }
    private void DksMode_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering || _selectedKeys.Count != 1 || DksMode.SelectedIndex < 0) return;
        var key = Key(_selectedKeys.First());
        key.Dks = DksMode.SelectedIndex switch {
            0 => null,
            1 => new ProfileDks(1, 3.6, DefaultSlots(), true),
            _ => new ProfileDks(1, 3.6, DefaultSlots())
        };
        PruneKeys(); Render();
    }
    private static List<ProfileDksSlot> DefaultSlots() => Enumerable.Range(0, 4)
        .Select(_ => new ProfileDksSlot()).ToList();
    private void DksSlot_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_rendering || _selectedKeys.Count != 1 || Key(_selectedKeys.First()).Dks is not { Standard: false } dks)
            return;
        for (var i = 0; i < _dksControls.Count; i++) {
            var row = _dksControls[i];
            var target = (row.Target.SelectedItem as ComboBoxItem)?.Tag as ushort?;
            dks.Slots[i] = new ProfileDksSlot {
                Target = target is ushort id ? new ProfileDksTarget("LogicalKey", id) : new ProfileDksTarget(),
                DownStart = TriggerValue(row.DownStart), DownEnd = TriggerValue(row.DownEnd),
                UpStart = TriggerValue(row.UpStart), UpEnd = TriggerValue(row.UpEnd)
            };
        }
        Render();
    }

    private async void RetryButton_Click(object sender, RoutedEventArgs e) { await _model.LoadAsync(); Render(); }
    private void Automation_Click(object sender, RoutedEventArgs e) => Frame.Navigate(typeof(DeviceProfileAutomationPage));
    private async void RetryApplyButton_Click(object sender, RoutedEventArgs e) { await _model.ApplyAsync(_model.Snapshot?.SelectedProfileId); Render(); }
    private async void SaveButton_Click(object sender, RoutedEventArgs e) { await _model.SaveAsync(); Render(); }
    private async void ApplyButton_Click(object sender, RoutedEventArgs e) { await _model.ApplyAsync(); Render(); }
    private void KeepDraftButton_Click(object sender, RoutedEventArgs e) { _model.KeepDraftAfterConflict(); Render(); }
    private void DiscardDraftButton_Click(object sender, RoutedEventArgs e) { _model.DiscardDraft(); Render(); }

    private async void DetailsButton_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new ContentDialog { XamlRoot = XamlRoot, Title = "应用详细信息",
            Content = new ScrollViewer { MaxHeight = 420, Content = new TextBlock {
                Text = _model.ApplyDetails ?? "暂无详细信息", TextWrapping = TextWrapping.Wrap } },
            CloseButtonText = "关闭" };
        await dialog.ShowAsync();
    }

    private async void DiagnosticsButton_Click(object sender, RoutedEventArgs e)
    {
        using var cancellation = new CancellationTokenSource();
        var model = new ProfileDiagnosticsModel(App.ProfileClient);
        var notice = new InfoBar { IsClosable = false, IsOpen = false };
        var progress = new ProgressRing { Width = 24, Height = 24, IsActive = true };
        var text = new TextBox { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.NoWrap,
            MinHeight = 160, MaxHeight = 360, HorizontalAlignment = HorizontalAlignment.Stretch };
        ScrollViewer.SetHorizontalScrollBarVisibility(text, ScrollBarVisibility.Auto);
        ScrollViewer.SetVerticalScrollBarVisibility(text, ScrollBarVisibility.Auto);
        AutomationProperties.SetName(text, "只读诊断 JSON");
        var retry = new Button { Content = "重新读取", IsEnabled = false };
        var content = new StackPanel { Spacing = 12, Children = {
            new TextBlock { Text = "仅导出后台已记录的状态。不会操作键盘、修改配置或解除安全隔离；不代表固件读取结果。",
                TextWrapping = TextWrapping.Wrap }, notice, progress, text, retry } };
        var dialog = new ContentDialog { XamlRoot = XamlRoot, Title = "高级诊断",
            Content = content, PrimaryButtonText = "复制 JSON", SecondaryButtonText = "导出文件…",
            CloseButtonText = "关闭", IsPrimaryButtonEnabled = false, IsSecondaryButtonEnabled = false };
        void Error(string message) {
            notice.Severity = InfoBarSeverity.Error; notice.Message = message; notice.IsOpen = true;
        }
        async Task ReadAsync() {
            retry.IsEnabled = false; progress.IsActive = true; progress.Visibility = Visibility.Visible;
            dialog.IsPrimaryButtonEnabled = dialog.IsSecondaryButtonEnabled = false;
            text.Text = ""; notice.IsOpen = false;
            try {
                await model.LoadAsync(cancellation.Token);
                text.Text = model.Json ?? "";
                if (model.Error.Length > 0) Error(model.Error);
                dialog.IsPrimaryButtonEnabled = dialog.IsSecondaryButtonEnabled = model.Json is not null;
            }
            catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
            finally { retry.IsEnabled = true; progress.IsActive = false; progress.Visibility = Visibility.Collapsed; }
        }
        retry.Click += async (_, _) => await ReadAsync();
        dialog.Opened += async (_, _) => await ReadAsync();
        dialog.PrimaryButtonClick += (_, args) => {
            args.Cancel = true;
            if (model.Json is null) return;
            try {
                var data = new Windows.ApplicationModel.DataTransfer.DataPackage(); data.SetText(model.Json);
                Windows.ApplicationModel.DataTransfer.Clipboard.SetContent(data);
                notice.Severity = InfoBarSeverity.Success; notice.Message = "诊断 JSON 已复制。"; notice.IsOpen = true;
            }
            catch (Exception) { Error("无法写入剪贴板，请重试或导出文件。"); }
        };
        dialog.SecondaryButtonClick += async (_, args) => {
            args.Cancel = true;
            if (model.Json is not { } json || App.MainWindowInstance is not { } window) return;
            var deferral = args.GetDeferral();
            retry.IsEnabled = false; dialog.IsPrimaryButtonEnabled = dialog.IsSecondaryButtonEnabled = false;
            try {
                var picker = new Windows.Storage.Pickers.FileSavePicker {
                    SuggestedFileName = $"Aura-diagnostics-{DateTime.Now:yyyyMMdd-HHmmss}" };
                picker.FileTypeChoices.Add("JSON 诊断文件", new List<string> { ".json" });
                WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(window));
                var file = await picker.PickSaveFileAsync();
                if (file is not null) {
                    if (!ProfileDiagnosticsModel.CanExportFileName(file.Name)) {
                        Error("不能用诊断文件覆盖正式配置，请选择其他文件名称。");
                        return;
                    }
                    await Windows.Storage.FileIO.WriteTextAsync(file, json);
                    notice.Severity = InfoBarSeverity.Success; notice.Message = "诊断文件已导出。"; notice.IsOpen = true;
                }
            }
            catch (Exception) { Error("无法导出诊断文件，请检查保存位置后重试。"); }
            finally {
                retry.IsEnabled = true; dialog.IsPrimaryButtonEnabled = dialog.IsSecondaryButtonEnabled = model.Json is not null;
                deferral.Complete();
            }
        };
        await dialog.ShowAsync();
        cancellation.Cancel();
    }

    private async Task<string?> AskNameAsync(string title, string initial)
    {
        var input = new TextBox { Header = "配置文件名称", Text = initial, MaxLength = 100 };
        var dialog = new ContentDialog { XamlRoot = XamlRoot, Title = title, Content = input,
            PrimaryButtonText = "确定", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Primary };
        if (await dialog.ShowAsync() != ContentDialogResult.Primary) return null;
        var name = input.Text.Trim();
        if (name.Length > 0) return name;
        NoticeBar.Severity = InfoBarSeverity.Warning;
        NoticeBar.Message = "名称不能为空。"; NoticeBar.IsOpen = true;
        return null;
    }

    private async Task<ContentDialogResult> ConfirmDraftAsync()
    {
        var dialog = new ContentDialog { XamlRoot = XamlRoot, Title = "保存草稿后切换？",
            Content = "当前配置有尚未保存的更改。",
            PrimaryButtonText = "保存并切换", SecondaryButtonText = "放弃更改", CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Primary };
        return await dialog.ShowAsync();
    }
    private async void CreateButton_Click(object sender, RoutedEventArgs e)
    {
        var name = await AskNameAsync("新建配置文件", "");
        if (name is null) return;
        await _model.CreateAsync(name); _selectedKeys.Clear(); Render();
    }
    private async void DuplicateButton_Click(object sender, RoutedEventArgs e)
    {
        if (_model.Draft is not { } draft) return;
        var name = await AskNameAsync("复制配置文件", draft.Name + " - 副本");
        if (name is null) return;
        await _model.DuplicateAsync(draft.Id, name); _selectedKeys.Clear(); Render();
    }
    private async void RenameButton_Click(object sender, RoutedEventArgs e)
    {
        if (_model.Draft is not { } draft) return;
        var name = await AskNameAsync("重命名配置文件", draft.Name);
        if (name is null || name == draft.Name) return;
        await _model.RenameAsync(draft.Id, name); Render();
    }
    private async void DeleteButton_Click(object sender, RoutedEventArgs e)
    {
        if (_model.Draft is not { } draft || !_model.CanDeleteEditing) return;
        var dialog = new ContentDialog { XamlRoot = XamlRoot, Title = $"删除“{draft.Name}”？",
            Content = "删除后无法从此处恢复。当前选择、正在使用及最后一个配置文件不可删除。",
            PrimaryButtonText = "删除", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Close };
        if (await dialog.ShowAsync() != ContentDialogResult.Primary) return;
        await _model.DeleteAsync(draft.Id); _selectedKeys.Clear(); Render();
    }
}
