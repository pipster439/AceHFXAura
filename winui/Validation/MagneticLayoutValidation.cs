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
    internal static bool Requested => (Environment.GetCommandLineArgs().Contains("--validate-magnetic-layout") ||
        OverlayOnlyRequested || ProfileRtLayoutValidation.Requested) &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_UI_VALIDATION_DIR"));
    private static bool OverlayOnlyRequested => Environment.GetCommandLineArgs().Contains("--validate-magnetic-overlays");

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
        await CaptureElementAsync(window.Content, directory, name);
    }

    private static async Task CaptureElementAsync(UIElement element, string directory, string name)
    {
        var bitmap = new RenderTargetBitmap();
        await bitmap.RenderAsync(element);
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
        public MagneticStatus? BatchResponse { get; set; }
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
        private static Task<MagneticStatus> WriteForbidden() =>
            throw new InvalidOperationException("Offline visual validation forbids magnetic writes");
        public Task<MagneticStatus> SetActuationAsync(ushort logicalId, double mm) => WriteForbidden();
        public Task<MagneticStatus> SetRapidTriggerAsync(ushort logicalId, double pressMm, double releaseMm, bool resolveDks) => WriteForbidden();
        public Task<MagneticStatus> DisableRapidTriggerAsync(ushort logicalId) => WriteForbidden();
        public Task<MagneticStatus> SetDeadzoneAsync(ushort logicalId, double topMm, double bottomMm) => WriteForbidden();
        public Task<MagneticStatus> ResetAllDeadzoneAsync() => WriteForbidden();
        public Task<MagneticStatus> SetDksAsync(ushort logicalId, double startMm, double endMm, IReadOnlyList<MagneticDksSlot> slots, bool resolveRt) => WriteForbidden();
        public Task<MagneticStatus> RestoreDksStandardAsync(ushort logicalId) => WriteForbidden();
        public Task<MagneticStatus> SetSpeedTapPairAsync(ushort key1, ushort key2) => WriteForbidden();
        public Task<MagneticStatus> DisableSpeedTapPairAsync(ushort key1, ushort key2) => WriteForbidden();
        public Task<MagneticStatus> SetSpeedTapMasterAsync(bool enabled) => WriteForbidden();
        public Task<MagneticStatus> ResetSpeedTapToProfileAsync() => WriteForbidden();
        public Task<MagneticStatus> SetStaticAnalogEffectAsync(bool enabled) => WriteForbidden();
        public Task<MagneticStatus> SetGlobalActuationAsync(double mm) => WriteForbidden();
        public Task<MagneticStatus> SetGlobalDeadzoneAsync(double topMm, double bottomMm) => WriteForbidden();
        public Task<MagneticStatus> SetGlobalRapidTriggerAsync(double pressMm, double releaseMm, double topMm, double bottomMm, bool separateMode) => WriteForbidden();
        public Task<MagneticStatus> AcknowledgeExternalResynchronizationAsync() => WriteForbidden();
        public Task<MagneticStatus> SetBatchActuationAsync(IReadOnlyList<ushort> ids, double millimeters) =>
            BatchResponse is { } response ? Task.FromResult(response) : WriteForbidden();
        public Task<MagneticStatus> SetBatchRapidTriggerAsync(IReadOnlyList<ushort> ids, bool enable,
            double? pressMm = null, double? releaseMm = null, bool resolveDks = false) =>
            BatchResponse is { } response ? Task.FromResult(response) : WriteForbidden();
        public Task<MagneticStatus> SetBatchDeadzoneAsync(IReadOnlyList<ushort> ids, double topMm, double bottomMm) =>
            BatchResponse is { } response ? Task.FromResult(response) : WriteForbidden();
    }

    private static async Task ValidateOverlaysAsync(MainWindow window, string directory,
        MagneticSwitchPage page, MagneticSettingsModel model, OfflineValidationClient client, List<object> observations)
    {
        var root = (FrameworkElement)window.Content;
        root.RequestedTheme = ElementTheme.Light;
        await Task.Delay(100);
        var status = client.CurrentStatus;
        status.GlobalActuation = new MagneticGlobalActuationState { Known = true, Raw = 10, Source = "HostProfile" };
        Button Key(ushort id) => Descendants(page).OfType<Button>().First(button =>
            Microsoft.UI.Xaml.Automation.AutomationProperties.GetAutomationId(button) == $"MagneticKey{id:X4}");
        TextBlock Part(ushort id, string kind) => Descendants(Key(id)).OfType<TextBlock>()
            .First(block => block.Name == $"MagneticKey{kind}{id:X4}");
        Windows.Foundation.Point Center(ushort id)
        {
            var label = Part(id, "Legend");
            return label.TransformToVisual(Key(id)).TransformPoint(
                new Windows.Foundation.Point(label.ActualWidth / 2, label.ActualHeight / 2));
        }
        var special = new ushort[] { 0x0701, 0x0100, 0x0503, 0x0707, 0x0400, 0x0508, 0x050a, 0x060a };
        model.SelectGlobal();
        page.ForceRender();
        await Task.Delay(100);
        var baseline = special.ToDictionary(id => id, id => (Key(id).ActualWidth, Key(id).ActualHeight, Center(id)));
        status.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0701, Raw = 10, Source = "SessionApplied" });
        status.RapidTrigger.Add(new MagneticRapidTriggerValue { LogicalId = 0x0701, Enabled = true,
            PressRaw = 2, ReleaseRaw = 2, Source = "SessionApplied" });
        status.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0503, Raw = 15, Source = "HostProfile" });
        status.RapidTrigger.Add(new MagneticRapidTriggerValue { LogicalId = 0x0707, Enabled = true,
            PressRaw = 4, ReleaseRaw = 3, Source = "SessionApplied" });
        status.RapidTrigger.Add(new MagneticRapidTriggerValue { LogicalId = 0x0400, Enabled = true,
            PressRaw = 4, ReleaseRaw = 2, Source = "HostProfile" });
        page.ForceRender();
        await Task.Delay(100);
        if (Part(0x0701, "Actuation").Text != "1.0" || Part(0x0701, "RtPress").Text != "↓0.2" ||
            Part(0x0701, "RtRelease").Text != "↑0.2" || Part(0x0100, "Actuation").Text != "1.0" ||
            Part(0x0400, "RtPress").Text != "—" || Part(0x0503, "Actuation").Text != "1.5" ||
            Part(0x0707, "RtPress").Text != "↓0.4")
            throw new InvalidOperationException("Keycap overlay provenance or placement mismatch");
        if (Part(0x0100, "Actuation").Opacity >= Part(0x0701, "Actuation").Opacity ||
            !Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(Key(0x0100)).Contains("全局基线 1.0 mm"))
            throw new InvalidOperationException("Global baseline lacks subtle or accessible source labeling");
        foreach (ushort id in special)
        {
            var visual = MagneticKeyLayout.Find(id)!;
            var button = Key(id);
            var point = Center(id);
            var before = baseline[id];
            if (Part(id, "Legend").Text != visual.Label ||
                Math.Abs(button.ActualWidth - before.ActualWidth) > 0.1 ||
                Math.Abs(button.ActualHeight - before.ActualHeight) > 0.1 ||
                Math.Abs(point.X - before.Item3.X) > 0.5 || Math.Abs(point.Y - before.Item3.Y) > 0.5)
                throw new InvalidOperationException($"Overlay changed key geometry or centered legend: {visual.FullName}");
        }
        var w = Key(0x0701);
        var wName = Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(w);
        if (!wName.Contains("未选择") || !wName.Contains("触发点 1.0 mm") ||
            !wName.Contains("RT 按下 0.2 mm") ||
            !Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(Key(0x0100)).Contains("设备逐键覆盖状态未确认"))
            throw new InvalidOperationException("Accessible overlay values or unknown state missing");

        var scale = window.Content.XamlRoot.RasterizationScale;
        async Task Resize(int width, int height)
        {
            ((OverlappedPresenter)window.AppWindow.Presenter).Restore();
            window.AppWindow.Resize(new SizeInt32((int)(width * scale), (int)(height * scale)));
            await Task.Delay(250);
            var pageScroll = Descendants(page).OfType<ScrollViewer>().First(view => view.Name == "PageScroll");
            if (pageScroll.ScrollableWidth > 1) throw new InvalidOperationException("Overlay caused page overflow");
            foreach (ushort id in special)
            {
                var button = Key(id);
                var center = Center(id);
                if (Math.Abs(button.ActualWidth - button.Width) > 0.5 ||
                    Math.Abs(button.ActualHeight - button.Height) > 0.5 ||
                    Math.Abs(center.X - button.ActualWidth / 2) > 1 ||
                    Math.Abs(center.Y - button.ActualHeight / 2) > 1)
                    throw new InvalidOperationException($"Responsive keycap geometry or legend center changed: " +
                        $"{MagneticKeyLayout.Find(id)!.FullName} at {width}x{height}, " +
                        $"actual {button.ActualWidth:F2}x{button.ActualHeight:F2}, requested {button.Width:F2}x{button.Height:F2}, " +
                        $"center {center.X:F2},{center.Y:F2}");
            }
            observations.Add(new { width, height, page_horizontal_overflow = pageScroll.ScrollableWidth,
                keyboard_horizontal_overflow = Descendants(page).OfType<ScrollViewer>()
                    .First(view => view.Name == "KeyboardViewport").ScrollableWidth });
        }
        await Resize(1060, 720);
        await CaptureAsync(window, directory, "KeycapOverlay-Light-1060x720-Global-overview");
        model.Select(0x0701); page.ForceRender();
        await Task.Delay(80);
        if (Math.Abs(Center(0x0701).X - baseline[0x0701].Item3.X) > 0.5 ||
            !Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(w).Contains("已选择"))
            throw new InvalidOperationException("Single selection moved W legend or lost accessible state");
        await CaptureAsync(window, directory, "KeycapOverlay-Light-1060x720-W-selected");
        model.EnterMulti();
        foreach (ushort id in new ushort[] { 0x0602, 0x0702, 0x0301 }) model.ToggleMulti(id);
        page.ForceRender();
        await Task.Delay(80);
        foreach (ushort id in new ushort[] { 0x0701, 0x0602, 0x0702, 0x0301 })
            if (Part(id, "Legend").Text != MagneticKeyLayout.Find(id)!.Label ||
                Math.Abs(Center(id).X - Key(id).ActualWidth / 2) > 0.5)
                throw new InvalidOperationException("Multi selection moved a centered legend");
        await CaptureAsync(window, directory, "KeycapOverlay-Light-1060x720-Multi-WASD");
        root.RequestedTheme = ElementTheme.Dark;
        await Task.Delay(160);
        await CaptureAsync(window, directory, "KeycapOverlay-Dark-1060x720-Multi-WASD");
        root.RequestedTheme = ElementTheme.Light;
        foreach (var size in new[] { (600, 700), (1440, 900), (3840, 2160) })
        {
            await Resize(size.Item1, size.Item2);
            await CaptureAsync(window, directory, $"KeycapOverlay-Light-{size.Item1}x{size.Item2}-Multi-WASD");
        }
    }

    internal static async Task RunAsync(MainWindow window)
    {
        if (ProfileRtLayoutValidation.Requested) { await ProfileRtLayoutValidation.RunAsync(window); return; }
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
            validationModel.Select(0x0301); // D: single-key baseline

            var initialPage = (MagneticSwitchPage)MainWindow.CurrentNavFrame!.Content;
            initialPage.SetModelForValidation(validationModel);
            await Task.Delay(100);
            if (OverlayOnlyRequested)
            {
                await ValidateOverlaysAsync(window, directory, initialPage, validationModel,
                    validationClient, observations);
                return;
            }

            // Phase 2 breakpoint and theme matrix, with a single-key context.
            foreach (var theme in new[] { ElementTheme.Light, ElementTheme.Dark })
            {
                ((FrameworkElement)window.Content).RequestedTheme = theme;
                foreach (var size in new[] { (600, 700), (1060, 720), (1440, 900), (3840, 2160) })
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
                        throw new InvalidOperationException($"Keyboard clips keys at desktop width: {keyboardScroll.ScrollableWidth:F1} px; workspace {Descendants(page).OfType<FrameworkElement>().First(e => e.Name == "ContentColumns").ActualWidth:F1} px");
                    var details = Descendants(page).OfType<FrameworkElement>().First(e => e.Name == "DetailsPanel");
                    if (size.Item1 >= 1440 && Grid.GetColumn(details) != 1)
                        throw new InvalidOperationException("Wide workspace did not place common details beside keyboard");
                    if (!Descendants(page).OfType<FrameworkElement>().Any(e => e.Name == "CommonLowerGrid") ||
                        !Descendants(page).OfType<FrameworkElement>().Any(e => e.Name == "GlobalFeaturesPanel"))
                        throw new InvalidOperationException("Responsive common/advanced regions missing");
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
            if (!Descendants(copilot).OfType<TextBlock>().Any(block =>
                block.Name == "MagneticKeyLegend050A" && block.Text == "Cop"))
                throw new InvalidOperationException("Copilot short label missing");
            var peer = new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(copilot);
            ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)peer.GetPattern(
                Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
            await Task.Delay(150);
            var selectedText = Descendants(activePage).OfType<TextBlock>()
                .First(block => block.Name == "SelectedKeyHeroTitle");
            if (!selectedText.Text.Contains("Copilot") ||
                Descendants(activePage).OfType<TextBlock>().Any(block => block.Name == "SelectedKeyText" ||
                    block.Text.StartsWith("已选按键：")))
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
            validationModel.ConfigureDksEditor();
            var dks = (CommunityToolkit.WinUI.Controls.SettingsExpander)FindNamed("DksExpander");
            activePage.ForceRender();
            dks.IsExpanded = true;
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

            // Phase 2 selection and DKS evidence screenshots use offline status only.
            var multiStatus = new MagneticStatus { Status = "ok", ApiVersion = 1, Health = "Clean", Available = true };
            var multiClient = new OfflineValidationClient { CurrentStatus = multiStatus };
            var multiModel = new MagneticSettingsModel(multiClient);
            await multiModel.RefreshAsync();
            multiModel.EnterMulti();
            activePage.SetModelForValidation(multiModel);
            ScrollTo(0);
            await Task.Delay(120);
            await CaptureAsync(window, directory, "Light-1060x720-Multi-no-selection");
            foreach (ushort id in new ushort[] { 0x0701, 0x0602, 0x0702, 0x0301 }) multiModel.ToggleMulti(id);
            activePage.SetModelForValidation(multiModel);
            ScrollTo(0);
            await Task.Delay(150);
            if (FindNamed("SelectedKeyHeroTitle") is not TextBlock multiHero || multiHero.Text != "4 个按键" ||
                FindNamed("ActuationApplyButton") is not Button multiApply || multiApply.IsEnabled ||
                !Equals(multiApply.Content, "应用到 4 个按键"))
                throw new InvalidOperationException("Multi selection did not show a read-only four-key context");
            var selectedKeys = Descendants(activePage).OfType<Button>()
                .Where(button => Microsoft.UI.Xaml.Automation.AutomationProperties.GetAutomationId(button)?.StartsWith("MagneticKey") == true &&
                    Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(button).Contains("已选择")).ToArray();
            if (selectedKeys.Length != 4 || selectedKeys.Any(button =>
                !Descendants(button).OfType<TextBlock>().Any(block => block.Name ==
                    $"MagneticKeyLegend{(ushort)button.Tag:X4}" &&
                    block.Text == MagneticKeyLayout.Find((ushort)button.Tag)!.Label) ||
                button.BorderThickness.Left < 2 || button.HorizontalContentAlignment != HorizontalAlignment.Stretch))
                throw new InvalidOperationException("Multi-selected keys lack visible and accessible selection state");
            await CaptureAsync(window, directory, "Light-1060x720-Multi-4-keys");
            BringToView(FindNamed("ActuationCard"));
            await Task.Delay(100);
            await CaptureAsync(window, directory, "Light-1060x720-Multi-Actuation-Unknown");
            multiStatus.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0701, Raw = 10, Source = "SessionApplied" });
            multiStatus.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0602, Raw = 20, Source = "SessionApplied" });
            activePage.ForceRender();
            await Task.Delay(150);
            await CaptureAsync(window, directory, "Light-1060x720-Multi-Actuation-ContainsUnknown");
            multiStatus.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0702, Raw = 10, Source = "SessionApplied" });
            multiStatus.Actuation.Add(new MagneticActuationValue { LogicalId = 0x0301, Raw = 10, Source = "SessionApplied" });
            activePage.ForceRender();
            await Task.Delay(120);
            await CaptureAsync(window, directory, "Light-1060x720-Multi-Actuation-Mixed");
            multiStatus.Actuation[1].Raw = 10;
            activePage.ForceRender();
            await Task.Delay(120);
            await CaptureAsync(window, directory, "Light-1060x720-Multi-Actuation-Uniform");

            multiModel.EditBatchRapidTrigger(MagneticBatchRtAction.Enable, 0.8, 0.6);
            activePage.ForceRender();
            BringToView(FindNamed("RapidTriggerCard"));
            await Task.Delay(120);
            await CaptureAsync(window, directory, "Light-1060x720-Multi-RT-Enable");

            multiClient.BatchResponse = new MagneticStatus { Status = "error", ApiVersion = 1,
                Health = "Clean", Available = true, BatchResult = new MagneticBatchResult {
                    RequestedCount = 4, NotExecutedCount = 4,
                    ConfiguredDksKeys = [0x0701], UnknownDksKeys = [0x0602, 0x0702],
                    Results = [new() { LogicalId = 0x0701 }, new() { LogicalId = 0x0602 },
                        new() { LogicalId = 0x0702 }, new() { LogicalId = 0x0301 }] } };
            var rtApply = (Button)FindNamed("RapidTriggerApplyButton");
            var rtPeer = new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(rtApply);
            ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)rtPeer.GetPattern(
                Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
            await Task.Delay(300);
            await CaptureAsync(window, directory, "Light-1060x720-Multi-RT-DKS-conflict");
            // The confirmation is only a screenshot scenario; close it without accepting.
            var dialog = VisualTreeHelper.GetOpenPopupsForXamlRoot(window.Content.XamlRoot)
                .Select(popup => popup.Child).OfType<ContentDialog>().FirstOrDefault();
            if (dialog == null) throw new InvalidOperationException("Batch RT conflict dialog did not open");
            await CaptureElementAsync(dialog, directory, "Light-1060x720-Multi-RT-DKS-conflict-dialog");
            dialog.Hide();
            await Task.Delay(120);

            multiClient.BatchResponse = new MagneticStatus { Status = "error", ApiVersion = 1,
                Health = "Clean", Available = true, BatchResult = new MagneticBatchResult {
                    RequestedCount = 4, AppliedCount = 2, FailedCount = 1, NotExecutedCount = 1,
                    Results = [new() { LogicalId = 0x0701, Status = "Applied" },
                        new() { LogicalId = 0x0602, Status = "Applied" },
                        new() { LogicalId = 0x0702, Status = "Failed", Detail = "模拟事务失败" },
                        new() { LogicalId = 0x0301, Status = "NotExecuted" }] } };
            multiModel.EditBatchActuation(1.2);
            await multiModel.ApplyBatchActuationAsync();
            activePage.ForceRender();
            BringToView(FindNamed("BatchResultBar"));
            await Task.Delay(120);
            await CaptureAsync(window, directory, "Light-1060x720-Multi-partial-result");

            var dksModel = new MagneticSettingsModel(new OfflineValidationClient());
            await dksModel.RefreshAsync();
            dksModel.Select(0x0301);
            activePage.SetModelForValidation(dksModel);
            BringToView(FindNamed("DksSection"));
            await Task.Delay(150);
            if (FindNamed("DksStatusText") is not TextBlock unknownText || unknownText.Text != "当前 DKS 状态未知" ||
                FindNamed("DksExpander").Visibility != Visibility.Collapsed)
                throw new InvalidOperationException("Unknown DKS state was not presented collapsed");
            await CaptureAsync(window, directory, "Light-1060x720-DKS-Unknown-collapsed");
            foreach (var (standard, name) in new[] { (false, "SessionConfigured"), (true, "Standard") })
            {
                var state = new MagneticStatus { Status = "ok", ApiVersion = 1, Health = "Clean", Available = true };
                state.Dks.Add(new MagneticDksValue { LogicalId = 0x0301, Source = "SessionApplied",
                    StandardRuntimeConfiguration = standard });
                var model = new MagneticSettingsModel(new OfflineValidationClient { CurrentStatus = state });
                await model.RefreshAsync();
                model.Select(0x0301);
                activePage.SetModelForValidation(model);
                BringToView(FindNamed("DksSection"));
                await Task.Delay(150);
                if (FindNamed("DksStatusText") is not TextBlock stateText || stateText.Text != model.DksStateText)
                    throw new InvalidOperationException($"DKS {name} status presentation mismatch");
                await CaptureAsync(window, directory, $"Light-1060x720-DKS-{name}");
            }

            var analogModel = new MagneticSettingsModel(new OfflineValidationClient());
            await analogModel.RefreshAsync();
            analogModel.SelectGlobal();
            activePage.SetModelForValidation(analogModel);
            BringToView(FindNamed("PressLightingCard"));
            await Task.Delay(150);
            if (FindNamed("AnalogToggle") is not ToggleSwitch analogToggle || analogToggle.IsEnabled ||
                FindNamed("AnalogApplyButton") is not Button analogApply || analogApply.IsEnabled ||
                FindNamed("AnalogDescriptionText") is not TextBlock analogDescription ||
                analogDescription.Text != MagneticSettingsModel.HardwareAnalogDescription ||
                FindNamed("AnalogBlockedReasonText") is not TextBlock analogReason ||
                analogReason.Text != MagneticSettingsModel.HardwareAnalogBlockedReason)
                throw new InvalidOperationException("Hardware Analog must show its prerequisite and block normal Apply");
            if (pageScroll.ScrollableWidth > 1)
                throw new InvalidOperationException("Hardware Analog copy caused horizontal page overflow");
            await CaptureElementAsync(FindNamed("PressLightingCard"), directory,
                "Light-HardwareAnalog-experimental-blocked-card");
            await CaptureAsync(window, directory, "Light-1060x720-HardwareAnalog-experimental-blocked");
            ((FrameworkElement)window.Content).RequestedTheme = ElementTheme.Dark;
            await Task.Delay(150);
            await CaptureElementAsync(FindNamed("PressLightingCard"), directory,
                "Dark-HardwareAnalog-experimental-blocked-card");
            await CaptureAsync(window, directory, "Dark-1060x720-HardwareAnalog-experimental-blocked");
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
