using System;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Aura_WinUI.Common;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed partial class SettingsPage : Page
{
    public SettingsPage()
    {
        InitializeComponent();
        Loaded += SettingsPage_Loaded;
    }

    private void SettingsPage_Loaded(object sender, RoutedEventArgs e)
    {
        MinimizeToTrayToggle.IsOn = TrayIconManager.MinimizeToTrayEnabled;
        UpdateDaemonDetail();

        // 绑定主题选择状态
        if (MainWindow.CurrentInstance?.Content is FrameworkElement root)
        {
            ThemeComboBox.SelectedIndex = root.RequestedTheme switch
            {
                ElementTheme.Light => 1,
                ElementTheme.Dark => 2,
                _ => 0
            };
        }
    }

    private void ThemeComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ThemeComboBox.SelectedItem is ComboBoxItem item &&
            MainWindow.CurrentInstance?.Content is FrameworkElement root)
        {
            root.RequestedTheme = (string)item.Tag switch
            {
                "Light" => ElementTheme.Light,
                "Dark" => ElementTheme.Dark,
                _ => ElementTheme.Default
            };
        }
    }

    private void MinimizeToTrayToggle_Toggled(object sender, RoutedEventArgs e)
    {
        TrayIconManager.MinimizeToTrayEnabled = MinimizeToTrayToggle.IsOn;
    }

    private async void RestartDaemonBtn_Click(object sender, RoutedEventArgs e)
    {
        DaemonStatusDetailText.Text = "正在重新连接...";
        await DaemonSupervisor.Instance.EnsureStartedAsync();
        UpdateDaemonDetail();
    }

    private void OpenBrowserBtn_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            Process.Start(new ProcessStartInfo("http://127.0.0.1:19898/") { UseShellExecute = true });
        }
        catch
        {
            // ignore
        }
    }

    private void UpdateDaemonDetail()
    {
        DaemonStatusDetailText.Text = DaemonSupervisor.Instance.StatusDescription;
    }
}
