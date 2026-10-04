using System.Text.Json;
using Aura_WinUI.Pages;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;
using Windows.Graphics.Imaging;
using Windows.Storage;
using Windows.Storage.Streams;

namespace Aura_WinUI.Validation;

// Explicit offline visual fixture. Only the opt-in manual-notice case implements
// an in-memory response; there is no daemon, persistent writer or hardware path.
// Reuses the existing validation startup guard: never starts a daemon or HID.
internal static class ProfileRtLayoutValidation
{
    internal static bool Requested => Environment.GetCommandLineArgs().Contains("--validate-profile-rt-layout") &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_UI_VALIDATION_DIR"));

    private sealed class OfflineClient(DeviceProfile profile, string gate = "unknown") : IProfileControlClient
    {
        public string Gate { get; set; } = gate;
        public HardwareSlotStatus? SlotStatus { get; set; }
        public List<DeviceProfile>? CanonicalProfiles { get; set; }
        public Guid? Selected { get; set; }
        public long Revision { get; set; } = 1;
        public bool Dirty { get; set; } = true;
        public bool AllowManualApply { get; set; }
        public bool AllowDocumentSave { get; set; }
        public int Saves { get; private set; }
        public string? Failure { get; set; }
        public int ManualRequests { get; private set; }
        public DeviceProfileAutomationDecision Decision { get; set; } = new() {
            Enabled = true, ConfigurationAvailable = true, HardwareActivationAllowed = true,
            ForegroundProcess = "cs2.exe", DecisionKind = "Match", DecisionReason = "MatchedRule",
            ResolvedProfileId = profile.Id, ActivationTarget = profile.Id,
            CoordinatorState = "Idle", ActivationOutcome = "succeeded", ConfigurationDocumentRevision = 1 };
        public Task<ProfileApiResponse> GetAutomationAsync(CancellationToken token = default) => Task.FromResult(new ProfileApiResponse {
            Status = "ok", ApiVersion = 1, DocumentRevision = 1, Profiles = [profile.Clone()],
            AutomationDecision = Decision, AutomationConfigurationAvailable = true,
            DeviceProfileAutomation = JsonSerializer.SerializeToElement(new DeviceProfileAutomationConfig { Enabled = true }, ProfileJson.Options) });
        public Task<ProfileApiResponse> GetAutomationStatusAsync(CancellationToken token = default) => GetAutomationAsync(token);
        public Task<ProfileApiResponse> ListAsync(CancellationToken token = default) => Task.FromResult(new ProfileApiResponse {
            Status = "ok", ApiVersion = 1, DocumentRevision = Revision, SelectedProfileId = Selected ?? profile.Id,
            ActiveProfileId = Selected ?? profile.Id, Profiles = CanonicalProfiles?.Select(p => p.Clone()).ToList() ?? [profile.Clone()], Dirty = Dirty,
            HardwareSlotStatus = SlotStatus, HardwareRtGate = new() { State = Gate, Source = "offline test fixture" } });
        public Task<ProfileApiResponse> GetAsync(Guid id, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> GetRuntimeAsync(CancellationToken token = default) => ListAsync(token);
        public Task<ProfileApiResponse> GetHardwareRtGateAsync(CancellationToken token = default) => ListAsync(token);
        public Task<ProfileApiResponse> GetDiagnosticsAsync(CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> CreateAsync(string name, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> DuplicateAsync(Guid id, string name, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> RenameAsync(Guid id, string name, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public async Task<ProfileApiResponse> UpdateAsync(DeviceProfile draft, long expectedRevision, CancellationToken token = default)
        {
            if (!AllowDocumentSave || expectedRevision != Revision) throw new NotSupportedException();
            Saves++; CanonicalProfiles = [draft.Clone()]; Revision++;
            var result = await ListAsync(token); result.Profile = draft.Clone(); return result;
        }
        public Task<ProfileApiResponse> DeleteAsync(Guid id, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> UpdateDefaultsAsync(ProfileMagnetic defaults, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> ActivateAsync(Guid id, ProfileActivationReason reason, long expectedRevision,
            ProfileMagnetic? temporaryOverride = null, CancellationToken token = default)
        {
            if (!AllowManualApply || reason != ProfileActivationReason.Manual || expectedRevision != Revision)
                throw new NotSupportedException();
            ManualRequests++; Selected = id; Revision++; Dirty = false;
            if (Failure is { } failure) {
                Failure = null; Dirty = true;
                return Task.FromResult(new ProfileApiResponse { Status = "ok", ApiVersion = 1,
                    DocumentRevision = Revision, SelectedProfileId = id, ActiveProfileId = null, Dirty = true,
                    Outcome = "failed", Error = failure });
            }
            Decision.ManualHold = true; Decision.ManualHoldForeground = "notepad.exe";
            Decision.DecisionReason = "ManualHold"; Decision.ResolvedProfileId = null;
            return Task.FromResult(new ProfileApiResponse { Status = "ok", ApiVersion = 1,
                DocumentRevision = Revision, SelectedProfileId = id, ActiveProfileId = id, Dirty = false,
                Outcome = "succeeded" });
        }
    }

    private static IEnumerable<DependencyObject> Descendants(DependencyObject parent)
    {
        for (var i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++) {
            var child = VisualTreeHelper.GetChild(parent, i);
            yield return child;
            foreach (var item in Descendants(child)) yield return item;
        }
    }

    private static async Task CaptureAsync(UIElement element, string directory, string name)
    {
        var bitmap = new RenderTargetBitmap(); await bitmap.RenderAsync(element);
        var buffer = await bitmap.GetPixelsAsync(); var bytes = new byte[buffer.Length];
        using (var reader = DataReader.FromBuffer(buffer)) reader.ReadBytes(bytes);
        var folder = await StorageFolder.GetFolderFromPathAsync(directory);
        var file = await folder.CreateFileAsync(name + ".png", CreationCollisionOption.ReplaceExisting);
        using var stream = await file.OpenAsync(FileAccessMode.ReadWrite);
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied,
            (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, bytes);
        await encoder.FlushAsync();
    }

    internal static async Task RunAsync(MainWindow window)
    {
        var directory = Path.GetFullPath(Environment.GetEnvironmentVariable("AURA_UI_VALIDATION_DIR")!);
        var observations = new List<object>(); string? error = null;
        try {
            Directory.CreateDirectory(directory);
            for (var i = 0; i < 100 && window.Content.XamlRoot is null; i++) await Task.Delay(50);
            if (window.Content.XamlRoot is null) throw new TimeoutException("Visual root unavailable");
            foreach (var scenario in new[] {
                (Name: "RT-hardware-on", Legacy: (bool?)null, Keys: 1, Enabled: true, Conflict: false, Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-hardware-off-narrow", Legacy: (bool?)null, Keys: 1, Enabled: true, Conflict: false, Width: 800, Theme: ElementTheme.Light),
                (Name: "RT-hardware-unknown-dark", Legacy: (bool?)null, Keys: 1, Enabled: true, Conflict: false, Width: 1200, Theme: ElementTheme.Dark),
                (Name: "RT-disabled-blocker", Legacy: (bool?)false, Keys: 0, Enabled: false, Conflict: false, Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-legacy-enabled", Legacy: (bool?)true, Keys: 0, Enabled: false, Conflict: false, Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-unmanaged", Legacy: (bool?)null, Keys: 0, Enabled: false, Conflict: false, Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-managed-disabled", Legacy: (bool?)null, Keys: 1, Enabled: false, Conflict: false, Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-normal", Legacy: (bool?)null, Keys: 1, Enabled: true, Conflict: false, Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-multi-key", Legacy: (bool?)null, Keys: 3, Enabled: true, Conflict: false, Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-all-keys", Legacy: (bool?)null, Keys: MagneticKeyLayout.Keys.Count, Enabled: true, Conflict: false, Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-dks-conflict", Legacy: (bool?)null, Keys: 1, Enabled: true, Conflict: true, Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-narrow", Legacy: (bool?)null, Keys: 3, Enabled: true, Conflict: false, Width: 800, Theme: ElementTheme.Light),
                (Name: "RT-narrow-blocker", Legacy: (bool?)false, Keys: 0, Enabled: false, Conflict: false, Width: 800, Theme: ElementTheme.Light),
                (Name: "RT-dark", Legacy: (bool?)null, Keys: 3, Enabled: true, Conflict: false, Width: 1200, Theme: ElementTheme.Dark) }) {
                var profile = new DeviceProfile { Name = "快速触发测试配置" };
                if (scenario.Legacy is bool legacyEnabled)
                    profile.Magnetic.GlobalRapidTrigger = new(legacyEnabled, 0.5, 1.5, true, 0, 0.1);
                foreach (var key in MagneticKeyLayout.Keys.Take(scenario.Keys)) profile.Magnetic.Keys.Add(new ProfileKey {
                    LogicalId = key.LogicalId, RapidTrigger = new(scenario.Enabled, 0.5, 1.5),
                    Dks = new ProfileDks(1, 3.6, Enumerable.Range(0, 4).Select(_ => new ProfileDksSlot()).ToList(), !scenario.Conflict) });
                var gate = scenario.Name == "RT-hardware-on" ? "on" : scenario.Name == "RT-hardware-off-narrow" ? "off" : "unknown";
                var offlineClient = new OfflineClient(profile, gate);
                var model = new ProfilePageModel(offlineClient); await model.LoadAsync();
                var page = new ProfilesPage(model); MainWindow.CurrentNavFrame!.Content = page;
                page.SelectRapidTriggerKeys(profile.Magnetic.Keys.Select(k => k.LogicalId));
                if (scenario.Legacy.HasValue)
                    ((CommunityToolkit.WinUI.Controls.SettingsExpander)page.FindName("LegacyRtExpander")).IsExpanded = true;
                ((FrameworkElement)window.Content).RequestedTheme = scenario.Theme;
                var scale = window.Content.XamlRoot.RasterizationScale;
                window.AppWindow.Resize(new SizeInt32((int)(scenario.Width * scale), (int)(900 * scale)));
                await Task.Delay(350); page.UpdateLayout();
                if (scenario.Legacy.HasValue) {
                    var legacy = (FrameworkElement)page.FindName("LegacyRtExpander");
                    var legacyScroll = (ScrollViewer)page.FindName("PageScroll");
                    var position = legacy.TransformToVisual(legacyScroll).TransformPoint(new Windows.Foundation.Point(0, 0));
                    legacyScroll.ChangeView(null, Math.Max(0, legacyScroll.VerticalOffset + position.Y - 40), null, true);
                    await Task.Delay(250); page.UpdateLayout();
                }
                // SettingsExpander content may have its own realized namescope.
                // Inspect the actual visual tree rather than assume page.FindName.
                FrameworkElement FindField(string name) {
                    if (page.FindName(name) is FrameworkElement element) return element;
                    FrameworkElement? FindVisual(DependencyObject node) {
                        if (node is FrameworkElement field && field.Name == name) return field;
                        for (var i = 0; i < VisualTreeHelper.GetChildrenCount(node); i++)
                            if (FindVisual(VisualTreeHelper.GetChild(node, i)) is { } found) return found;
                        return null;
                    }
                    return FindVisual(page) ?? throw new InvalidOperationException("Fixture field not realized: " + name);
                }
                var fields = FindField(scenario.Legacy.HasValue ? "GlobalRtFields" : "KeyRtFields");
                var inline = (InfoBar)FindField(scenario.Legacy.HasValue ? "RapidTriggerValidationBar" : "KeyRtValidationBar");
                var press = (Slider)FindField(scenario.Legacy.HasValue ? "GlobalPressSlider" : "KeyPressSlider");
                var valid = scenario.Legacy != false && !scenario.Conflict;
                var sensitivityEnabled = !scenario.Legacy.HasValue && scenario.Keys > 0 && scenario.Enabled;
                if (inline.IsOpen == valid || press.IsEnabled != sensitivityEnabled ||
                    ((Button)page.FindName("ApplyButton")).IsEnabled != valid ||
                    ((Button)page.FindName("RetryApplyButton")).IsEnabled != valid ||
                    model.EnabledRapidTriggerKeyCount != (scenario.Enabled ? scenario.Keys : 0))
                    throw new InvalidOperationException("RT validation/enable visuals disagree with draft");
                await CaptureAsync(window.Content, directory, scenario.Name + "-summary");
                var scroll = (ScrollViewer)page.FindName("PageScroll");
                var selectionPoint = ((FrameworkElement)page.FindName("RtKeySetSummary"))
                    .TransformToVisual(scroll).TransformPoint(new Windows.Foundation.Point(0, 0));
                scroll.ChangeView(null, Math.Max(0, scroll.VerticalOffset + selectionPoint.Y - 100), null, true);
                await Task.Delay(150); page.UpdateLayout();
                await CaptureAsync(window.Content, directory, scenario.Name + "-selection");
                var gateBar = (InfoBar)page.FindName("HardwareRtGateBar");
                var gateParent = (FrameworkElement)VisualTreeHelper.GetParent(gateBar);
                if (!gateBar.IsOpen || gateBar.Message != model.HardwareRtGateText ||
                    gateBar.ActualWidth > gateParent.ActualWidth + 1)
                    throw new InvalidOperationException("Hardware gate status overflows or misrepresents the observation");
                if (scenario.Name == "RT-hardware-on") {
                    offlineClient.Gate = "off";
                    await Task.Delay(1300); // offline UI DispatcherTimer check, not HID timing
                    if (model.HardwareRtGateState != "off" || !gateBar.Message.Contains("不会生效") ||
                        model.HasUnsavedChanges || model.EnabledRapidTriggerKeyCount != 1)
                        throw new InvalidOperationException("Live gate update mutated draft or did not refresh native status");
                    await CaptureAsync(window.Content, directory, "RT-hardware-live-off-selection");
                    offlineClient.Gate = gate;
                    await Task.Delay(1300);
                }
                var point = fields.TransformToVisual(scroll).TransformPoint(new Windows.Foundation.Point(0, 0));
                scroll.ChangeView(null, Math.Max(0, scroll.VerticalOffset + point.Y - 60), null, true);
                await Task.Delay(150); page.UpdateLayout();
                var parent = (FrameworkElement)VisualTreeHelper.GetParent(fields);
                var inlineParent = (FrameworkElement)VisualTreeHelper.GetParent(inline);
                if (fields.ActualWidth > parent.ActualWidth + 1 || inline.ActualWidth > inlineParent.ActualWidth + 1)
                    throw new InvalidOperationException("RT fields overflow available width");
                await CaptureAsync(window.Content, directory, scenario.Name);
                await CaptureAsync(parent, directory, scenario.Name + "-section");
                observations.Add(new { scenario.Name, scenario.Width, theme = scenario.Theme.ToString(),
                    page_width = page.ActualWidth, fields_width = fields.ActualWidth, parent_width = parent.ActualWidth,
                    inline_width = inline.ActualWidth, inline_height = inline.ActualHeight,
                    inline_error = inline.Message, sensitivity_enabled = press.IsEnabled,
                    managed_keys = model.ManagedRapidTriggerKeyCount, enabled_keys = model.EnabledRapidTriggerKeyCount,
                    hardware_gate = model.HardwareRtGateState, hardware_gate_text = model.HardwareRtGateText,
                    hardware_gate_width = gateBar.ActualWidth, hardware_gate_parent_width = gateParent.ActualWidth,
                    summary = model.RapidTriggerSelectionSummary, apply_enabled = valid });
            }
            foreach (var scenario in new[] {
                (Name: "RT-DKS-unknown", Conflict: false, Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-DKS-unknown-narrow", Conflict: false, Width: 800, Theme: ElementTheme.Light),
                (Name: "RT-DKS-conflict-dark", Conflict: true, Width: 1200, Theme: ElementTheme.Dark) }) {
                var profile = new DeviceProfile { Name = "标准模式与快速触发草稿" };
                profile.Magnetic.Keys.Add(new ProfileKey { LogicalId = 0x0701, RapidTrigger = new(true, 0.5, 1.5),
                    Dks = scenario.Conflict ? new(1, 3.6, Enumerable.Range(0, 4).Select(_ => new ProfileDksSlot()).ToList()) : null });
                var client = new OfflineClient(profile) { AllowManualApply = true, AllowDocumentSave = true };
                var model = new ProfilePageModel(client); await model.LoadAsync();
                var page = new ProfilesPage(model); MainWindow.CurrentNavFrame!.Content = page;
                page.SelectRapidTriggerKeys([0x0701]);
                ((FrameworkElement)window.Content).RequestedTheme = scenario.Theme;
                var scale = window.Content.XamlRoot.RasterizationScale;
                window.AppWindow.Resize(new SizeInt32((int)(scenario.Width * scale), (int)(900 * scale)));
                await Task.Delay(350); page.UpdateLayout();
                var bar = (InfoBar)page.FindName("RtDksAuthoringBar");
                var button = (Button)page.FindName("SetRtKeysStandardButton");
                var scroll = (ScrollViewer)page.FindName("PageScroll");
                var point = bar.TransformToVisual(scroll).TransformPoint(new Windows.Foundation.Point(0, 0));
                scroll.ChangeView(null, Math.Max(0, scroll.VerticalOffset + point.Y - 70), null, true);
                await Task.Delay(150); page.UpdateLayout();
                if (!bar.IsOpen || button.Visibility != Visibility.Visible || model.CanApply ||
                    bar.ActualWidth > ((FrameworkElement)VisualTreeHelper.GetParent(bar)).ActualWidth + 1)
                    throw new InvalidOperationException("Explicit Standard authoring UX missing or overflowing");
                if (await model.ApplyAsync()) throw new InvalidOperationException("Unsafe authoring was accepted");
                page.Render();
                var notice = (InfoBar)page.FindName("NoticeBar");
                var sequence = model.NoticeSequence;
                await model.LoadAsync(); page.Render(); page.Render();
                if (!notice.IsOpen || model.NoticeSequence != sequence || client.ManualRequests != 0 || client.Saves != 0)
                    throw new InvalidOperationException("Blocking notice disappeared or authoring performed an API mutation");
                await CaptureAsync(window.Content, directory, scenario.Name);
                // Explicit draft authoring seam; a non-Standard replacement in
                // the actual button handler additionally requires ContentDialog confirmation.
                model.SetSelectedKeysStandard([0x0701]); page.Render();
                if (bar.IsOpen || notice.IsOpen || !model.CanApply || !model.Draft!.Magnetic.Keys[0].RapidTrigger!.Enabled)
                    throw new InvalidOperationException("Draft Standard did not resolve only the RT/DKS blocker");
                if (!await model.SaveAsync() || client.ManualRequests != 0)
                    throw new InvalidOperationException("Offline Save was not document-only");
                page.Render(); await CaptureAsync(window.Content, directory, scenario.Name + "-resolved");
                observations.Add(new { scenario.Name, scenario.Width, theme = scenario.Theme.ToString(),
                    blocker_sequence = sequence, resolved = !bar.IsOpen, explicit_standard = model.Draft.Magnetic.Keys[0].Dks!.Standard,
                    rt_preserved = model.Draft.Magnetic.Keys[0].RapidTrigger!.Enabled, saves = client.Saves, applies = client.ManualRequests });
            }
            // Actual control events exercise OFF -> DKS and Standard -> RT
            // without a daemon or hardware transport. The old ownership toggle
            // must not exist, even when the selected key was already managed.
            foreach (var scenario in new[] {
                (Name: "RT-ownership-disabled-DKS", Width: 1200, Theme: ElementTheme.Light),
                (Name: "RT-ownership-disabled-DKS-narrow", Width: 800, Theme: ElementTheme.Dark) }) {
                var profile = new DeviceProfile { Name = "关闭快速触发后设置 DKS" };
                profile.Magnetic.Keys.Add(new ProfileKey { LogicalId = 0x0701,
                    RapidTrigger = new(true, 0.5, 1.5, true) { Extensions = new() {
                        ["continuous"] = JsonSerializer.SerializeToElement(false),
                        ["opaque_rt"] = JsonSerializer.SerializeToElement("preserved") } },
                    Dks = new(1, 3.6, Enumerable.Range(0, 4).Select(_ => new ProfileDksSlot()).ToList(), true) });
                var client = new OfflineClient(profile) { AllowDocumentSave = true };
                var model = new ProfilePageModel(client); await model.LoadAsync();
                var page = new ProfilesPage(model); MainWindow.CurrentNavFrame!.Content = page;
                page.SelectRapidTriggerKeys([0x0701]);
                ((FrameworkElement)window.Content).RequestedTheme = scenario.Theme;
                var scale = window.Content.XamlRoot.RasterizationScale;
                window.AppWindow.Resize(new SizeInt32((int)(scenario.Width * scale), (int)(900 * scale)));
                await Task.Delay(350); page.UpdateLayout();
                var toggle = (ToggleSwitch)page.FindName("KeyRtEnabled");
                var mode = (ComboBox)page.FindName("DksMode");
                var expander = (CommunityToolkit.WinUI.Controls.SettingsExpander)page.FindName("DksExpander");
                var key = model.Draft!.Magnetic.Keys.Single(); var prior = key.RapidTrigger!;
                toggle.IsOn = false; // real handler must only change Enabled
                if (key.RapidTrigger != prior with { Enabled = false })
                    throw new InvalidOperationException("RT OFF mutated ownership or parameters");
                var disabled = key.RapidTrigger;
                expander.IsExpanded = true; mode.SelectedIndex = 2;
                expander.IsExpanded = false; expander.IsExpanded = true;
                if (!ReferenceEquals(disabled, key.RapidTrigger) || !model.CanApply)
                    throw new InvalidOperationException("DKS editing removed managed disabled RT");
                var fields = (FrameworkElement)page.FindName("KeyRtFields");
                var scroll = (ScrollViewer)page.FindName("PageScroll");
                var point = fields.TransformToVisual(scroll).TransformPoint(new Windows.Foundation.Point(0, 0));
                scroll.ChangeView(null, Math.Max(0, scroll.VerticalOffset + point.Y - 40), null, true);
                await Task.Delay(150); page.UpdateLayout();
                await CaptureAsync(window.Content, directory, scenario.Name);
                mode.SelectedIndex = 1;
                if (!ReferenceEquals(disabled, key.RapidTrigger))
                    throw new InvalidOperationException("DKS Standard changed disabled RT");
                toggle.IsOn = true; var enabled = key.RapidTrigger;
                mode.SelectedIndex = 0; mode.SelectedIndex = 1;
                if (!ReferenceEquals(enabled, key.RapidTrigger) || !model.CanApply)
                    throw new InvalidOperationException("DKS editor changed enabled RT ownership");
                toggle.IsOn = false;
                if (!await model.SaveAsync() || model.Draft!.Magnetic.Keys.Single().RapidTrigger?.Enabled != false ||
                    client.ManualRequests != 0 || page.FindName("KeyRtCustom") is not null)
                    throw new InvalidOperationException("Disabled RT persistence or UI ownership boundary failed");
                page.Render(); await CaptureAsync(window.Content, directory, scenario.Name + "-standard");
                observations.Add(new { scenario.Name, scenario.Width, rt_object_retained = true,
                    enabled = false, press_mm = key.RapidTrigger!.PressMm, release_mm = key.RapidTrigger.ReleaseMm,
                    opaque_preserved = key.RapidTrigger.Extensions!["opaque_rt"].GetString(),
                    dks_editor_preserved_rt = true, saves = client.Saves, applies = client.ManualRequests });
            }
            // Real native six-second success timer, then a blocker replacing a
            // still-visible success notice. Pure polling must not hide the error.
            {
                var profile = new DeviceProfile { Name = "通知生命周期离线验证" };
                var client = new OfflineClient(profile) { AllowManualApply = true };
                var model = new ProfilePageModel(client); await model.LoadAsync();
                var page = new ProfilesPage(model); MainWindow.CurrentNavFrame!.Content = page;
                await Task.Delay(350);
                await model.ApplyAsync(); page.Render();
                var notice = (InfoBar)page.FindName("NoticeBar");
                await Task.Delay(6500);
                if (notice.IsOpen) throw new InvalidOperationException("Success no longer expires");
                await model.ApplyAsync(); page.Render(); // fresh success, timer armed
                client.Failure = "persistent quarantine (offline fixture)";
                await model.ApplyAsync(); page.Render();
                var sequence = model.NoticeSequence;
                await Task.Delay(6500); await model.LoadAsync(); page.Render();
                if (!notice.IsOpen || notice.Severity != InfoBarSeverity.Error || model.NoticeSequence != sequence)
                    throw new InvalidOperationException("Success timeout or polling dismissed a newer persistent blocker");
                await CaptureAsync(window.Content, directory, "RT-persistent-error-after-success-timeout");
                notice.IsOpen = false; await model.LoadAsync(); page.Render();
                if (notice.IsOpen) throw new InvalidOperationException("Polling resurrected a dismissed error");
                observations.Add(new { Name = "RT-notice-lifecycle", success_expired = true,
                    error_persisted = true, dismissed_stayed_closed = true, sequence });
            }
            foreach (int? observed in new int?[] {5,1,null}) {
                var profile = new DeviceProfile { Name="板载配置测试", ActivationBackend="hardware_slot", HardwareSlot=5 };
                var client = new OfflineClient(profile) { Dirty = observed != 5,
                    SlotStatus = new() { DesiredHardwareSlot=5, ObservedHardwareSlot=observed,
                        HardwareSlotMatch=observed is null ? null : observed==5, ProfileActivationBackend="hardware_slot",
                        Source="BasicInfo (offline fixture)" } };
                var model=new ProfilePageModel(client); await model.LoadAsync();
                var page=new ProfilesPage(model); MainWindow.CurrentNavFrame!.Content=page;
                ((FrameworkElement)window.Content).RequestedTheme = ElementTheme.Light;
                window.AppWindow.Resize(new SizeInt32(1200,1000));
                await Task.Delay(350);page.Render();page.UpdateLayout();
                if (((FrameworkElement)page.FindName("HostManagedEditor")).Visibility!=Visibility.Collapsed ||
                    ((ComboBox)page.FindName("HardwareSlotPicker")).Items.Count!=5 ||
                    ((InfoBar)page.FindName("HardwareSlotWarning")).IsOpen!=(observed is not null && observed!=5))
                    throw new InvalidOperationException("Hardware-slot native fixture does not separate backend/observation");
                var name="hardware-slot-"+(observed?.ToString() ?? "unknown");
                await CaptureAsync(window.Content,directory,name);
                var slotPanel=(FrameworkElement)page.FindName("HardwareSlotPanel");
                var slotScroll=(ScrollViewer)page.FindName("PageScroll");
                var slotPoint=slotPanel.TransformToVisual(slotScroll).TransformPoint(new Windows.Foundation.Point(0,0));
                slotScroll.ChangeView(null,Math.Max(0,slotScroll.VerticalOffset+slotPoint.Y-40),null,true);
                await Task.Delay(150);page.UpdateLayout();
                await CaptureAsync(window.Content,directory,name+"-detail");
                observations.Add(new {Name=name, backend="hardware_slot",target=5,actual=observed,magnetic_editor_hidden=true});
            }
            // Native Profile revision fixtures use the same production editor/model.
            // No actuator or document writer exists in OfflineClient.
            foreach (var scenario in new[] {
                (Name: "draft-automation-rebase", Conflict: false, Width: 1200, Theme: ElementTheme.Light),
                (Name: "draft-automation-rebase-narrow", Conflict: false, Width: 800, Theme: ElementTheme.Dark),
                (Name: "draft-content-conflict", Conflict: true, Width: 1200, Theme: ElementTheme.Light) }) {
                var editing = new DeviceProfile { Name = "test2 · 正在编辑" };
                var automatic = new DeviceProfile { Name = "test · 自动选择" };
                var client = new OfflineClient(editing) { CanonicalProfiles = [editing, automatic], Selected = editing.Id, Dirty = false };
                var model = new ProfilePageModel(client); await model.LoadAsync();
                model.Draft!.Magnetic.GlobalActuationMm = 1.7;
                client.Selected = automatic.Id; client.Revision++;
                if (scenario.Conflict) editing.Name = "test2 · 外部修改";
                await model.LoadAsync();
                var page = new ProfilesPage(model); MainWindow.CurrentNavFrame!.Content = page;
                ((FrameworkElement)window.Content).RequestedTheme = scenario.Theme;
                var scale = window.Content.XamlRoot.RasterizationScale;
                window.AppWindow.Resize(new SizeInt32((int)(scenario.Width * scale), (int)(900 * scale)));
                await Task.Delay(350); page.Render(); page.UpdateLayout();
                var bar = (InfoBar)page.FindName("ConflictBar");
                if (model.HasConflict != scenario.Conflict || bar.IsOpen != scenario.Conflict ||
                    model.EditingId != editing.Id || model.Snapshot?.SelectedProfileId != automatic.Id || model.Snapshot?.ActiveProfileId != automatic.Id ||
                    ((TextBlock)page.FindName("HeroName")).Text != automatic.Name ||
                    model.Draft.Magnetic.GlobalActuationMm != 1.7 || !model.HasUnsavedChanges)
                    throw new InvalidOperationException("Native draft rebase/selection/conflict fixture disagrees with editor state");
                await CaptureAsync(window.Content, directory, scenario.Name);
                observations.Add(new { scenario.Name, scenario.Width, conflict = bar.IsOpen,
                    selected = model.CurrentName, edited = model.EditingName, draft_actuation = 1.7,
                    base_revision = model.BaseDocumentRevision, preserved = model.HasUnsavedChanges });
            }
            // One fake Manual Apply; real native notice expires/dismisses once.
            // A return to the same external context is suppressed, never another Apply.
            {
                var a = new DeviceProfile { Name = "test · 自动配置" };
                var b = new DeviceProfile { Name = "test2 · 手动配置" };
                var client = new OfflineClient(a) { CanonicalProfiles = [a, b], Selected = a.Id,
                    Dirty = false, AllowManualApply = true };
                client.Decision.ForegroundProcess = "aura.exe";
                var model = new ProfilePageModel(client); await model.LoadAsync(); model.Edit(b.Id);
                var page = new ProfilesPage(model); MainWindow.CurrentNavFrame!.Content = page;
                window.AppWindow.Resize(new SizeInt32(1200, 900));
                await Task.Delay(350);
                if (!await model.ApplyAsync()) throw new InvalidOperationException("Offline manual Apply rejected");
                page.Render(); page.UpdateLayout();
                var notice = (InfoBar)page.FindName("NoticeBar");
                if (!notice.IsOpen || model.HasConflict || model.CurrentName != b.Name)
                    throw new InvalidOperationException("Manual hold native success state is incorrect");
                await CaptureAsync(window.Content, directory, "manual-override-aura-success");
                client.Decision.ForegroundProcess = "notepad.exe";
                await model.LoadAsync(); page.Render();
                if (!notice.IsOpen || client.ManualRequests != 1 || model.Snapshot?.ActiveProfileId != b.Id)
                    throw new InvalidOperationException("Suppressed return changed manual success state");
                await CaptureAsync(window.Content, directory, "manual-override-return-notepad");
                var sequence = model.NoticeSequence;
                notice.IsOpen = false; // deterministic expiry/dismissal, no delay workaround
                client.Revision++;
                await model.LoadAsync(); page.Render(); page.Render();
                if (notice.IsOpen || model.NoticeSequence != sequence || client.ManualRequests != 1 || model.HasConflict)
                    throw new InvalidOperationException("Poll resurrected a consumed manual notification");
                await CaptureAsync(window.Content, directory, "manual-override-consumed-notification");
                observations.Add(new { Name = "manual-override-notification", selected = model.CurrentName,
                    active = model.Snapshot.ActiveProfileId, manual_hold = client.Decision.ManualHold,
                    anchor = client.Decision.ManualHoldForeground, requests = client.ManualRequests,
                    notice_sequence = sequence, reappeared = notice.IsOpen, conflict = model.HasConflict });
            }
            foreach (var scenario in new[] {
                (Name: "P4B-applied", State: "Idle", Outcome: "succeeded", Width: 1200, Theme: ElementTheme.Light),
                (Name: "P4B-applying-narrow", State: "Activating", Outcome: "", Width: 800, Theme: ElementTheme.Light),
                (Name: "P4B-deferred-dark", State: "Deferred", Outcome: "deferred", Width: 1200, Theme: ElementTheme.Dark),
                (Name: "P4B-quarantine-narrow", State: "Blocked", Outcome: "blocked", Width: 800, Theme: ElementTheme.Dark) }) {
                var profile = new DeviceProfile { Name = "自动切换测试配置" };
                var client = new OfflineClient(profile);
                client.Decision.CoordinatorState = scenario.State;
                client.Decision.ActivationOutcome = scenario.Outcome;
                client.Decision.HardwareBlockReasons = scenario.Name.Contains("quarantine") ? ["SafetyQuarantined"] : [];
                var model = new DeviceProfileAutomationModel(client); await model.RefreshAsync();
                var page = new DeviceProfileAutomationPage(model); MainWindow.CurrentNavFrame!.Content = page;
                ((FrameworkElement)window.Content).RequestedTheme = scenario.Theme;
                var scale = window.Content.XamlRoot.RasterizationScale;
                window.AppWindow.Resize(new SizeInt32((int)(scenario.Width * scale), (int)(900 * scale)));
                await Task.Delay(350); page.UpdateLayout();
                var bar = (InfoBar)page.FindName("ActivationBar");
                if (bar.ActualWidth > ((FrameworkElement)VisualTreeHelper.GetParent(bar)).ActualWidth + 1 ||
                    bar.Message != model.ActivationStatus)
                    throw new InvalidOperationException("P4B native status clips or misrepresents the coordinator");
                await CaptureAsync(window.Content, directory, scenario.Name);
                observations.Add(new { scenario.Name, scenario.Width, theme = scenario.Theme.ToString(),
                    activation_state = scenario.State, message = bar.Message });
            }
        } catch (Exception ex) { error = ex.ToString(); }
        finally {
            Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory, "profile-rt-layout-results.json"),
                JsonSerializer.Serialize(new { source = "native XAML, offline mock; no hardware state", error, observations },
                    new JsonSerializerOptions { WriteIndented = true }));
            await App.RequestExit();
        }
    }
}
