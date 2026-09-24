using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Aura_WinUI.Common;

// Shared native-control sizing only; no custom layout/scrolling implementation.
internal static class PageLayout
{
    internal static void Attach(Page page, ScrollViewer scroll, StackPanel content, Action<double>? reflow = null, double maxWidth = 1700)
    {
        scroll.HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled;
        scroll.HorizontalScrollMode = ScrollMode.Disabled;
        scroll.VerticalScrollBarVisibility = ScrollBarVisibility.Visible;
        scroll.HorizontalContentAlignment = HorizontalAlignment.Stretch;
        scroll.VerticalContentAlignment = VerticalAlignment.Top;
        content.HorizontalAlignment = HorizontalAlignment.Center;
        content.MaxWidth = maxWidth;
        void Resize()
        {
            var viewport = scroll.ViewportWidth;
            if (viewport <= 0) return;
            var width = Math.Min(maxWidth, viewport);
            var padding = width < 720 ? 16 : 24;
            content.Width = width;
            content.Padding = new Thickness(padding);
            reflow?.Invoke(Math.Max(0, width - padding * 2));
        }
        scroll.SizeChanged += (_, _) => Resize();
        page.Loaded += (_, _) => { Resize(); page.DispatcherQueue.TryEnqueue(Resize); };
    }

    internal static void Columns(Grid grid, int columns)
    {
        grid.ColumnDefinitions.Clear(); grid.RowDefinitions.Clear();
        for (int i = 0; i < columns; i++) grid.ColumnDefinitions.Add(new() { Width = new GridLength(1, GridUnitType.Star) });
        for (int i = 0; i < (grid.Children.Count + columns - 1) / columns; i++) grid.RowDefinitions.Add(new() { Height = GridLength.Auto });
        for (int i = 0; i < grid.Children.Count; i++) { Grid.SetRow((FrameworkElement)grid.Children[i], i / columns); Grid.SetColumn((FrameworkElement)grid.Children[i], i % columns); }
    }

    internal static void Notification(Page page, InfoBar bar)
    {
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(6) };
        timer.Tick += (_, _) => { timer.Stop(); bar.IsOpen = false; };
        bar.RegisterPropertyChangedCallback(InfoBar.IsOpenProperty, (_, _) => {
            timer.Stop(); if (bar.IsOpen && bar.Severity == InfoBarSeverity.Success) timer.Start();
        });
        page.Unloaded += (_, _) => { timer.Stop(); bar.IsOpen = false; };
    }
}
