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

// Explicit offline visual fixture. Mutations/activation have no implementation.
// Reuses the existing validation startup guard: never starts a daemon or HID.
internal static class ProfileRtLayoutValidation
{
    internal static bool Requested => Environment.GetCommandLineArgs().Contains("--validate-profile-rt-layout") &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_UI_VALIDATION_DIR"));

    private sealed class OfflineClient(DeviceProfile profile, string gate = "unknown") : IProfileControlClient
    {
        public string Gate { get; set; } = gate;
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
            Status = "ok", ApiVersion = 1, DocumentRevision = 1, SelectedProfileId = profile.Id,
            Profiles = [profile.Clone()], Dirty = true,
            HardwareRtGate = new() { State = Gate, Source = "offline test fixture" } });
        public Task<ProfileApiResponse> GetAsync(Guid id, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> GetRuntimeAsync(CancellationToken token = default) => ListAsync(token);
        public Task<ProfileApiResponse> GetHardwareRtGateAsync(CancellationToken token = default) => ListAsync(token);
        public Task<ProfileApiResponse> GetDiagnosticsAsync(CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> CreateAsync(string name, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> DuplicateAsync(Guid id, string name, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> RenameAsync(Guid id, string name, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> UpdateAsync(DeviceProfile profile, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> DeleteAsync(Guid id, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> UpdateDefaultsAsync(ProfileMagnetic defaults, long expectedRevision, CancellationToken token = default) => throw new NotSupportedException();
        public Task<ProfileApiResponse> ActivateAsync(Guid id, ProfileActivationReason reason, long expectedRevision,
            ProfileMagnetic? temporaryOverride = null, CancellationToken token = default) => throw new NotSupportedException();
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
                    Dks = scenario.Conflict ? new ProfileDks(1, 3.6, Enumerable.Range(0, 4).Select(_ => new ProfileDksSlot()).ToList()) : null });
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
