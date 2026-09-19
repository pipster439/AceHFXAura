using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed partial class HomePage : Page
{
    public HomePage()
    {
        InitializeComponent();
        Loaded += HomePage_Loaded;
    }

    private async void HomePage_Loaded(object sender, RoutedEventArgs e)
    {
        await RefreshStatusAsync();
    }

    private async void RefreshStatusBtn_Click(object sender, RoutedEventArgs e)
    {
        await RefreshStatusAsync();
    }

    private void OpenStudioBtn_Click(object sender, RoutedEventArgs e)
    {
        // 导航至 Studio
        if (MainWindow.CurrentNavFrame != null)
        {
            MainWindow.CurrentNavFrame.Navigate(typeof(StudioPage));
            if (MainWindow.CurrentNavView != null)
            {
                foreach (var item in MainWindow.CurrentNavView.MenuItems)
                {
                    if (item is NavigationViewItem nvi && (string)nvi.Tag == "studio")
                    {
                        MainWindow.CurrentNavView.SelectedItem = nvi;
                        break;
                    }
                }
            }
        }
    }

    private async System.Threading.Tasks.Task RefreshStatusAsync()
    {
        DaemonStatusText.Text = "正在检测...";
        await DaemonSupervisor.Instance.EnsureStartedAsync();
        DaemonStatusText.Text = DaemonSupervisor.Instance.StatusDescription;
    }
}
