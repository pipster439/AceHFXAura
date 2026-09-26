using System.Text.Json;
using Aura_WinUI.Pages;
using Aura_WinUI.Services;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;
using Windows.Graphics.Imaging;
using Windows.Storage;
using Windows.Storage.Streams;

namespace Aura_WinUI.Validation;

// Opt-in visual validation never starts the daemon or invokes a magnetic API.
internal static class MagneticLayoutValidation
{
    internal static bool Requested => Environment.GetCommandLineArgs().Contains("--validate-magnetic-layout") &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_UI_VALIDATION_DIR"));

    private static IEnumerable<DependencyObject> Descendants(DependencyObject parent)
    {
        for (var i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            yield return child;
            foreach (var descendant in Descendants(child)) yield return descendant;
        }
    }

    private static async Task CaptureAsync(MainWindow window, string directory, string name)
    {
        var bitmap = new RenderTargetBitmap();
        await bitmap.RenderAsync(window.Content);
        var buffer = await bitmap.GetPixelsAsync();
        var bytes = new byte[buffer.Length];
        using (var reader = DataReader.FromBuffer(buffer)) reader.ReadBytes(bytes);
        var folder = await StorageFolder.GetFolderFromPathAsync(directory);
        var file = await folder.CreateFileAsync(name + ".png", CreationCollisionOption.ReplaceExisting);
        using var stream = await file.OpenAsync(FileAccessMode.ReadWrite);
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied,
            (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, bytes);
        await encoder.FlushAsync();
    }

    private sealed class OfflineValidationClient : IMagneticControlClient
    {
        public MagneticStatus CurrentStatus { get; set; } = new()
        {
            Status = "ok",
            ApiVersion = 1,
            Health = "Clean",
            Available = true,
            HostProfile = new MagneticHostProfileState
            {
                GlobalActuation = new MagneticKnownRaw { Known = true, Raw = 10, Source = "HostProfile" },
                GlobalRtPress = new MagneticKnownRaw { Known = true, Raw = 4, Source = "HostProfile" },
                GlobalRtRelease = new MagneticKnownRaw { Known = true, Raw = 2, Source = "HostProfile" },
                GlobalDeadzoneTop = new MagneticKnownRaw { Known = true, Raw = 0, Source = "HostProfile" },
                GlobalDeadzoneBottom = new MagneticKnownRaw { Known = true, Raw = 1, Source = "HostProfile" },
                PerKeyRtListKnown = true
            },
            SpeedTap = new MagneticSpeedTapState
            {
                Master = new MagneticKnownBool { Known = true, Value = true, Source = "HostProfile" },
                SavedProfilePairsKnown = true,
                PairKnowledge = "已同步活动配置"
            },
            StaticAnalogEffect = new MagneticKnownBool { Known = true, Value = true, Source = "HostProfile" }
        };

        public Task<MagneticStatus> GetStatusAsync() => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> SetActuationAsync(ushort logicalId, double mm) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> SetRapidTriggerAsync(ushort logicalId, double pressMm, double releaseMm, bool resolveDks) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> DisableRapidTriggerAsync(ushort logicalId) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> SetDeadzoneAsync(ushort logicalId, double topMm, double bottomMm) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> ResetAllDeadzoneAsync() => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> SetDksAsync(ushort logicalId, double startMm, double endMm, IReadOnlyList<MagneticDksSlot> slots, bool resolveRt) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> RestoreDksStandardAsync(ushort logicalId) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> SetSpeedTapPairAsync(ushort key1, ushort key2) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> DisableSpeedTapPairAsync(ushort key1, ushort key2) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> SetSpeedTapMasterAsync(bool enabled) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> ResetSpeedTapToProfileAsync() => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> SetStaticAnalogEffectAsync(bool enabled) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> SetGlobalActuationAsync(double mm) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> SetGlobalDeadzoneAsync(double topMm, double bottomMm) => Task.FromResult(CurrentStatus);
        public Task<MagneticStatus> SetGlobalRapidTriggerAsync(double pressMm, double releaseMm, double topMm, double bottomMm, bool separateMode) => Task.FromResult(CurrentStatus);
    }

