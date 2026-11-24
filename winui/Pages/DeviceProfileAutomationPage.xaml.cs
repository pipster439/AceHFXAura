using Aura_WinUI.Common;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Navigation;
using Microsoft.UI.Xaml.Media;
using System.Globalization;

namespace Aura_WinUI.Pages;

public sealed partial class DeviceProfileAutomationPage : Page
{
    private readonly DeviceProfileAutomationModel _model;
    private readonly DispatcherTimer _timer = new() { Interval = TimeSpan.FromSeconds(1) };
    private CancellationTokenSource? _lifetime;
    private Task? _refreshTask;
    private bool _rendering, _dialogOpen;
    public DeviceProfileAutomationPage() : this(new DeviceProfileAutomationModel(App.ProfileClient)) { }
    internal DeviceProfileAutomationPage(DeviceProfileAutomationModel model)
    {
        _model = model;
        InitializeComponent();
        NavigationCacheMode = NavigationCacheMode.Required; // retain unsubmitted drafts across navigation
        PageLayout.Attach(this, PageScroll, PageContent, width => {
            Grid.SetColumn(CommandActions, width < 660 ? 0 : 1);
            Grid.SetRow(CommandActions, width < 660 ? 1 : 0);
            FallbackPicker.MaxWidth = Math.Min(340, Math.Max(160, width - 36));
        }, maxWidth: 1120);
        PageLayout.Notification(this, NoticeBar);
        Loaded += async (_, _) => {
            _lifetime = new(); await RefreshAsync(); if (IsLoaded) _timer.Start();
        };
        Unloaded += (_, _) => { _timer.Stop(); _lifetime?.Cancel(); _lifetime?.Dispose(); _lifetime = null; };
        _timer.Tick += async (_, _) => { if (!_dialogOpen && !FallbackPicker.IsDropDownOpen) await RefreshStatusAsync(); };
        FallbackPicker.DropDownClosed += (_, _) => { if (!_dialogOpen) Render(); };
        Render();
    }
    private async Task RefreshStatusAsync()
    {
        if (_model.IsBusy || _refreshTask is not null || _lifetime is null) return;
        if (!_model.IsAvailable) { await RefreshAsync(); return; }
        try {
            _refreshTask = _model.RefreshDecisionAsync(_lifetime.Token);
            await _refreshTask;
            if (!IsLoaded || _dialogOpen || FallbackPicker.IsDropDownOpen) return;
            if (_model.IsAvailable) RenderStatus(); else Render();
            if (_model.NeedsConfigurationRefresh && _model.Decision?.CoordinatorState is not ("Activating" or "Preflight")) {
                _refreshTask = null;
                await RefreshAsync();
            }
        } catch (OperationCanceledException) { }
        finally { _refreshTask = null; }
    }
    private void RenderStatus()
    {
        ForegroundText.Text = _model.Foreground; MatchedRuleText.Text = _model.MatchedRule;
        TargetText.Text = _model.WouldSelect; PreviewStatusText.Text = _model.PreviewStatus;
        ActivationBar.Message = _model.ActivationStatus;
        ActivationStatusText.Text = _model.ActivationStatus;
        ActivationBar.Severity = _model.Decision?.ActivationOutcome == "failed" ? InfoBarSeverity.Error :
            _model.Decision?.CoordinatorState is "Deferred" or "Blocked" ? InfoBarSeverity.Warning : InfoBarSeverity.Informational;
        PreviewExpander.Description = _model.PreviewStatus;
        PreviewSummary.Text = $"当前前台：{_model.Foreground}\n将选择：{_model.WouldSelect}";
    }
    private async Task RefreshAsync()
    {
        if (_model.IsBusy || _lifetime is null) return;
        try {
            var task = _model.RefreshAsync(_lifetime.Token);
            _refreshTask = task;
            // Background preview reads must not disable editors or dismiss an
            // open picker once loaded. Loading/unavailable still has feedback.
            if (!_model.IsAvailable) Render();
            await task; if (IsLoaded && !_dialogOpen && !FallbackPicker.IsDropDownOpen) Render();
        }
        catch (OperationCanceledException) { } // page lifetime ended; no late UI update
        finally { _refreshTask = null; }
    }
    private void Render()
    {
        _rendering = true;
        try {
            BusyRing.IsActive = _model.IsBusy; BusyRing.Visibility = _model.IsBusy ? Visibility.Visible : Visibility.Collapsed;
            EditorSurface.IsEnabled = _model.IsAvailable && !_model.IsBusy && !_model.HasConflict;
            SaveButton.IsEnabled = _model.CanSave;
            LightingCompatibilityBar.IsOpen = _model.IsAvailable && !_model.SupportsIndependentLighting;
            LightingCompatibilityBar.Message = _model.LightingCompatibilityNotice;
            EnabledToggle.IsOn = _model.Draft?.Enabled ?? false;
            var choices = _model.TargetChoices(_model.Draft?.FallbackProfileId, true);
            if (FallbackPicker.ItemsSource is not IReadOnlyList<DeviceProfileTargetChoice> previous || !previous.SequenceEqual(choices))
                FallbackPicker.ItemsSource = choices;
            else
                choices = previous; // selected item must belong to the retained source
            FallbackPicker.SelectedItem = choices.FirstOrDefault(p => p.Id == _model.Draft?.FallbackProfileId);
            var rows = _model.Rules;
            // Avoid rebuilding identical repeater contents on every preview tick.
            if (RulesRepeater.ItemsSource is not IReadOnlyList<DeviceProfileRuleRow> prior || !prior.SequenceEqual(rows))
                RulesRepeater.ItemsSource = rows;
            EmptyRulesText.Visibility = rows.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
            AddButton.IsEnabled = _model.IsAvailable;
            ConflictBar.IsOpen = _model.HasConflict; DiscardButton.Visibility = _model.HasConflict ? Visibility.Visible : Visibility.Collapsed;
            DraftText.Text = _model.HasUnsavedChanges ? "有未保存的规则更改" : _model.IsAvailable ? "规则已保存" : "等待后台服务提供规则";
            RenderStatus();
            NoticeBar.IsOpen = _model.Notice.Length > 0; NoticeBar.Message = _model.Notice;
            NoticeBar.Severity = _model.NoticeKind switch {
                ProfileNoticeKind.Error => InfoBarSeverity.Error, ProfileNoticeKind.Warning => InfoBarSeverity.Warning,
                ProfileNoticeKind.Success => InfoBarSeverity.Success, _ => InfoBarSeverity.Informational };
        } finally { _rendering = false; }
    }
    private void Back_Click(object sender, RoutedEventArgs e) { if (Frame.CanGoBack) Frame.GoBack(); }
    private async void Refresh_Click(object sender, RoutedEventArgs e) => await RefreshAsync();
    private async void Save_Click(object sender, RoutedEventArgs e)
    {
        if (_lifetime is null) return;
        try {
            // A preview read must not swallow an explicit Save click.
            if (_refreshTask is { } refresh) await refresh;
            if (_lifetime is null) return;
            var task = _model.SaveAsync(_lifetime.Token); Render(); await task; if (IsLoaded) Render();
        }
        catch (OperationCanceledException) { }
    }
    private void Enabled_Toggled(object sender, RoutedEventArgs e) { if (!_rendering) { _model.SetEnabled(EnabledToggle.IsOn); Render(); } }
    private void Fallback_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!_rendering && FallbackPicker.SelectedItem is DeviceProfileTargetChoice choice) { _model.SetFallback(choice.Id); Render(); }
    }
    private void KeepDraft_Click(object sender, RoutedEventArgs e) { _model.KeepDraftAfterConflict(); Render(); }
    private void Discard_Click(object sender, RoutedEventArgs e) { _model.DiscardDraft(); Render(); }
    private async void Add_Click(object sender, RoutedEventArgs e) => await EditRuleAsync(null);
    private async void Edit_Click(object sender, RoutedEventArgs e) { if (sender is Button { Tag: Guid id }) await EditRuleAsync(id); }
    private async Task EditRuleAsync(Guid? id)
    {
        if (_dialogOpen || !_model.IsAvailable || _model.HasConflict) return;
        _dialogOpen = true;
        try {
            if (_refreshTask is { } refresh) await refresh;
            if (!IsLoaded || !_model.IsAvailable || _model.IsBusy || _model.HasConflict) return;
            var rule = _model.Draft?.Bindings.FirstOrDefault(r => r.RuleId == id);
            var process = new TextBox { Header = "程序名称", Text = rule?.ProcessName ?? "", PlaceholderText = "例如 cs2.exe；也可粘贴程序路径" };
            AutomationProperties.SetAutomationId(process, "DeviceAutomationProcessInput");
            var priority = new NumberBox { Header = "优先级",
                Value = rule?.Priority ?? 100, SmallChange = 1, SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact,
                ValidationMode = NumberBoxValidationMode.Disabled };
            var enabled = new ToggleSwitch { Header = "启用规则", IsOn = rule?.Enabled ?? true, OnContent = "开启", OffContent = "关闭" };
            AutomationProperties.SetAutomationId(priority, "DeviceAutomationRulePriority");
            AutomationProperties.SetAutomationId(enabled, "DeviceAutomationRuleEnabled");
            var error = new InfoBar { IsClosable = false, Severity = InfoBarSeverity.Warning };
            AutomationProperties.SetAutomationId(error, "DeviceAutomationRuleValidation");
            var form = new StackPanel { Spacing = 12, MinWidth = 240, MaxWidth = 420 };
            form.Children.Add(process); form.Children.Add(priority);
            form.Children.Add(new TextBlock { Text = "数值越大，多个规则同时匹配时优先级越高。", TextWrapping = TextWrapping.Wrap });
            form.Children.Add(enabled); form.Children.Add(error);
            var dialog = new ContentDialog { XamlRoot = XamlRoot, RequestedTheme = ActualTheme, Title = id is null ? "添加程序规则" : "编辑程序规则",
                Content = new ScrollViewer { Content = form, MaxHeight = 480, VerticalScrollBarVisibility = ScrollBarVisibility.Auto }, PrimaryButtonText = "保留到草稿", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Primary };
            var target = AddTargetEditor(form, dialog, rule?.ProfileId, false);
            dialog.PrimaryButtonClick += (_, args) => {
                try {
                    // NumberBox.Value can retain its last value while malformed
                    // text is being edited. Validate the actual text before commit.
                    var enteredText = FindPriorityInput(priority)?.Text;
                    if (!double.TryParse(enteredText, NumberStyles.Float | NumberStyles.AllowThousands,
                        CultureInfo.CurrentCulture, out var enteredPriority))
                        throw new InvalidDataException("优先级必须是有效整数。");
                    _model.UpsertRule(id, process.Text, (target.SelectedItem as DeviceProfileTargetChoice)?.Id, enteredPriority, enabled.IsOn);
                }
                catch (InvalidDataException ex) { args.Cancel = true; error.Message = ex.Message; error.IsOpen = true; }
            };
            await dialog.ShowAsync(); Render();
        } catch (OperationCanceledException) { }
        finally { _dialogOpen = false; }
    }
    // Both rule and fallback dialogs use this same picker/create flow. Polling is
    // suspended for their lifetime; controls and entered text survive request errors.
    private ComboBox AddTargetEditor(StackPanel form, ContentDialog dialog, Guid? current, bool includeNone)
    {
        var section = new StackPanel { Spacing = 12 };
        form.Children.Insert(Math.Min(1, form.Children.Count), section);
        var slots = new ComboBox { Header = "磁轴配置来源 / 板载槽位", HorizontalAlignment = HorizontalAlignment.Stretch,
            ItemsSource = new[] { "全部配置", "板载槽位 1", "板载槽位 2", "板载槽位 3", "板载槽位 4", "板载槽位 5" }, SelectedIndex = 0 };
        AutomationProperties.SetAutomationId(slots, "DeviceAutomationSlot");
        AutomationProperties.SetName(slots, "磁轴配置来源或板载槽位 1 至 5");
        var target = new ComboBox { Header = "目标配置文件", DisplayMemberPath = "Label", HorizontalAlignment = HorizontalAlignment.Stretch };
        AutomationProperties.SetAutomationId(target, "DeviceAutomationRuleTarget");
        AutomationProperties.SetName(target, "自动切换目标配置文件");
        var empty = new TextBlock { TextWrapping = TextWrapping.Wrap };
        void Populate(Guid? selected)
        {
            var choices = slots.SelectedIndex > 0 ? _model.SlotChoices(slots.SelectedIndex) : _model.TargetChoices(selected, includeNone);
            target.ItemsSource = choices;
            target.SelectedItem = choices.FirstOrDefault(p => p.Id == selected);
            empty.Text = choices.Count == 0 ? "此槽位尚无 Profile，请展开下方创建板载配置。" : "请选择现有 Profile；同一槽位的配置按各自 ID 保存。";
        }
        slots.SelectionChanged += (_, _) => Populate(null);
        section.Children.Add(slots); section.Children.Add(target); section.Children.Add(empty);
        var name = new TextBox { Header = "新配置名称", PlaceholderText = "例如 CS2 板载" };
        AutomationProperties.SetName(name, "新板载配置名称");
        AutomationProperties.SetAutomationId(name, "DeviceAutomationCreateName");
        var createSlot = new ComboBox { Header = "新配置使用的板载槽位", ItemsSource = new[] { 1, 2, 3, 4, 5 }, SelectedIndex = 0 };
        AutomationProperties.SetName(createSlot, "新配置板载槽位 1 至 5");
        AutomationProperties.SetAutomationId(createSlot, "DeviceAutomationCreateSlot");
        slots.SelectionChanged += (_, _) => { if (slots.SelectedIndex > 0) createSlot.SelectedIndex = slots.SelectedIndex - 1; };
        var lighting = new ComboBox { Header = "灯光控制来源", ItemsSource = new[] { "板载固件灯光", "Aura 动态灯光" }, SelectedIndex = 0 };
        AutomationProperties.SetName(lighting, "新配置灯光控制来源");
        AutomationProperties.SetAutomationId(lighting, "DeviceAutomationCreateLighting");
        var create = new Button { Content = "创建板载配置并选中", IsEnabled = _model.CanCreateHardware };
        AutomationProperties.SetAutomationId(create, "DeviceAutomationCreateConfirm");
        var message = new InfoBar { IsOpen = true, IsClosable = false, Message = _model.HardwareCreateHint };
        AutomationProperties.SetAutomationId(message, "DeviceAutomationCreateResult");
        var createForm = new StackPanel { Spacing = 12 };
        foreach (var element in new UIElement[] { name, createSlot, lighting, create, message }) createForm.Children.Add(element);
        var createExpander = new Expander { Header = "创建板载配置", Content = createForm,
            HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch };
        AutomationProperties.SetAutomationId(createExpander, "DeviceAutomationCreateExpander");
        section.Children.Add(createExpander);
        bool creating = false;
        dialog.Closing += (_, args) => { if (creating) args.Cancel = true; };
        create.Click += async (_, _) => {
            if (creating || _lifetime is null) return;
            creating = true; create.IsEnabled = false; dialog.IsPrimaryButtonEnabled = false;
            name.IsEnabled = createSlot.IsEnabled = lighting.IsEnabled = slots.IsEnabled = target.IsEnabled = false;
            try {
                var owner = lighting.SelectedIndex == 1 ? "AuraDirectRgb" : "FirmwareBank";
                var id = await _model.CreateHardwareAsync(name.Text, createSlot.SelectedIndex + 1, owner, _lifetime.Token);
                if (!IsLoaded) return;
                if (id is not null) { slots.SelectedIndex = 0; Populate(id); }
                message.Message = _model.Notice;
                message.Severity = id is not null ? InfoBarSeverity.Informational : InfoBarSeverity.Warning;
            } finally {
                creating = false; create.IsEnabled = _model.CanCreateHardware;
                dialog.IsPrimaryButtonEnabled = _model.IsAvailable && !_model.HasConflict;
                name.IsEnabled = createSlot.IsEnabled = lighting.IsEnabled = slots.IsEnabled = target.IsEnabled = true;
            }
        };
        Populate(current);
        return target;
    }
    private async void FallbackTarget_Click(object sender, RoutedEventArgs e)
    {
        if (_dialogOpen || !_model.IsAvailable || _model.HasConflict) return;
        _dialogOpen = true;
        try {
            if (_refreshTask is { } refresh) await refresh;
            if (!IsLoaded || !_model.IsAvailable || _model.IsBusy || _model.HasConflict) return;
            var form = new StackPanel { Spacing = 12, MinWidth = 240, MaxWidth = 420 };
            var dialog = new ContentDialog { XamlRoot = XamlRoot, RequestedTheme = ActualTheme,
                Title = "选择后备配置", PrimaryButtonText = "保留到草稿", CloseButtonText = "取消", Content = new ScrollViewer { Content = form, MaxHeight = 480, VerticalScrollBarVisibility = ScrollBarVisibility.Auto } };
            var target = AddTargetEditor(form, dialog, _model.Draft?.FallbackProfileId, true);
            var error = new InfoBar { IsClosable = false, Severity = InfoBarSeverity.Warning };
            form.Children.Add(error);
            dialog.PrimaryButtonClick += (_, args) => {
                if (target.SelectedItem is not DeviceProfileTargetChoice choice ||
                    choice.Id is { } id && !_model.Profiles.Any(p => p.Id == id)) {
                    args.Cancel = true; error.Message = "请选择一个仍然存在的配置，或不执行任何操作。"; error.IsOpen = true; return;
                }
                _model.SetFallback(choice.Id);
            };
            await dialog.ShowAsync(); if (IsLoaded) Render();
        } catch (OperationCanceledException) { }
        finally { _dialogOpen = false; }
    }
    // NumberBox.Text only reflects committed numeric text in some WinAppSDK
    // versions. Inspect its visible native input so malformed edits cannot reuse
    // an old numeric value when the dialog is submitted.
    private static TextBox? FindPriorityInput(DependencyObject parent)
    {
        if (parent is TextBox input) return input;
        for (var i = 0; i < VisualTreeHelper.GetChildrenCount(parent); ++i)
            if (FindPriorityInput(VisualTreeHelper.GetChild(parent, i)) is { } child) return child;
        return null;
    }
    private async void Delete_Click(object sender, RoutedEventArgs e)
    {
        if (_dialogOpen || sender is not Button { Tag: Guid id }) return;
        _dialogOpen = true;
        try {
            var rule = _model.Draft?.Bindings.FirstOrDefault(r => r.RuleId == id);
            var dialog = new ContentDialog { XamlRoot = XamlRoot, RequestedTheme = ActualTheme, Title = "删除程序规则？",
                Content = $"删除“{rule?.ProcessName}”的匹配规则。保存规则后生效。", PrimaryButtonText = "删除", CloseButtonText = "取消" };
            if (await dialog.ShowAsync() == ContentDialogResult.Primary) _model.DeleteRule(id);
            Render();
        } finally { _dialogOpen = false; }
    }
}
