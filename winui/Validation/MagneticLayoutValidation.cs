using System.Text.Json;
using Aura_WinUI.Pages;
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
            foreach (var theme in new[] { ElementTheme.Light, ElementTheme.Dark })
            foreach (var size in new[] { (600, 500), (1060, 720), (3840, 2160) })
            {
                ((FrameworkElement)window.Content).RequestedTheme = theme;
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
                observations.Add(new { theme = theme.ToString(), requested_width = size.Item1,
                    requested_height = size.Item2, actual_width = window.AppWindow.Size.Width,
                    actual_height = window.AppWindow.Size.Height, rendered_keys = buttons.Length,
                    page_horizontal_overflow = scroll.ScrollableWidth,
                    keyboard_horizontal_overflow = keyboardScroll.ScrollableWidth });
            }
            ((FrameworkElement)window.Content).RequestedTheme = ElementTheme.Light;
            ((OverlappedPresenter)window.AppWindow.Presenter).Restore();
            var normalScale = window.Content.XamlRoot.RasterizationScale;
            window.AppWindow.Resize(new SizeInt32((int)(1060 * normalScale), (int)(720 * normalScale)));
            await Task.Delay(250);
            var selectedPage = (MagneticSwitchPage)MainWindow.CurrentNavFrame!.Content;
            var copilot = Descendants(selectedPage).OfType<Button>()
                .First(button => Microsoft.UI.Xaml.Automation.AutomationProperties.GetAutomationId(button) == "MagneticKey050A");
            if (!Equals(copilot.Content, "Cop")) throw new InvalidOperationException("Copilot short label missing");
            var peer = new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(copilot);
            ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)peer.GetPattern(
                Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
            await Task.Delay(100);
            var selected = Descendants(selectedPage).OfType<TextBlock>()
                .First(block => block.Name == "SelectedKeyText");
            if (!selected.Text.Contains("Copilot"))
                throw new InvalidOperationException("Selected-key context did not update");
            await CaptureAsync(window, directory, "Light-1060x720-Copilot-selected");
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