    internal static async Task RunAsync(MainWindow window)
    {
        var directory = Path.GetFullPath(Environment.GetEnvironmentVariable("AURA_UI_VALIDATION_DIR")!);
        var observations = new List<object>();
        string? error = null;
        try
        {
            Directory.CreateDirectory(directory);
            for (var i = 0; i < 100 && window.Content.XamlRoot == null; i++) await Task.Delay(50);
            if (window.Content.XamlRoot == null) throw new TimeoutException("WinUI visual root unavailable");

            window.NavigateTo(typeof(MagneticSwitchPage));
            for (var i = 0; i < 100 && MainWindow.CurrentNavFrame?.Content is not MagneticSwitchPage; i++) await Task.Delay(50);
            await Task.Delay(350);

            var validationClient = new OfflineValidationClient();
            var validationModel = new MagneticSettingsModel(validationClient);
            await validationModel.RefreshAsync();

            var initialPage = (MagneticSwitchPage)MainWindow.CurrentNavFrame!.Content;
            initialPage.SetModelForValidation(validationModel);
            await Task.Delay(100);

            // 1. Breakpoint & Theme Matrix (Light & Dark x Narrow, Standard, 4K)
            foreach (var theme in new[] { ElementTheme.Light, ElementTheme.Dark })
            {
                ((FrameworkElement)window.Content).RequestedTheme = theme;
                foreach (var size in new[] { (600, 500), (1060, 720), (3840, 2160) })
                {
                    var presenter = (OverlappedPresenter)window.AppWindow.Presenter;
                    presenter.Restore();
                    var scale = window.Content.XamlRoot.RasterizationScale;
                    window.AppWindow.Resize(new SizeInt32((int)(size.Item1 * scale), (int)(size.Item2 * scale)));
                    await Task.Delay(350);
                    var page = (MagneticSwitchPage)MainWindow.CurrentNavFrame!.Content;
                    var scroll = Descendants(page).OfType<ScrollViewer>().First(view => view.Name == "PageScroll");
                    var keyboard = Descendants(page).OfType<StackPanel>().First(panel => panel.Name == "KeyboardRows");
                    var keyboardScroll = Descendants(page).OfType<ScrollViewer>().First(view => view.Name == "KeyboardViewport");
                    var buttons = keyboard.Children.OfType<Canvas>().SelectMany(row => row.Children.OfType<Button>()).ToArray();
                    if (buttons.Length != 68) throw new InvalidOperationException($"Rendered {buttons.Length} keys, expected 68");
                    if (scroll.ScrollableWidth > 1) throw new InvalidOperationException("Page has horizontal overflow");
                    if (size.Item1 >= 1060 && keyboardScroll.ScrollableWidth > 1)
                        throw new InvalidOperationException("Keyboard clips keys at desktop width");
                    var name = $"{theme}-{size.Item1}x{size.Item2}";
                    await CaptureAsync(window, directory, name);
                    observations.Add(new
                    {
                        theme = theme.ToString(),
                        requested_width = size.Item1,
                        requested_height = size.Item2,
                        actual_width = window.AppWindow.Size.Width,
                        actual_height = window.AppWindow.Size.Height,
                        rendered_keys = buttons.Length,
                        page_horizontal_overflow = scroll.ScrollableWidth,
                        keyboard_horizontal_overflow = keyboardScroll.ScrollableWidth
                    });
                }
            }

            // Restore Standard Size (1060x720) in Light Theme for detailed state captures
            ((FrameworkElement)window.Content).RequestedTheme = ElementTheme.Light;
            ((OverlappedPresenter)window.AppWindow.Presenter).Restore();
            var normalScale = window.Content.XamlRoot.RasterizationScale;
            window.AppWindow.Resize(new SizeInt32((int)(1060 * normalScale), (int)(720 * normalScale)));
            await Task.Delay(300);

            var activePage = (MagneticSwitchPage)MainWindow.CurrentNavFrame!.Content;
            var pageScroll = Descendants(activePage).OfType<ScrollViewer>().First(view => view.Name == "PageScroll");
            void ScrollTo(double offset) => pageScroll.ChangeView(null, offset, null, true);
            void BringToView(FrameworkElement element)
            {
                var transform = element.TransformToVisual(pageScroll);
                var point = transform.TransformPoint(new Windows.Foundation.Point(0, 0));
                pageScroll.ChangeView(null, Math.Max(0, pageScroll.VerticalOffset + point.Y - 20), null, true);
            }

            FrameworkElement FindNamed(string name) => Descendants(activePage).OfType<FrameworkElement>().First(e => e.Name == name);

            // Scenario 1: Copilot selected (Contract verification)
            ScrollTo(0);
            await Task.Delay(100);
            var copilot = Descendants(activePage).OfType<Button>()
                .First(button => Microsoft.UI.Xaml.Automation.AutomationProperties.GetAutomationId(button) == "MagneticKey050A");
            if (!Equals(copilot.Content, "Cop")) throw new InvalidOperationException("Copilot short label missing");
            var peer = new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(copilot);
            ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)peer.GetPattern(
                Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
            await Task.Delay(150);
            var selectedText = Descendants(activePage).OfType<TextBlock>()
                .First(block => block.Name == "SelectedKeyText");
            if (!selectedText.Text.Contains("Copilot"))
                throw new InvalidOperationException("Selected-key context did not update");
            await CaptureAsync(window, directory, "Light-1060x720-Copilot-selected");

            // Scenario 2: Select 'V' (0x0402) with Actuation Dirty
            validationModel.Select(0x0402);
            validationModel.EditActuation(1.8);
            activePage.ForceRender();
            BringToView(FindNamed("ActuationCard"));
            await Task.Delay(150);
            await CaptureAsync(window, directory, "Light-1060x720-Actuation-dirty");

            // Scenario 3: RT Enabled (Press 0.6 mm, Release 0.4 mm)
            validationModel.EditRapidTrigger(true, 0.6, 0.4);
            activePage.ForceRender();
            BringToView(FindNamed("RapidTriggerCard"));
            await Task.Delay(150);
            await CaptureAsync(window, directory, "Light-1060x720-RT-enabled");

            // Scenario 4: Deadzone Editor (Top 0.2 mm, Bottom 0.1 mm)
            validationModel.EditDeadzone(0.2, 0.1);
            activePage.ForceRender();
            BringToView(FindNamed("DeadzoneCard"));
            await Task.Delay(150);
            await CaptureAsync(window, directory, "Light-1060x720-Deadzone-editor");

            // Scenario 5: DKS Editor Expanded (Start 1.2 mm, End 3.0 mm, Slot 1 Tap)
            validationModel.EditDksThresholds(1.2, 3.0);
            validationModel.EditDksSlot(0, 0x0602, "Tap", "Inactive", "Release", "Inactive");
            var dks = (CommunityToolkit.WinUI.Controls.SettingsExpander)FindNamed("DksExpander");
            dks.IsExpanded = true;
            activePage.ForceRender();
            BringToView(dks);
            await Task.Delay(150);
            await CaptureAsync(window, directory, "Light-1060x720-DKS-editor-expanded");

            // Scenario 6: SpeedTap Unknown Baseline State
            var unknownBaselineStatus = new MagneticStatus
            {
                Status = "ok",
                ApiVersion = 1,
                Health = "Clean",
                Available = true,
                SpeedTap = new MagneticSpeedTapState
                {
                    Master = new MagneticKnownBool { Known = true, Value = true, Source = "HostProfile" },
                    SavedProfilePairsKnown = false,
                    PairKnowledge = "活动配置键对基线未知"
                }
            };
            var unknownBaselineClient = new OfflineValidationClient { CurrentStatus = unknownBaselineStatus };
            var unknownBaselineModel = new MagneticSettingsModel(unknownBaselineClient);
            await unknownBaselineModel.RefreshAsync();
            unknownBaselineModel.Select(0x0402);
            unknownBaselineModel.EditSpeedTapPair(0x0602, 0x0301); // A + D
            activePage.SetModelForValidation(unknownBaselineModel);
            dks.IsExpanded = false;
            await Task.Delay(100);
            BringToView(FindNamed("SpeedTapCard"));
            await Task.Delay(150);
            await CaptureAsync(window, directory, "Light-1060x720-SpeedTap-baseline-unknown");

            // Scenario 7: Persistent Safety Quarantine State
            var quarantineStatus = new MagneticStatus
            {
                Status = "error",
                ApiVersion = 1,
                Health = "PersistentSafetyQuarantine",
                PersistentSafetyQuarantine = true,
                Available = true,
                LastError = "跨重启安全隔离中：上次磁轴事务未确认完成。"
            };
            var quarantineClient = new OfflineValidationClient { CurrentStatus = quarantineStatus };
            var quarantineModel = new MagneticSettingsModel(quarantineClient);
            await quarantineModel.RefreshAsync();
            quarantineModel.Select(0x0402);
            activePage.SetModelForValidation(quarantineModel);
            ScrollTo(0);
            await Task.Delay(150);
            await CaptureAsync(window, directory, "Light-1060x720-Persistent-quarantine");

            // Scenario 8: Validation / Operation Error State
            var errorStatus = new MagneticStatus
            {
                Status = "error",
                ApiVersion = 1,
                Health = "Clean",
                Available = false,
                LastError = "原生 HID 设备当前不可用，或核心正在模拟/使用其他后端。"
            };
            var errorClient = new OfflineValidationClient { CurrentStatus = errorStatus };
            var errorModel = new MagneticSettingsModel(errorClient);
            await errorModel.RefreshAsync();
            errorModel.Select(0x0402);
            activePage.SetModelForValidation(errorModel);
            ScrollTo(0);
            await Task.Delay(150);
            await CaptureAsync(window, directory, "Light-1060x720-Validation-error");

            // Scenario 9: Global Mode (All Keys / 全部按键)
            var globalClient = new OfflineValidationClient();
            var globalModel = new MagneticSettingsModel(globalClient);
            await globalModel.RefreshAsync();
            globalModel.SelectGlobal();
            globalModel.EditGlobalActuation(2.0);
            globalModel.EditGlobalRapidTrigger(0.5, 0.5, false);
            activePage.SetModelForValidation(globalModel);
            ScrollTo(0);
            await Task.Delay(150);
            await CaptureAsync(window, directory, "Light-1060x720-Global-mode");

            ((FrameworkElement)window.Content).RequestedTheme = ElementTheme.Dark;
            await Task.Delay(150);
            await CaptureAsync(window, directory, "Dark-1060x720-Global-mode");
            ((FrameworkElement)window.Content).RequestedTheme = ElementTheme.Light;
        }
        catch (Exception ex) { error = ex.ToString(); }
        finally
        {
            Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory, "magnetic-layout-results.json"),
                JsonSerializer.Serialize(new { error, observations }, new JsonSerializerOptions { WriteIndented = true }));
            await App.RequestExit();
        }
    }
}
