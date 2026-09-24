using System.Text.Json;
using CommunityToolkit.WinUI.Controls;
using Aura_WinUI.Pages;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics.Imaging;
using Windows.Storage;
using Windows.Storage.Streams;

namespace Aura_WinUI.Validation;

// Explicit opt-in Release test entry; never runs on normal activation or uses a developer path.
internal static class LayoutValidation
{
    internal static bool Requested => Environment.GetCommandLineArgs().Contains("--validate-layout") &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_UI_VALIDATION_DIR"));

    private static IEnumerable<DependencyObject> Descendants(DependencyObject parent)
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i); yield return child;
            foreach (var item in Descendants(child)) yield return item;
        }
    }
    private static T Find<T>(Page page, string name) where T : FrameworkElement =>
        Descendants(page).OfType<T>().First(e => e.Name == name);
    private static double Left(FrameworkElement element, UIElement root) => element.TransformToVisual(root).TransformPoint(new Windows.Foundation.Point()).X;
    private static async Task Screenshot(MainWindow window, string directory, string name)
    {
        var bitmap = new RenderTargetBitmap(); await bitmap.RenderAsync(window.Content);
        var pixels = await bitmap.GetPixelsAsync(); var bytes = new byte[pixels.Length];
        using (var reader = DataReader.FromBuffer(pixels)) reader.ReadBytes(bytes);
        var folder = await StorageFolder.GetFolderFromPathAsync(directory);
        var file = await folder.CreateFileAsync(name + ".png", CreationCollisionOption.ReplaceExisting);
        using var stream = await file.OpenAsync(FileAccessMode.ReadWrite);
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, bytes);
        await encoder.FlushAsync();
    }

    private static void Invoke(Button button)
    {
        var peer = new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(button);
        ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)peer.GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
    }
    private static async Task Until(Func<bool> condition, string message)
    {
        var deadline=DateTime.UtcNow.AddSeconds(8);
        while (!condition()) { if (DateTime.UtcNow>deadline) throw new TimeoutException(message); await Task.Delay(50); }
    }
    private static async Task<ContentDialog> Dialog(MainWindow window, string title)
    {
        ContentDialog? found=null;
        await Until(()=> {
            found=VisualTreeHelper.GetOpenPopupsForXamlRoot(window.Content.XamlRoot)
                .SelectMany(p=>new[] { p.Child }.Concat(Descendants(p.Child))).OfType<ContentDialog>()
                .FirstOrDefault(d=>d.Title?.ToString()==title);
            return found!=null;
        }, "Dialog did not open: "+title);
        await Task.Delay(100); return found!;
    }
    private static void Choose(ContentDialog dialog,string button) => Invoke(Descendants(dialog).OfType<Button>().First(b=>b.Name==button));
    private static async Task ValidateFpsInteraction(MainWindow window)
    {
        window.NavigateTo(typeof(LightingPage)); await Task.Delay(600);
        var page=(Page)MainWindow.CurrentNavFrame!.Content;
        var slider=Find<Slider>(page,"BrightnessSlider"); var entry=Find<Button>(page,"GlobalFpsButton");
        var save=Find<Button>(page,"SaveBtn");
        const string dirtyTitle="先处理未保存的灯效", fpsTitle="设置全局默认刷新率";
        async Task SetExternal(int value) {
            var current=await AuraControlClient.Instance.GetGlobalLightingAsync();
            var result=await AuraControlClient.Instance.UpdateGlobalLightingAsync(value,current.Value!.Revision);
            if (!result.IsSuccess) throw new InvalidOperationException(result.Error);
        }
        async Task CheckFps(int expected) {
            await Until(()=>entry.IsEnabled,"FPS operation did not finish");
            var actual=await AuraControlClient.Instance.GetGlobalLightingAsync();
            if (actual.Value?.Fps!=expected) throw new InvalidOperationException("Unexpected persisted FPS");
        }
        // Cancel must retain draft and must not open a second dialog.
        slider.Value=60; Invoke(entry); (await Dialog(window,dirtyTitle)).Hide();
        await Until(()=>entry.IsEnabled,"Cancel did not finish");
        if (!save.IsEnabled) throw new InvalidOperationException("Cancel lost lighting draft");
        // Discard explicitly, then confirm an independent global write.
        Invoke(entry); Choose(await Dialog(window,dirtyTitle),"SecondaryButton");
        var fps=await Dialog(window,fpsTitle); var number=Descendants(fps).OfType<NumberBox>().First();
        number.Value=40.5; if (fps.IsPrimaryButtonEnabled) throw new InvalidOperationException("Fractional FPS was accepted");
        number.Value=40; Choose(fps,"PrimaryButton"); await CheckFps(40);
        // A failed draft save (409) must not continue into the FPS dialog/write.
        slider.Value=75; await SetExternal(41); Invoke(entry); Choose(await Dialog(window,dirtyTitle),"PrimaryButton");
        await CheckFps(41);
        if (VisualTreeHelper.GetOpenPopupsForXamlRoot(window.Content.XamlRoot).SelectMany(p=>Descendants(p.Child)).OfType<ContentDialog>().Any())
            throw new InvalidOperationException("Failed draft save continued to FPS dialog");
        // Successful draft save precedes opening the FPS editor.
        slider.Value=70; Invoke(entry); Choose(await Dialog(window,dirtyTitle),"PrimaryButton");
        fps=await Dialog(window,fpsTitle); Descendants(fps).OfType<NumberBox>().First().Value=42;
        Choose(fps,"PrimaryButton"); await CheckFps(42);
        // Concurrent global edit must surface an error and read back, never overwrite.
        Invoke(entry); fps=await Dialog(window,fpsTitle); await SetExternal(43);
        Descendants(fps).OfType<NumberBox>().First().Value=44; Choose(fps,"PrimaryButton"); await CheckFps(43);
        var resultBar=Find<InfoBar>(page,"StatusInfoBar");
        if (!resultBar.IsOpen || resultBar.Severity!=InfoBarSeverity.Error) throw new InvalidOperationException("FPS conflict was not visible");
    }

    internal static async Task RunAsync(MainWindow window)
    {
        var directory = Path.GetFullPath(Environment.GetEnvironmentVariable("AURA_UI_VALIDATION_DIR")!);
        var results = new List<object>(); string? error = null;
        try
        {
            Directory.CreateDirectory(directory);
            await DaemonSupervisor.Instance.EnsureStartedAsync();
            var status = await AuraControlClient.Instance.GetRuntimeStatusAsync();
            if (!status.IsOnline || !status.IsDryRun) throw new InvalidOperationException("Layout validation requires a separately started, isolated dry-run core");
            var deadline = DateTime.UtcNow.AddSeconds(10);
            while (window.Content.XamlRoot == null)
            {
                if (DateTime.UtcNow > deadline) throw new TimeoutException("XAML root did not load");
                await Task.Delay(50);
            }
            await ValidateFpsInteraction(window);
            window.NavigateTo(typeof(GameIntegrationPage)); await Task.Delay(600);
            var game=(Page)MainWindow.CurrentNavFrame!.Content;
            var advanced=Find<Expander>(game,"AdvancedExpander");
            if (advanced.IsExpanded) throw new InvalidOperationException("Advanced debugging should default to collapsed");
            var simulated=await AuraControlClient.Instance.UpdateSimulationAsync(new() { Enabled=true, Health=37, Armor=62, Bomb="planted", RoundPhase="live" });
            if (!simulated.IsSuccess) throw new InvalidOperationException(simulated.Error);
            var banner=Find<InfoBar>(game,"SimulationBanner");
            await Until(()=>banner.IsOpen && Find<TextBlock>(game,"HealthText").Text=="37","Simulation state did not reach native page");
            if (advanced.IsExpanded || Find<TextBlock>(game,"SourceText").Text!="模拟") throw new InvalidOperationException("Simulation state hidden by advanced section");
            await Screenshot(window,directory,"Simulation-advanced-collapsed");
            if (!((Button)banner.ActionButton).Content.Equals("打开自动化工作室")) throw new InvalidOperationException("Simulation owner link missing");
            await AuraControlClient.Instance.UpdateSimulationAsync(new() { Enabled=false });
            await Until(()=>!banner.IsOpen && Find<TextBlock>(game,"SourceText").Text=="真实 GSI","Return to real data failed");
            foreach (var theme in new[] { ElementTheme.Dark, ElementTheme.Light })
            foreach (var size in new[] { (600,500), (800,600), (1060,720), (1600,1000), (0,0) })
            {
                ((FrameworkElement)window.Content).RequestedTheme = theme;
                var scale = window.Content.XamlRoot.RasterizationScale;
                var presenter = (Microsoft.UI.Windowing.OverlappedPresenter)window.AppWindow.Presenter;
                if (size.Item1 == 0) presenter.Maximize();
                else { presenter.Restore(); window.AppWindow.Resize(new Windows.Graphics.SizeInt32((int)(size.Item1*scale),(int)(size.Item2*scale))); }
                await Task.Delay(150);
                foreach (var type in new[] { typeof(HomePage), typeof(LightingPage), typeof(GameIntegrationPage), typeof(SettingsPage) })
                {
                    window.NavigateTo(type); await Task.Delay(450);
                    var page = (Page)MainWindow.CurrentNavFrame!.Content;
                    if (type == typeof(SettingsPage))
                    {
                        var cards = Descendants(page).OfType<SettingsCard>().ToArray();
                        if (cards.Length < 3 || cards.Any(card => card.IsClickEnabled))
                            throw new InvalidOperationException("SettingsCard hierarchy or click semantics invalid");
                        if (Find<SettingsExpander>(page,"CoreSettingsExpander").IsExpanded)
                            throw new InvalidOperationException("Core details should start collapsed");
                    }
                    var scroll = Find<ScrollViewer>(page, type == typeof(LightingPage) ? "LightingContent" : "PageScroll");
                    var panel = Find<StackPanel>(page, type == typeof(HomePage) ? "RootPanel" : "PageContent");
                    var name = $"{theme}-{(size.Item1 == 0 ? "Maximized" : $"{size.Item1}x{size.Item2}")}-{type.Name}";
                    await Screenshot(window,directory,name);
                    var left = Left(panel,page); var width = panel.ActualWidth;
                    var height = scroll.ActualHeight;
                    var tracked = panel.Children.OfType<FrameworkElement>().ToArray();
                    var bounds = tracked.Select(e => (Left(e,page),e.ActualWidth)).ToArray();
                    foreach (var expander in Descendants(page).OfType<Expander>().ToArray()) expander.IsExpanded = true;
                    foreach (var expander in Descendants(page).OfType<SettingsExpander>().ToArray()) expander.IsExpanded = true;
                    await Task.Delay(500);
                    if (type == typeof(SettingsPage))
                    {
                        var reconnect = Find<Button>(page,"RestartDaemonBtn");
                        if (reconnect.ActualWidth < 1 || !reconnect.IsEnabled)
                            throw new InvalidOperationException("SettingsExpander lost reconnect action");
                        if (Descendants(page).OfType<SettingsCard>().Count() < 8)
                            throw new InvalidOperationException("SettingsExpander items did not render");
                    }
                    var dx = Math.Abs(Left(panel,page)-left); var dw = Math.Abs(panel.ActualWidth-width);
                    if (dx>1 || dw>1 || scroll.ScrollableWidth>1) throw new InvalidOperationException($"Unstable/overflowing layout: {name}, dx={dx}, dw={dw}");
                    for (int i=0;i<tracked.Length;i++)
                        if (Math.Abs(Left(tracked[i],page)-bounds[i].Item1)>1 || Math.Abs(tracked[i].ActualWidth-bounds[i].Item2)>1)
                            throw new InvalidOperationException("Expander changed sibling geometry: "+name);
                    await Screenshot(window,directory,name+"-expanded");
                    foreach (var expander in Descendants(page).OfType<Expander>().ToArray()) expander.IsExpanded = false;
                    foreach (var expander in Descendants(page).OfType<SettingsExpander>().ToArray()) expander.IsExpanded = false;
                    foreach (var bar in Descendants(page).OfType<InfoBar>().Where(b=>b.Name is "ResultBar" or "StatusInfoBar"))
                    { bar.Severity=InfoBarSeverity.Error;bar.Message="布局验证：这是一条测试通知，不是服务错误。";bar.IsOpen=true; }
                    await Task.Delay(120);
                    if (Math.Abs(scroll.ActualHeight-height)>1 || Math.Abs(panel.ActualWidth-width)>1) throw new InvalidOperationException("Notification changed viewport: "+name);
                    await Screenshot(window,directory,name+"-notification");
                    scroll.ChangeView(null,scroll.ScrollableHeight,null,true); await Task.Delay(100);
                    await Screenshot(window,directory,name+"-bottom");
                    foreach (var bar in Descendants(page).OfType<InfoBar>().Where(b=>b.Name is "ResultBar" or "StatusInfoBar")) bar.IsOpen=false;
                    await Task.Delay(60);
                    if (Math.Abs(scroll.ActualHeight-height)>1) throw new InvalidOperationException("Closing notification changed viewport: "+name);
                    var nav = Descendants(window.Content).OfType<NavigationView>().First();
                    var wasOpen = nav.IsPaneOpen; nav.IsPaneOpen=!wasOpen; await Task.Delay(150);
                    if (scroll.ScrollableWidth>1) throw new InvalidOperationException("Navigation toggle overflow: "+name);
                    await Screenshot(window,directory,name+"-navigation"); nav.IsPaneOpen=wasOpen; await Task.Delay(100);
                    results.Add(new {page=type.Name,theme=theme.ToString(),width=size.Item1,height=size.Item2,actual_rasterization_scale=scale,content_width=width,expander_dx=dx,expander_dw=dw,scrollable_width=scroll.ScrollableWidth});
                }
            }
        }
        catch (Exception ex) { error=ex.ToString(); }
        finally
        {
            File.WriteAllText(Path.Combine(directory,"layout-results.json"),JsonSerializer.Serialize(new {error,results},new JsonSerializerOptions { WriteIndented=true }));
            await App.RequestExit();
        }
    }
}
